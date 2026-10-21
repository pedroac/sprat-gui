#include "NormalMapPaintCanvas.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QUrl>
#include <QGraphicsEllipseItem>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>

// OpenGL offscreen compositing
#include <QOffscreenSurface>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QVector2D>
#include <QVector3D>

#include <QtMath>
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// NormalMapPaintCanvas
// ---------------------------------------------------------------------------

NormalMapPaintCanvas::NormalMapPaintCanvas(QWidget* parent)
    : MapPaintCanvas(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
}

NormalMapPaintCanvas::~NormalMapPaintCanvas()
{
    destroyGL();
}

// ---------------------------------------------------------------------------
// Sprite load / clear
// ---------------------------------------------------------------------------

void NormalMapPaintCanvas::clearSprite()
{
    m_layers.clear();
    m_activeLayerIdx = 0;
    m_compositeCache = QImage();
    m_lightIndicator         = nullptr;  // m_scene->clear() inside parent deletes these
    std::fill(std::begin(m_lightRays), std::end(m_lightRays), nullptr);
    m_fillPreviewLine        = nullptr;
    m_fillPreviewCircle      = nullptr;
    m_fillPreviewInnerCircle = nullptr;
    m_fillPreviewRect        = nullptr;
    m_fillPreviewInnerRect   = nullptr;
    m_litItem                = nullptr;
    m_measureDot1            = nullptr;
    m_measureDot2            = nullptr;
    m_measureLine1           = nullptr;
    m_measureLine2           = nullptr;
    m_measurePoly            = nullptr;
    m_measureArrow           = nullptr;
    m_measureState           = MeasureState::Idle;
    m_measureFlipped         = false;
    m_fillActive      = false;
    MapPaintCanvas::clearSprite();
    m_litCache = QImage();
    m_glSpriteDirty = m_glNormalDirty = true;
    emit layersChanged();
}

void NormalMapPaintCanvas::loadSprite(const QString& path)
{
    // Clear layers before base class calls updateCompanionItem() during its setup
    m_layers.clear();
    m_activeLayerIdx = 0;
    m_compositeCache = QImage();

    m_lightIndicator         = nullptr;  // m_scene->clear() inside parent deletes these
    m_fillPreviewLine        = nullptr;
    m_fillPreviewCircle      = nullptr;
    m_fillPreviewInnerCircle = nullptr;
    m_fillPreviewRect        = nullptr;
    m_fillPreviewInnerRect   = nullptr;
    m_litItem                = nullptr;
    m_measureDot1            = nullptr;
    m_measureDot2            = nullptr;
    m_measureLine1           = nullptr;
    m_measureLine2           = nullptr;
    m_measurePoly            = nullptr;
    m_measureArrow           = nullptr;
    m_measureState           = MeasureState::Idle;
    m_measureFlipped         = false;
    m_fillActive        = false;
    MapPaintCanvas::loadSprite(path);

    if (m_sprite.isNull()) {
        m_litCache = QImage();
        return;
    }

    const int w = m_sprite.width();
    const int h = m_sprite.height();

    // Initialize companion with flat normals, mirroring sprite alpha
    m_companion = QImage(m_sprite.size(), QImage::Format_ARGB32);
    const QRgb flatNormal    = qRgba(128, 128, 255, 255);
    const QRgb outsideNormal = qRgba(128, 128, 255, 0);
    for (int y = 0; y < h; ++y) {
        const QRgb* sl = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(y));
        QRgb* cl = reinterpret_cast<QRgb*>(m_companion.scanLine(y));
        for (int x = 0; x < w; ++x)
            cl[x] = (qAlpha(sl[x]) > 0) ? flatNormal : outsideNormal;
    }

    // Set dirty before any loadCompanion call so GL uploads fresh data
    m_glSpriteDirty = m_glNormalDirty = true;

    // Load existing _normal.png on top if present
    const QFileInfo fi(path);
    const QString normalPath =
        fi.dir().filePath(fi.completeBaseName() + QStringLiteral("_normal.png"));
    const QImage existing(normalPath);
    if (!existing.isNull()) {
        const QImage existingArgb = (existing.size() == m_sprite.size())
            ? existing.convertToFormat(QImage::Format_ARGB32)
            : existing.scaled(m_sprite.size()).convertToFormat(QImage::Format_ARGB32);
        loadCompanion(existingArgb);
    }
    // Create lit-preview layer (sits above companion/normal-map at z=1.5)
    if (!m_litItem && m_scene) {
        m_litItem = new QGraphicsPixmapItem();
        m_litItem->setZValue(1.5);
        m_litItem->setOpacity(m_viewBlend);
        m_scene->addItem(m_litItem);
    }

    // Create light indicator (circle + rays)
    if (!m_lightIndicator && m_scene) {
        m_lightIndicator = new QGraphicsEllipseItem();
        m_lightIndicator->setZValue(10);
        m_scene->addItem(m_lightIndicator);
        for (auto& ray : m_lightRays) {
            ray = new QGraphicsLineItem();
            ray->setZValue(9.5);
            m_scene->addItem(ray);
        }
    }
    updateLightIndicator();

    // Build initial layer stack from the loaded companion image
    {
        NormalLayer baseLayer;
        baseLayer.image = m_companion.copy();
        baseLayer.name  = tr("Normal Map");
        m_layers.append(baseLayer);
        m_activeLayerIdx = 0;

        // Check for multi-layer save file
        const QString metaPath =
            fi.dir().filePath(fi.completeBaseName() + QStringLiteral("_normal_layers.json"));
        if (QFile::exists(metaPath)) {
            loadLayersFromJson(metaPath, fi.dir(), fi.completeBaseName());
            if (m_layers.isEmpty()) {
                // Fallback: re-add base layer if JSON was empty/corrupt
                m_layers.append(baseLayer);
                m_activeLayerIdx = 0;
            }
            syncCompanionFromActiveLayer();
        }
    }

    recomputeLit();
    updateCompanionItem();
    emit layersChanged();
}

// ---------------------------------------------------------------------------
// Light indicator
// ---------------------------------------------------------------------------

qreal NormalMapPaintCanvas::lightIndicatorRadius() const
{
    // Perspective-like: larger when high (high Z), smaller when low (low Z).
    // At Z=0 → r=3 (min),  Z=100 (default) → r=6,  Z=300 → r=9,  Z→∞ → r=12 (max).
    return qBound(3.0, 12.0 * m_lightZ / (m_lightZ + 100.0), 12.0);
}

void NormalMapPaintCanvas::updateLightIndicator()
{
    if (!m_lightIndicator || m_sprite.isNull()) return;

    const qreal cx = m_sprite.width()  * 0.5 + m_lightX;
    const qreal cy = m_sprite.height() * 0.5 + m_lightY;
    const qreal r  = lightIndicatorRadius();

    // Fill: light color at partial opacity
    QColor fill = m_lightColor;
    fill.setAlpha(200);

    // Border: contrasting color based on perceptual luminance
    const qreal lum = 0.299 * m_lightColor.redF()
                    + 0.587 * m_lightColor.greenF()
                    + 0.114 * m_lightColor.blueF();
    const QColor border = (lum > 0.5) ? Qt::black : Qt::white;

    m_lightIndicator->setRect(cx - r, cy - r, r * 2, r * 2);
    m_lightIndicator->setPen(QPen(border, 1.5));
    m_lightIndicator->setBrush(fill);
    m_lightIndicator->setVisible(true);

    // Rays: length scales with light strength (0 strength → no rays)
    const qreal rayLen = qBound(0.0, double(m_lightStrength) * 10.0, 18.0);
    const QPen  rayPen(border, 1.2);
    const qreal gapFromEdge = r + 2.0;

    for (int i = 0; i < 8; ++i) {
        if (!m_lightRays[i]) continue;
        const qreal angle = qDegreesToRadians(45.0 * i);
        const qreal dx = std::cos(angle);
        const qreal dy = std::sin(angle);
        m_lightRays[i]->setLine(cx + dx * gapFromEdge,
                                 cy + dy * gapFromEdge,
                                 cx + dx * (gapFromEdge + rayLen),
                                 cy + dy * (gapFromEdge + rayLen));
        m_lightRays[i]->setPen(rayPen);
        m_lightRays[i]->setVisible(rayLen >= 1.0);
    }
}

static constexpr qreal kMeasureDotR = 4.0;
static const QColor    kMeasureColor(255, 220, 0);

void NormalMapPaintCanvas::mousePressEvent(QMouseEvent* event)
{
    if (m_readOnly) { MapPaintCanvas::mousePressEvent(event); return; }

    // Reset any leftover bevel stroke state (e.g. if mouse-release was missed).
    if (m_bevelStrokeActive) {
        m_bevelStrokeActive = false;
        m_bevelHeightBuf.clear();
        m_bevelBase = QImage();
    }

    // Pick mode: single left-click samples the composite normal map
    if (m_pickMode && !m_sprite.isNull() && event->button() == Qt::LeftButton) {
        const QPointF sp = mapToScene(event->pos());
        const int px = qRound(sp.x());
        const int py = qRound(sp.y());
        const QImage img = flattenedNormalMap();
        if (!img.isNull() && px >= 0 && py >= 0 && px < img.width() && py < img.height()) {
            const QRgb   rgb = img.pixel(px, py);
            const float  nx  = qRed(rgb)   / 127.5f - 1.0f;
            const float  ny  = qGreen(rgb) / 127.5f - 1.0f;
            const float  nz  = qBlue(rgb)  / 255.0f;
            m_pickMode = false;
            updateCursor();
            emit normalPicked(nx, ny, nz);
            emit pickCancelled();
        }
        event->accept();
        return;
    }

    // Measure mode intercepts both left-click (place points) and
    // middle-click (flip normal side) while active.
    if (m_measureState != MeasureState::Idle && !m_sprite.isNull()) {
        const QPointF sp = mapToScene(event->pos());

        // Middle-click during WaitingP3: flip the normal to the other side
        if (event->button() == Qt::MiddleButton &&
                m_measureState == MeasureState::WaitingP3) {
            m_measureFlipped = !m_measureFlipped;
            updateMeasurePreview(sp);
            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton) {
            static const QPen dotPen(Qt::black, 1);

            if (m_measureState == MeasureState::WaitingP1) {
                m_measureP1 = sp;
                if (m_scene) {
                    m_measureDot1 = new QGraphicsEllipseItem(
                        sp.x() - kMeasureDotR, sp.y() - kMeasureDotR,
                        kMeasureDotR * 2, kMeasureDotR * 2);
                    m_measureDot1->setPen(dotPen);
                    m_measureDot1->setBrush(kMeasureColor);
                    m_measureDot1->setZValue(5);
                    m_scene->addItem(m_measureDot1);

                    m_measureLine1 = new QGraphicsLineItem(
                        sp.x(), sp.y(), sp.x(), sp.y());
                    m_measureLine1->setPen(QPen(kMeasureColor, 2, Qt::DashLine));
                    m_measureLine1->setZValue(5);
                    m_scene->addItem(m_measureLine1);
                }
                m_measureState = MeasureState::WaitingP2;

            } else if (m_measureState == MeasureState::WaitingP2) {
                m_measureP2 = sp;
                if (m_measureLine1)
                    m_measureLine1->setLine(
                        m_measureP1.x(), m_measureP1.y(), sp.x(), sp.y());
                if (m_scene) {
                    m_measureDot2 = new QGraphicsEllipseItem(
                        sp.x() - kMeasureDotR, sp.y() - kMeasureDotR,
                        kMeasureDotR * 2, kMeasureDotR * 2);
                    m_measureDot2->setPen(dotPen);
                    m_measureDot2->setBrush(kMeasureColor);
                    m_measureDot2->setZValue(5);
                    m_scene->addItem(m_measureDot2);

                    // Rubber-band for edge 2
                    m_measureLine2 = new QGraphicsLineItem(
                        sp.x(), sp.y(), sp.x(), sp.y());
                    m_measureLine2->setPen(QPen(kMeasureColor, 2, Qt::DashLine));
                    m_measureLine2->setZValue(5);
                    m_scene->addItem(m_measureLine2);

                    // Filled parallelogram preview (below the lines)
                    m_measurePoly = new QGraphicsPolygonItem();
                    m_measurePoly->setPen(Qt::NoPen);
                    m_measurePoly->setZValue(4.5);
                    m_scene->addItem(m_measurePoly);

                    // Normal-direction arrow (above everything)
                    m_measureArrow = new QGraphicsPathItem();
                    m_measureArrow->setPen(QPen(Qt::white, 2));
                    m_measureArrow->setZValue(5.5);
                    m_measureArrow->setVisible(false);
                    m_scene->addItem(m_measureArrow);
                }
                m_measureState = MeasureState::WaitingP3;

            } else { // WaitingP3 — commit
                float azimuth = 0.0f, elevation = 0.0f;
                computeMeasureAngles(m_measureP1, m_measureP2, sp, azimuth, elevation);
                removeMeasureItems();
                m_measureState = MeasureState::Idle;
                updateCursor();
                emit anglesMeasured(azimuth, elevation);
                emit measureCancelled(); // uncheck the measure button in the workspace
            }
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::LeftButton && !m_sprite.isNull()) {
        // Light drag hit-test has priority
        if (m_lightIndicator && m_lightIndicator->isVisible()) {
            const QPointF sp = mapToScene(event->pos());
            const qreal cx = m_sprite.width()  * 0.5 + m_lightX;
            const qreal cy = m_sprite.height() * 0.5 + m_lightY;
            if (std::hypot(sp.x() - cx, sp.y() - cy) <= lightIndicatorRadius() + 4.0) {
                m_draggingLight = true;
                viewport()->setCursor(Qt::SizeAllCursor);
                event->accept();
                return;
            }
        }

        if (m_fillMode) {
            m_fillNormalFrom = m_paintColor;  // capture "from" normal at press time
            m_fillPressPos = mapToScene(event->pos()).toPoint();
            m_fillCurScenePos = QPointF(m_fillPressPos);
            m_fillActive = true;

            const bool isDomeOrCone = (m_fillType == FillType::Dome ||
                                       m_fillType == FillType::Cone);
            if (isDomeOrCone) {
                if (!m_fillPreviewCircle && m_scene) {
                    m_fillPreviewCircle = new QGraphicsEllipseItem();
                    m_fillPreviewCircle->setPen(QPen(Qt::white, 1.5, Qt::DashLine));
                    m_fillPreviewCircle->setZValue(11);
                    m_scene->addItem(m_fillPreviewCircle);
                }
                if (m_fillPreviewCircle)
                    m_fillPreviewCircle->setRect(QRectF(m_fillPressPos.x(),
                                                        m_fillPressPos.y(), 0, 0));
            } else if (m_fillType == FillType::Donut) {
                if (!m_fillPreviewCircle && m_scene) {
                    m_fillPreviewCircle = new QGraphicsEllipseItem();
                    m_fillPreviewCircle->setPen(QPen(Qt::white, 1.5, Qt::DashLine));
                    m_fillPreviewCircle->setZValue(11);
                    m_scene->addItem(m_fillPreviewCircle);
                }
                if (m_fillPreviewCircle)
                    m_fillPreviewCircle->setRect(QRectF(m_fillPressPos.x(),
                                                        m_fillPressPos.y(), 0, 0));
                if (!m_fillPreviewInnerCircle && m_scene) {
                    m_fillPreviewInnerCircle = new QGraphicsEllipseItem();
                    m_fillPreviewInnerCircle->setPen(QPen(Qt::white, 1.0, Qt::DotLine));
                    m_fillPreviewInnerCircle->setZValue(11);
                    m_scene->addItem(m_fillPreviewInnerCircle);
                }
                if (m_fillPreviewInnerCircle)
                    m_fillPreviewInnerCircle->setRect(QRectF(m_fillPressPos.x(),
                                                             m_fillPressPos.y(), 0, 0));
            } else if (m_fillType == FillType::Prism) {
                if (!m_fillPreviewRect && m_scene) {
                    m_fillPreviewRect = new QGraphicsRectItem();
                    m_fillPreviewRect->setPen(QPen(Qt::white, 1.5, Qt::DashLine));
                    m_fillPreviewRect->setZValue(11);
                    m_scene->addItem(m_fillPreviewRect);
                }
                if (m_fillPreviewRect)
                    m_fillPreviewRect->setRect(QRectF(m_fillPressPos.x(),
                                                      m_fillPressPos.y(), 0, 0));
                if (!m_fillPreviewInnerRect && m_scene) {
                    m_fillPreviewInnerRect = new QGraphicsRectItem();
                    m_fillPreviewInnerRect->setPen(QPen(Qt::white, 1.0, Qt::DotLine));
                    m_fillPreviewInnerRect->setZValue(11);
                    m_scene->addItem(m_fillPreviewInnerRect);
                }
                if (m_fillPreviewInnerRect)
                    m_fillPreviewInnerRect->setRect(QRectF(m_fillPressPos.x(),
                                                           m_fillPressPos.y(), 0, 0));
            } else if (m_fillType != FillType::HeightField
                    && m_fillType != FillType::EdgeEmboss
                    && m_fillType != FillType::EdgeTrace
                    && m_fillType != FillType::AutoNormal) {
                if (!m_fillPreviewLine && m_scene) {
                    m_fillPreviewLine = new QGraphicsLineItem();
                    m_fillPreviewLine->setPen(QPen(Qt::white, 1.5, Qt::DashLine));
                    m_fillPreviewLine->setZValue(11);
                    m_scene->addItem(m_fillPreviewLine);
                }
                if (m_fillPreviewLine)
                    m_fillPreviewLine->setLine(m_fillPressPos.x(), m_fillPressPos.y(),
                                               m_fillPressPos.x(), m_fillPressPos.y());
            }
            event->accept();
            return;
        }
    }
    MapPaintCanvas::mousePressEvent(event);
}

void NormalMapPaintCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_readOnly) { MapPaintCanvas::mouseMoveEvent(event); return; }

    if (m_measureState == MeasureState::WaitingP2 && m_measureLine1) {
        const QPointF sp = mapToScene(event->pos());
        m_measureLine1->setLine(m_measureP1.x(), m_measureP1.y(), sp.x(), sp.y());
        event->accept();
        return;
    }
    if (m_measureState == MeasureState::WaitingP3) {
        updateMeasurePreview(mapToScene(event->pos()));
        event->accept();
        return;
    }
    if (m_draggingLight) {
        const QPointF sp = mapToScene(event->pos());
        const int nx = int(sp.x() - m_sprite.width()  * 0.5);
        const int ny = int(sp.y() - m_sprite.height() * 0.5);
        m_lightX = nx;
        m_lightY = ny;
        updateLightIndicator();
        recomputeLit();
        updateCompanionItem();
        emit lightPositionChanged(nx, ny);
        viewport()->setCursor(Qt::SizeAllCursor);
        event->accept();
        return;
    }
    if (m_fillActive) {
        const QPoint cur = mapToScene(event->pos()).toPoint();
        m_fillCurScenePos = QPointF(cur);

        if (m_fillType == FillType::Dome) {
            // Dome: ellipse preview matching the painted ellipse (rx=|Δx|, ry=|Δy|)
            const float rx = std::abs(float(cur.x() - m_fillPressPos.x()));
            const float ry = std::abs(float(cur.y() - m_fillPressPos.y()));
            if (m_fillPreviewCircle)
                m_fillPreviewCircle->setRect(QRectF(m_fillPressPos.x() - rx,
                                                    m_fillPressPos.y() - ry,
                                                    rx * 2.0f, ry * 2.0f));
        } else if (m_fillType == FillType::Cone) {
            // Cone: circle preview (radius = drag length)
            const float r = std::hypot(float(cur.x() - m_fillPressPos.x()),
                                       float(cur.y() - m_fillPressPos.y()));
            if (m_fillPreviewCircle)
                m_fillPreviewCircle->setRect(QRectF(m_fillPressPos.x() - r,
                                                    m_fillPressPos.y() - r,
                                                    r * 2.0f, r * 2.0f));
        } else if (m_fillType == FillType::Donut) {
            updateDonutPreviewEllipses();
        } else if (m_fillType == FillType::Prism) {
            updatePrismPreviewRects();
        } else if (m_fillType != FillType::HeightField
                && m_fillType != FillType::EdgeEmboss) {
            if (m_fillPreviewLine)
                m_fillPreviewLine->setLine(m_fillPressPos.x(), m_fillPressPos.y(),
                                           cur.x(), cur.y());
        }
        event->accept();
        return;
    }

    // Check light hover before delegating so we can set the cursor correctly afterwards
    const bool overLight = [&]() -> bool {
        if (!m_lightIndicator || !m_lightIndicator->isVisible() || m_sprite.isNull())
            return false;
        const QPointF sp = mapToScene(event->pos());
        const qreal cx = m_sprite.width()  * 0.5 + m_lightX;
        const qreal cy = m_sprite.height() * 0.5 + m_lightY;
        return std::hypot(sp.x() - cx, sp.y() - cy) <= lightIndicatorRadius() + 4.0;
    }();

    if (overLight != m_lightHovered) {
        m_lightHovered = overLight;
        if (!overLight)
            updateCursor();   // restore brush/cross cursor when leaving the indicator
    }

    MapPaintCanvas::mouseMoveEvent(event);

    if (overLight)
        viewport()->setCursor(Qt::SizeAllCursor);
}

void NormalMapPaintCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_draggingLight && event->button() == Qt::LeftButton) {
        m_draggingLight = false;
        // Cursor will be corrected by the next mouseMoveEvent
        event->accept();
        return;
    }
    if (m_bevelStrokeActive && event->button() == Qt::LeftButton) {
        m_bevelStrokeActive = false;
        m_bevelHeightBuf.clear();
        m_bevelBase = QImage();
    }
    if (m_fillActive && event->button() == Qt::LeftButton) {
        m_fillActive = false;
        const QPoint end = mapToScene(event->pos()).toPoint();
        if (m_fillPreviewLine && m_scene) {
            m_scene->removeItem(m_fillPreviewLine);
            delete m_fillPreviewLine;
            m_fillPreviewLine = nullptr;
        }
        if (m_fillPreviewCircle && m_scene) {
            m_scene->removeItem(m_fillPreviewCircle);
            delete m_fillPreviewCircle;
            m_fillPreviewCircle = nullptr;
        }
        if (m_fillPreviewInnerCircle && m_scene) {
            m_scene->removeItem(m_fillPreviewInnerCircle);
            delete m_fillPreviewInnerCircle;
            m_fillPreviewInnerCircle = nullptr;
        }
        if (m_fillPreviewRect && m_scene) {
            m_scene->removeItem(m_fillPreviewRect);
            delete m_fillPreviewRect;
            m_fillPreviewRect = nullptr;
        }
        if (m_fillPreviewInnerRect && m_scene) {
            m_scene->removeItem(m_fillPreviewInnerRect);
            delete m_fillPreviewInnerRect;
            m_fillPreviewInnerRect = nullptr;
        }
        applyFill(m_fillPressPos, end);
        event->accept();
        return;
    }
    MapPaintCanvas::mouseReleaseEvent(event);
}

// ---------------------------------------------------------------------------
// Measure-angle mode
// ---------------------------------------------------------------------------

void NormalMapPaintCanvas::removeMeasureItems()
{
    auto remove = [this](QGraphicsItem*& item) {
        if (item && m_scene) { m_scene->removeItem(item); delete item; }
        item = nullptr;
    };
    remove(reinterpret_cast<QGraphicsItem*&>(m_measureDot1));
    remove(reinterpret_cast<QGraphicsItem*&>(m_measureDot2));
    remove(reinterpret_cast<QGraphicsItem*&>(m_measureLine1));
    remove(reinterpret_cast<QGraphicsItem*&>(m_measureLine2));
    remove(reinterpret_cast<QGraphicsItem*&>(m_measurePoly));
    remove(reinterpret_cast<QGraphicsItem*&>(m_measureArrow));
}

void NormalMapPaintCanvas::computeMeasureAngles(
    const QPointF& p1, const QPointF& p2, const QPointF& p3,
    float& azimuthDeg, float& elevationDeg) const
{
    const float e1x = float(p2.x() - p1.x());
    const float e1y = float(p2.y() - p1.y());
    const float e2x = float(p3.x() - p2.x());
    const float e2y = float(p3.y() - p2.y());

    const float len1 = std::hypot(e1x, e1y);
    const float len2 = std::hypot(e2x, e2y);

    azimuthDeg   = 0.0f;
    elevationDeg = 0.0f;

    if (len1 < 1e-4f) return;
    if (len2 < 1e-4f) { elevationDeg = 90.0f; return; }

    // Elevation: how much L2 deviates from being perpendicular to L1.
    // arcsin(|e1̂·e2̂|): 0° when L2 ⊥ L1 (face-on), 90° when L2 ∥ L1 (max tilt).
    // Using absolute value makes it symmetric — both forward and backward tilts
    // of the same angle give the same elevation.
    const float dot_e1e2 = (e1x * e2x + e1y * e2y) / (len1 * len2);
    elevationDeg = std::asin(std::clamp(std::abs(dot_e1e2), 0.0f, 1.0f)) * 180.0f / float(M_PI);

    // Azimuth: perpendicular to e1, on the P3 side (flipped by m_measureFlipped)
    const float cross    = e1x * e2y - e1y * e2x;
    const bool  pickLeft = m_measureFlipped ? (cross < 0.0f) : (cross > 0.0f);
    const float px = pickLeft ? (-e1y / len1) : ( e1y / len1);
    const float py = pickLeft ? ( e1x / len1) : (-e1x / len1);

    azimuthDeg = std::atan2(py, px) * 180.0f / float(M_PI);
    if (azimuthDeg < 0.0f) azimuthDeg += 360.0f;
}

void NormalMapPaintCanvas::updateMeasurePreview(const QPointF& p3)
{
    // Update rubber-band for second edge
    if (m_measureLine2)
        m_measureLine2->setLine(m_measureP2.x(), m_measureP2.y(), p3.x(), p3.y());

    if (!m_measurePoly || !m_scene) return;

    // Build a perspective trapezoid that mimics how a rectangle looks in perspective:
    //   P1→P2  : the fixed axis (near edge)
    //   P2→P3  : the second line the user is drawing (one depth edge)
    //   P4     : mirror of (P3−P2) reflected across the perpendicular of L1, placed at P1
    //            (the opposite depth edge — when L2⊥L1 this is parallel → rectangle;
    //             otherwise the two depth edges converge/diverge like a perspective view)
    const float e1x  = float(m_measureP2.x() - m_measureP1.x());
    const float e1y  = float(m_measureP2.y() - m_measureP1.y());
    const float len1 = std::hypot(e1x, e1y);
    if (len1 < 1e-4f) return;

    // Unit perpendicular to the axis (rotated 90° CCW)
    const float px0 = -e1y / len1;
    const float py0 =  e1x / len1;

    // Vector from P2 to P3 (the drawn second line)
    const float l2x = float(p3.x() - m_measureP2.x());
    const float l2y = float(p3.y() - m_measureP2.y());

    // Reflect (l2x, l2y) across the perpendicular axis p: reflect(v, p̂) = 2(v·p̂)p̂ − v
    const float dot_p = l2x * px0 + l2y * py0;
    const float rdx   = 2.0f * dot_p * px0 - l2x;
    const float rdy   = 2.0f * dot_p * py0 - l2y;

    // P4 is the mirrored depth edge starting from P1
    const QPointF p4(m_measureP1.x() + rdx, m_measureP1.y() + rdy);
    m_measurePoly->setPolygon(QPolygonF({m_measureP1, m_measureP2, p3, p4}));

    // Compute the normal direction
    float az = 0.0f, el = 0.0f;
    computeMeasureAngles(m_measureP1, m_measureP2, p3, az, el);

    constexpr float kPi = 3.14159265f;
    const float elRad = el * kPi / 180.0f;
    const float azRad = az * kPi / 180.0f;
    const float nz = std::cos(elRad);
    const float r  = std::sin(elRad);
    const float nx = r * std::cos(azRad);
    const float ny = r * std::sin(azRad);

    // Normal map color for the fill
    const QColor fill(
        std::clamp(int((nx + 1.0f) * 127.5f), 0, 255),
        std::clamp(int((ny + 1.0f) * 127.5f), 0, 255),
        std::clamp(int( nz         * 255.0f),  0, 255),
        160);
    m_measurePoly->setBrush(fill);
    m_measurePoly->setPen(QPen(fill.lighter(130), 1));

    // Broadcast live angle update so the wheel widget tracks the drag
    emit anglesMeasured(az, el);

    // Arrow: from center of parallelogram in the 2D normal (nx, ny) direction
    if (!m_measureArrow) return;
    const float xyLen = std::hypot(nx, ny);
    if (el < 0.5f || xyLen < 1e-4f) {
        m_measureArrow->setVisible(false);
        return;
    }
    m_measureArrow->setVisible(true);

    const QPointF center = (m_measureP1 + m_measureP2 + p3 + p4) / 4.0;
    const float dx = nx / xyLen;
    const float dy = ny / xyLen;

    constexpr float kArrLen  = 20.0f;
    constexpr float kHeadLen = 6.0f;
    constexpr float kHeadAng = 30.0f * kPi / 180.0f;
    const float cosH = std::cos(kHeadAng);
    const float sinH = std::sin(kHeadAng);

    const QPointF tip(center.x() + dx * kArrLen, center.y() + dy * kArrLen);
    // Arrowhead lines: rotate (-dx,-dy) by ±kHeadAng
    const QPointF h1(tip.x() + kHeadLen * (-dx * cosH + dy * sinH),
                     tip.y() + kHeadLen * (-dx * sinH - dy * cosH));
    const QPointF h2(tip.x() + kHeadLen * (-dx * cosH - dy * sinH),
                     tip.y() + kHeadLen * ( dx * sinH - dy * cosH));

    QPainterPath path;
    path.moveTo(center); path.lineTo(tip);
    path.moveTo(tip);    path.lineTo(h1);
    path.moveTo(tip);    path.lineTo(h2);
    m_measureArrow->setPath(path);
}

void NormalMapPaintCanvas::startPick()
{
    cancelMeasure();
    m_pickMode = true;
    viewport()->setCursor(Qt::CrossCursor);
}

void NormalMapPaintCanvas::cancelPick()
{
    if (!m_pickMode) return;
    m_pickMode = false;
    updateCursor();
    emit pickCancelled();
}

void NormalMapPaintCanvas::startMeasure()
{
    cancelPick();
    cancelMeasure();
    m_measureFlipped = false;
    m_measureState   = MeasureState::WaitingP1;
    viewport()->setCursor(Qt::CrossCursor);
}

void NormalMapPaintCanvas::cancelMeasure()
{
    if (m_measureState == MeasureState::Idle) return;
    m_measureState = MeasureState::Idle;
    removeMeasureItems();
    updateCursor();
    emit measureCancelled();
}

void NormalMapPaintCanvas::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_pickMode) {
            cancelPick();
            event->accept();
            return;
        }
        if (m_measureState != MeasureState::Idle) {
            cancelMeasure();
            event->accept();
            return;
        }
    }
    MapPaintCanvas::keyPressEvent(event);
}

void NormalMapPaintCanvas::wheelEvent(QWheelEvent* event)
{
    if (m_fillActive && m_fillType == FillType::Prism && !m_sprite.isNull()) {
        const int delta = event->angleDelta().y();
        if (delta != 0) {
            const float step = (delta > 0) ? 0.05f : -0.05f;
            m_prismSideT = qBound(0.0f, m_prismSideT + step, 1.0f);
            m_prismSideR = qBound(0.0f, m_prismSideR + step, 1.0f);
            m_prismSideB = qBound(0.0f, m_prismSideB + step, 1.0f);
            m_prismSideL = qBound(0.0f, m_prismSideL + step, 1.0f);
            updatePrismPreviewRects();
            emit prismSidesChanged(m_prismSideT, m_prismSideR, m_prismSideB, m_prismSideL);
            event->accept();
            return;
        }
    }
    if (m_fillActive && m_fillType == FillType::Donut && !m_sprite.isNull()) {
        const int delta = event->angleDelta().y();
        if (delta != 0) {
            const float step = (delta > 0) ? 0.05f : -0.05f;
            m_donutLength = qBound(0.01f, m_donutLength + step, 1.0f);
            updateDonutPreviewEllipses();
            emit donutLengthChanged(m_donutLength);
            event->accept();
            return;
        }
    }
    if (m_lightHovered && !m_sprite.isNull()) {
        const int delta = event->angleDelta().y();
        if (delta != 0) {
            const int step  = (delta > 0) ? 10 : -10;
            const int newZ  = qBound(0, m_lightZ + step, 9999);
            if (newZ != m_lightZ)
                setLightZ(newZ);
            event->accept();
            return;
        }
    }
    MapPaintCanvas::wheelEvent(event);
}

// ---------------------------------------------------------------------------
// Fill tool
// ---------------------------------------------------------------------------

void NormalMapPaintCanvas::updateDonutPreviewEllipses()
{
    const float rx = std::abs(float(m_fillCurScenePos.x() - m_fillPressPos.x()));
    const float ry = std::abs(float(m_fillCurScenePos.y() - m_fillPressPos.y()));

    if (m_fillPreviewCircle)
        m_fillPreviewCircle->setRect(QRectF(m_fillPressPos.x() - rx,
                                            m_fillPressPos.y() - ry,
                                            rx * 2.0f, ry * 2.0f));

    const float innerScale = qMax(0.0f, 1.0f - m_donutLength);
    const float innerRx = rx * innerScale;
    const float innerRy = ry * innerScale;
    if (m_fillPreviewInnerCircle)
        m_fillPreviewInnerCircle->setRect(QRectF(m_fillPressPos.x() - innerRx,
                                                  m_fillPressPos.y() - innerRy,
                                                  innerRx * 2.0f, innerRy * 2.0f));
}

void NormalMapPaintCanvas::updatePrismPreviewRects()
{
    const QPoint cur = m_fillCurScenePos.toPoint();
    const float x0 = float(qMin(m_fillPressPos.x(), cur.x()));
    const float y0 = float(qMin(m_fillPressPos.y(), cur.y()));
    const float x1 = float(qMax(m_fillPressPos.x(), cur.x()));
    const float y1 = float(qMax(m_fillPressPos.y(), cur.y()));

    if (m_fillPreviewRect)
        m_fillPreviewRect->setRect(QRectF(x0, y0, x1 - x0, y1 - y0));

    if (m_fillPreviewInnerRect) {
        const float hw = (x1 - x0) * 0.5f;
        const float hh = (y1 - y0) * 0.5f;
        const float cx = x0 + hw;
        const float cy = y0 + hh;
        const float il = cx - (1.0f - m_prismSideL) * hw;
        const float ir = cx + (1.0f - m_prismSideR) * hw;
        const float it = cy - (1.0f - m_prismSideT) * hh;
        const float ib = cy + (1.0f - m_prismSideB) * hh;
        m_fillPreviewInnerRect->setRect(QRectF(il, it,
                                               qMax(0.0f, ir - il),
                                               qMax(0.0f, ib - it)));
    }
}

void NormalMapPaintCanvas::setFillMode(bool on)
{
    m_fillMode = on;
    if (!on) {
        m_fillActive = false;
        if (m_fillPreviewLine && m_scene) {
            m_scene->removeItem(m_fillPreviewLine);
            delete m_fillPreviewLine;
            m_fillPreviewLine = nullptr;
        }
        if (m_fillPreviewCircle && m_scene) {
            m_scene->removeItem(m_fillPreviewCircle);
            delete m_fillPreviewCircle;
            m_fillPreviewCircle = nullptr;
        }
        if (m_fillPreviewInnerCircle && m_scene) {
            m_scene->removeItem(m_fillPreviewInnerCircle);
            delete m_fillPreviewInnerCircle;
            m_fillPreviewInnerCircle = nullptr;
        }
        if (m_fillPreviewRect && m_scene) {
            m_scene->removeItem(m_fillPreviewRect);
            delete m_fillPreviewRect;
            m_fillPreviewRect = nullptr;
        }
        if (m_fillPreviewInnerRect && m_scene) {
            m_scene->removeItem(m_fillPreviewInnerRect);
            delete m_fillPreviewInnerRect;
            m_fillPreviewInnerRect = nullptr;
        }
    }
}

void NormalMapPaintCanvas::setFillType(FillType type)
{
    m_fillType = type;
}


void NormalMapPaintCanvas::setFillFitToSelection(bool fit)
{
    m_fillFitToSel = fit;
}

void NormalMapPaintCanvas::setFillEndNormal(float nx, float ny, float nz)
{
    const int r = qBound(0, int((nx + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
    const int g = qBound(0, int((ny + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
    const int b = qBound(0, int(nz * 255.0f + 0.5f), 255);
    m_fillNormalTo = QColor(r, g, b, 255);
}

void NormalMapPaintCanvas::setFillInvert(bool invert) { m_fillInvert = invert; }
void NormalMapPaintCanvas::setPrismSides(float t, float r, float b, float l)
{
    m_prismSideT = t; m_prismSideR = r; m_prismSideB = b; m_prismSideL = l;
}
void NormalMapPaintCanvas::setPrismAngle(float degrees)
{
    m_prismAngle = qBound(0.0f, degrees, 90.0f);
}
void NormalMapPaintCanvas::setConeEdge(float x, float y)  { m_coneEdgeX = x; m_coneEdgeY = y; }
void NormalMapPaintCanvas::setDonutLength(float length)   { m_donutLength    = length;  }
void NormalMapPaintCanvas::setFillRoundness(float r)      { m_fillRoundness  = qBound(0.01f, r, 1.0f); }

void NormalMapPaintCanvas::setEmbossHeight(float h)      { m_embossHeight      = qBound(0.0f, h,   1.0f); }
void NormalMapPaintCanvas::setEmbossStrength(float s)    { m_embossStrength    = qBound(0.0f, s,   1.0f); }
void NormalMapPaintCanvas::setEmbossAngle(float deg)     { m_embossAngle       = deg; }
void NormalMapPaintCanvas::setEmbossInnerWidth(float px) { m_embossInnerWidth  = qMax(0.5f, px); }
void NormalMapPaintCanvas::setEmbossOuterWidth(float px) { m_embossOuterWidth  = qMax(0.0f, px); }
void NormalMapPaintCanvas::setEmbossMidpoint(float mp)   { m_embossMidpoint    = qBound(0.0f, mp,  0.99f); }
void NormalMapPaintCanvas::setEmbossEdgeMode(int mode)   { m_embossEdgeMode    = mode; }
void NormalMapPaintCanvas::setEmbossCorner(int corner)   { m_embossCorner      = corner; }
void NormalMapPaintCanvas::setEmbossBevelShape(int shape){ m_embossBevelShape   = shape; }

void NormalMapPaintCanvas::setHeightFieldSource(int source) { m_heightFieldSource = source; }
void NormalMapPaintCanvas::setHeightFieldScale(float scale) { m_heightFieldScale  = qMax(0.01f, scale); }
void NormalMapPaintCanvas::setHeightFieldBlur(float blur)   { m_heightFieldBlur   = qMax(0.0f, blur); }

void NormalMapPaintCanvas::setEdgeTraceSource(int source)      { m_edgeTraceSource      = source; }
void NormalMapPaintCanvas::setEdgeTraceThreshold(float t)      { m_edgeTraceThreshold   = qBound(0.0f, t, 1.0f); }
void NormalMapPaintCanvas::setEdgeTraceBlur(float blur)        { m_edgeTraceBlur        = qMax(0.0f, blur); }
void NormalMapPaintCanvas::setEdgeTraceScale(float scale)      { m_edgeTraceScale       = qMax(0.01f, scale); }
void NormalMapPaintCanvas::setEdgeTraceSpread(float spread)    { m_edgeTraceSpread      = qMax(0.0f, spread); }

void NormalMapPaintCanvas::setAutoSource(int source)           { m_autoSource           = source; }
void NormalMapPaintCanvas::setAutoSmooth(float blur)           { m_autoSmooth           = qMax(0.0f, blur); }
void NormalMapPaintCanvas::setAutoPillowStrength(float s)      { m_autoPillowStrength   = qMax(0.0f, s); }
void NormalMapPaintCanvas::setAutoDetailStrength(float s)      { m_autoDetailStrength   = qMax(0.0f, s); }
void NormalMapPaintCanvas::setAutoPanelStrength(float s)       { m_autoPanelStrength    = qMax(0.0f, s); }
void NormalMapPaintCanvas::setAutoPanelThreshold(float t)      { m_autoPanelThreshold   = qBound(0.0f, t, 1.0f); }
void NormalMapPaintCanvas::setAutoPanelWidth(float px)         { m_autoPanelWidth       = qMax(0.5f, px); }

void NormalMapPaintCanvas::setEdgeEmbossSource(int source)     { m_edgeEmbossSource     = source; }
void NormalMapPaintCanvas::setEdgeEmbossThreshold(float t)     { m_edgeEmbossThreshold  = qBound(0.0f, t, 1.0f); }
void NormalMapPaintCanvas::setEdgeEmbossBlur(float blur)       { m_edgeEmbossBlur       = qMax(0.0f, blur); }
void NormalMapPaintCanvas::setEdgeEmbossWidth(float px)        { m_edgeEmbossWidth      = qMax(0.5f, px); }
void NormalMapPaintCanvas::setEdgeEmbossHeight(float h)        { m_edgeEmbossHeight     = qBound(0.0f, h, 1.0f); }
void NormalMapPaintCanvas::setEdgeEmbossMidpoint(float mp)     { m_edgeEmbossMidpoint   = qBound(0.0f, mp, 0.99f); }
void NormalMapPaintCanvas::setEdgeEmbossBevelShape(int shape)  { m_edgeEmbossBevelShape = shape; }
void NormalMapPaintCanvas::setEdgeEmbossCorner(int corner)     { m_edgeEmbossCorner     = corner; }

// Separable Gaussian blur for float fields (in-place, clamp-to-edge).
// O(W*H*sigma) — only use for small sigma (≤ ~10). For large sigma use approxGaussianLarge.
static void gaussianBlurSep(std::vector<float>& field, int w, int h, float sigma)
{
    const int r = std::max(1, int(std::ceil(sigma * 3.0f)));
    std::vector<float> kernel(2 * r + 1);
    float ksum = 0.0f;
    for (int i = -r; i <= r; ++i) {
        kernel[i + r] = std::exp(-float(i * i) / (2.0f * sigma * sigma));
        ksum += kernel[i + r];
    }
    for (auto& k : kernel) k /= ksum;

    std::vector<float> tmp(w * h, 0.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float v = 0.0f;
            for (int k = -r; k <= r; ++k)
                v += field[y * w + qBound(0, x + k, w - 1)] * kernel[k + r];
            tmp[y * w + x] = v;
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float v = 0.0f;
            for (int k = -r; k <= r; ++k)
                v += tmp[qBound(0, y + k, h - 1) * w + x] * kernel[k + r];
            field[y * w + x] = v;
        }
}

// One separable box-blur pass (horizontal then vertical) using a sliding-window running sum.
// O(W*H) per call regardless of radius — use for large-sigma blurs.
static void boxBlurSep(std::vector<float>& field, int w, int h, int r)
{
    if (r <= 0) return;
    const float norm = 1.0f / float(2 * r + 1);
    std::vector<float> tmp(w * h);

    // Horizontal pass
    for (int y = 0; y < h; ++y) {
        const float* src = field.data() + y * w;
        float*       dst = tmp.data()   + y * w;
        float sum = 0.0f;
        for (int k = -r; k <= r; ++k) sum += src[qBound(0, k, w - 1)];
        for (int x = 0; x < w; ++x) {
            dst[x] = sum * norm;
            sum -= src[qBound(0, x - r,     w - 1)];
            sum += src[qBound(0, x + r + 1, w - 1)];
        }
    }
    // Vertical pass
    for (int x = 0; x < w; ++x) {
        float sum = 0.0f;
        for (int k = -r; k <= r; ++k) sum += tmp[qBound(0, k, h - 1) * w + x];
        for (int y = 0; y < h; ++y) {
            field[y * w + x] = sum * norm;
            sum -= tmp[qBound(0, y - r,     h - 1) * w + x];
            sum += tmp[qBound(0, y + r + 1, h - 1) * w + x];
        }
    }
}

// Approximate Gaussian with large sigma via 3 box-blur passes (O(W*H) total, any sigma).
// Radius formula: r(r+1) = sigma² → r ≈ sigma - 0.5.
static void approxGaussianLarge(std::vector<float>& field, int w, int h, float sigma)
{
    const int r = qMax(1, int(std::round(sigma - 0.5f)));
    boxBlurSep(field, w, h, r);
    boxBlurSep(field, w, h, r);
    boxBlurSep(field, w, h, r);
}

// BFS distance field: for each selected+opaque pixel, its 4-connected distance
// to the nearest non-selected (or transparent) pixel, normalized to [0,1].
// Boundary pixels → 0, interior peak → 1.  Non-selected pixels stay at 0.
static std::vector<float> buildDistField(
    const QImage& sprite, const QImage& selMask, int w, int h, int* outMaxDist = nullptr)
{
    auto isSelOpaque = [&](int x, int y) -> bool {
        if (x < 0 || y < 0 || x >= w || y >= h) return false;
        if (qAlpha(reinterpret_cast<const QRgb*>(sprite.constScanLine(y))[x]) == 0)
            return false;
        return selMask.constScanLine(y)[x] > 0;
    };
    std::vector<int> dist(w * h, -1);
    std::queue<int> q;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!isSelOpaque(x, y)) continue;
            if (!isSelOpaque(x-1,y) || !isSelOpaque(x+1,y) ||
                !isSelOpaque(x,y-1) || !isSelOpaque(x,y+1)) {
                dist[y*w+x] = 0;
                q.push(y*w+x);
            }
        }
    static constexpr int dx4[4] = {-1,1,0,0};
    static constexpr int dy4[4] = {0,0,-1,1};
    int maxDist = 0;
    while (!q.empty()) {
        const int idx = q.front(); q.pop();
        const int cx = idx % w, cy = idx / w;
        for (int k = 0; k < 4; ++k) {
            const int nx = cx + dx4[k], ny = cy + dy4[k];
            if (!isSelOpaque(nx, ny)) continue;
            const int nidx = ny*w+nx;
            if (dist[nidx] < 0) {
                dist[nidx] = dist[idx] + 1;
                maxDist = qMax(maxDist, dist[nidx]);
                q.push(nidx);
            }
        }
    }
    std::vector<float> field(w * h, 0.0f);
    if (maxDist > 0)
        for (int i = 0; i < w * h; ++i)
            if (dist[i] > 0)
                field[i] = float(dist[i]) / float(maxDist);
    if (outMaxDist) *outMaxDist = maxDist;
    return field;
}

void NormalMapPaintCanvas::applyFill(QPoint start, QPoint end)
{
    if (m_sprite.isNull() || m_companion.isNull()) return;

    pushUndoSnapshot();

    const int w = m_companion.width();
    const int h = m_companion.height();
    const bool hasSel = m_hasSelection && !m_selectionMask.isNull();

    // When "Fit to selection" is on but there is no active selection, treat
    // the sprite silhouette (all opaque pixels) as the selection for fit purposes.
    QImage synthSel;
    const QImage* effectiveSel = hasSel ? &m_selectionMask : nullptr;
    if (m_fillFitToSel && !hasSel) {
        synthSel = QImage(w, h, QImage::Format_Grayscale8);
        for (int y2 = 0; y2 < h; ++y2) {
            const QRgb* sp2 = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(y2));
            uchar* sl = synthSel.scanLine(y2);
            for (int x2 = 0; x2 < w; ++x2)
                sl[x2] = (qAlpha(sp2[x2]) > 0) ? 255 : 0;
        }
        effectiveSel = &synthSel;
    }
    // effectiveHasSel: true when there is something meaningful to fit against
    const bool effectiveHasSel = hasSel || (m_fillFitToSel && !m_sprite.isNull());

    const int sr = m_fillNormalFrom.red();
    const int sg = m_fillNormalFrom.green();
    const int sb = m_fillNormalFrom.blue();
    // Slope blends From → flat (128,128,255); TwoSides blends From → user-chosen To
    const bool isSlopeType = (m_fillType == FillType::Slope);
    const int er = isSlopeType ? 128 : m_fillNormalTo.red();
    const int eg = isSlopeType ? 128 : m_fillNormalTo.green();
    const int eb = isSlopeType ? 255 : m_fillNormalTo.blue();

    const QPoint delta = end - start;
    const float lenSq  = float(delta.x() * delta.x() + delta.y() * delta.y());
    const float radius = std::sqrt(lenSq);  // for radial types

    // --- Fit-to-selection pre-scan ----------------------------------------
    //
    // Slope:    per-perpendicular-band normalization (each row/strip stretched to fill selection).
    // Cylinder: global projection normalization.
    // Prism:    bounding box of selection.
    // Dome/Cone: centroid + max-distance radius.
    // Donut:    BFS boundary distance field (ring at boundary, interior = hole).
    //
    float fitTMin  = 0.0f, fitTMax = 1.0f;   // Cylinder global remapping
    float fitRadius = radius;                   // radial non-adapted fallback
    float centX = float(start.x()), centY = float(start.y());  // Dome/Cone centre
    float prismFitX0 = 0.0f, prismFitY0 = 0.0f; // Prism fit bounding box
    float prismFitX1 = 0.0f, prismFitY1 = 0.0f;

    // Slope fit: per-band [min_proj, max_proj]; key = rounded perpendicular coordinate
    std::unordered_map<int, std::pair<float,float>> slopeBandRange;
    float slopeNdx = 0.0f, slopeNdy = 0.0f;  // normalised drag direction for band lookup
    int   slopeBandLo = 0;                     // minimum band key used as array index offset
    std::vector<float> slopeSmMin, slopeSmMax; // Gaussian-smoothed band [min,max] arrays

    // Donut fit: distance field from boundary + per-pixel outward direction
    std::vector<float> distField, outXField, outYField;
    int donutMaxDistPx = 1;  // raw BFS maxDist for pixel-space AA at ring edges


    if (m_fillFitToSel && effectiveHasSel && effectiveSel) {
        if ((m_fillType == FillType::Slope ||
             m_fillType == FillType::TwoSides ||
             m_fillType == FillType::Cylinder) && lenSq > 1e-6f) {
            // Per-perpendicular-band: each strip parallel to drag direction gets its own
            // [min, max] projection range so the gradient stretches to fill every row/strip.
            // Applies to both Slope (colour blend) and Cylinder (geometric profile).
            const float len = std::sqrt(lenSq);
            slopeNdx = float(delta.x()) / len;
            slopeNdy = float(delta.y()) / len;
            for (int y2 = 0; y2 < h; ++y2) {
                const QRgb*  sp2  = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(y2));
                const uchar* sel2 = effectiveSel->constScanLine(y2);
                for (int x2 = 0; x2 < w; ++x2) {
                    if (qAlpha(sp2[x2]) == 0 || sel2[x2] == 0) continue;
                    const float proj = slopeNdx * float(x2 - start.x()) + slopeNdy * float(y2 - start.y());
                    const float perp = -slopeNdy * float(x2 - start.x()) + slopeNdx * float(y2 - start.y());
                    const int band = int(std::floor(perp + 0.5f));
                    const auto it = slopeBandRange.find(band);
                    if (it == slopeBandRange.end())
                        slopeBandRange[band] = {proj, proj};
                    else {
                        it->second.first  = std::min(it->second.first,  proj);
                        it->second.second = std::max(it->second.second, proj);
                    }
                }
            }

            // Convert the band map to flat arrays and Gaussian-smooth them along the
            // perpendicular axis.  This removes abrupt t-value jumps at boundaries
            // between rows/strips of different widths.
            if (!slopeBandRange.empty()) {
                int bHi = INT_MIN;
                slopeBandLo = INT_MAX;
                for (const auto& kv : slopeBandRange) {
                    slopeBandLo = std::min(slopeBandLo, kv.first);
                    bHi         = std::max(bHi,         kv.first);
                }
                const int bCnt = bHi - slopeBandLo + 1;
                slopeSmMin.assign(bCnt,  1e9f);
                slopeSmMax.assign(bCnt, -1e9f);
                for (const auto& kv : slopeBandRange) {
                    slopeSmMin[kv.first - slopeBandLo] = kv.second.first;
                    slopeSmMax[kv.first - slopeBandLo] = kv.second.second;
                }
                // Nearest-neighbour fill for any gaps (non-contiguous selections)
                {
                    float fm = slopeSmMin[0], fx = slopeSmMax[0];
                    for (int i = 0; i < bCnt; ++i) {
                        if (slopeSmMin[i] < 9e8f) { fm = slopeSmMin[i]; fx = slopeSmMax[i]; }
                        else { slopeSmMin[i] = fm; slopeSmMax[i] = fx; }
                    }
                    fm = slopeSmMin[bCnt-1]; fx = slopeSmMax[bCnt-1];
                    for (int i = bCnt-1; i >= 0; --i) {
                        if (slopeSmMin[i] < 9e8f) { fm = slopeSmMin[i]; fx = slopeSmMax[i]; }
                        else { slopeSmMin[i] = fm; slopeSmMax[i] = fx; }
                    }
                }
                // 1D Gaussian blur: sigma proportional to span, capped to keep locality
                {
                    const float sigma = qBound(1.5f, float(bCnt) * 0.05f, 8.0f);
                    const int   r     = std::max(1, int(std::ceil(sigma * 3.0f)));
                    std::vector<float> kernel(2*r+1);
                    float ksum = 0.0f;
                    for (int i = -r; i <= r; ++i) {
                        kernel[i+r] = std::exp(-float(i*i) / (2.0f*sigma*sigma));
                        ksum += kernel[i+r];
                    }
                    for (auto& k : kernel) k /= ksum;
                    std::vector<float> tmpMin(bCnt), tmpMax(bCnt);
                    for (int i = 0; i < bCnt; ++i) {
                        float vm = 0.0f, vx = 0.0f;
                        for (int j = -r; j <= r; ++j) {
                            const int ii = qBound(0, i+j, bCnt-1);
                            vm += slopeSmMin[ii] * kernel[j+r];
                            vx += slopeSmMax[ii] * kernel[j+r];
                        }
                        tmpMin[i] = vm;
                        tmpMax[i] = vx;
                    }
                    slopeSmMin = std::move(tmpMin);
                    slopeSmMax = std::move(tmpMax);
                }
            }

        } else if (m_fillType == FillType::Prism) {
            // Bounding box of selected pixels defines the prism rectangle
            float minX = float(w), minY = float(h), maxX = 0.0f, maxY = 0.0f;
            for (int y2 = 0; y2 < h; ++y2) {
                const QRgb*  sp2  = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(y2));
                const uchar* sel2 = effectiveSel->constScanLine(y2);
                for (int x2 = 0; x2 < w; ++x2) {
                    if (qAlpha(sp2[x2]) == 0 || sel2[x2] == 0) continue;
                    if (float(x2) < minX) minX = float(x2);
                    if (float(y2) < minY) minY = float(y2);
                    if (float(x2) > maxX) maxX = float(x2);
                    if (float(y2) > maxY) maxY = float(y2);
                }
            }
            if (maxX > minX && maxY > minY) {
                prismFitX0 = minX; prismFitY0 = minY;
                prismFitX1 = maxX; prismFitY1 = maxY;
            }

        } else if (m_fillType == FillType::Donut) {
            // BFS distance field: boundary pixels → 0, interior peak → 1.
            // The ring occupies the boundary region; pixels beyond the ring thickness
            // receive a flat normal (empty hole).
            distField = buildDistField(m_sprite, *effectiveSel, w, h, &donutMaxDistPx);
            {
                const float sigma = qBound(1.5f, float(qMin(w, h)) * 0.04f, 8.0f);
                gaussianBlurSep(distField, w, h, sigma);
                float dfMax = 0.0f;
                for (float v : distField) dfMax = qMax(dfMax, v);
                if (dfMax > 1e-6f)
                    for (float& v : distField) v /= dfMax;
            }
            // Per-pixel outward direction: gradient of distField points toward interior;
            // negate to get the direction toward the boundary (outward).
            outXField.assign(w * h, 0.0f);
            outYField.assign(w * h, 0.0f);
            for (int yy = 0; yy < h; ++yy) {
                for (int xx = 0; xx < w; ++xx) {
                    const float gX = (distField[yy*w + qMin(xx+1, w-1)]
                                    - distField[yy*w + qMax(xx-1, 0)]) * 0.5f;
                    const float gY = (distField[qMin(yy+1, h-1)*w + xx]
                                    - distField[qMax(yy-1, 0)*w + xx]) * 0.5f;
                    const float gLen = std::sqrt(gX*gX + gY*gY);
                    if (gLen > 1e-6f) {
                        outXField[yy*w + xx] = -gX / gLen;  // toward boundary
                        outYField[yy*w + xx] = -gY / gLen;
                    }
                }
            }

        } else if (m_fillType == FillType::Dome) {
            // Dome fit: balloon effect that deforms to the selection shape.
            //
            // Blurred un-normalized gradient of BFS field: opposing interior gradients cancel
            // at the medial axis → magnitude → 0 (flat top); near boundary → magnitude high
            // (steep rim). No normalization = no direction flips = no peaks.
            //
            // Boundary correction: blurring across the selection edge averages selected pixels'
            // gradients with exterior 0s, artificially halving the boundary magnitude (halo).
            // Fix: blur a mask field (1=selected, 0=outside) with the same kernel, then divide
            // gradient by mask. The ratio cancels the edge attenuation.
            distField = buildDistField(m_sprite, *effectiveSel, w, h, &donutMaxDistPx);

            // Outward gradient of the RAW BFS field — only for selected+opaque pixels.
            // Non-selected pixels stay 0 so they don't inject incorrect inward gradients.
            outXField.assign(w * h, 0.0f);
            outYField.assign(w * h, 0.0f);
            std::vector<float> maskField(w * h, 0.0f);
            for (int yy = 0; yy < h; ++yy) {
                const QRgb*  sp2  = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
                const uchar* sel2 = effectiveSel->constScanLine(yy);
                for (int xx = 0; xx < w; ++xx) {
                    if (qAlpha(sp2[xx]) == 0 || sel2[xx] == 0) continue;
                    const float gX = (distField[yy*w + qMin(xx+1, w-1)]
                                    - distField[yy*w + qMax(xx-1, 0)]) * 0.5f;
                    const float gY = (distField[qMin(yy+1, h-1)*w + xx]
                                    - distField[qMax(yy-1, 0)*w + xx]) * 0.5f;
                    outXField[yy*w + xx] = -gX;  // outward (toward boundary)
                    outYField[yy*w + xx] = -gY;
                    maskField[yy*w + xx] = 1.0f;
                }
            }

            // Blur gradient + mask with the same large sigma.
            {
                const float sigma = qBound(3.0f, float(donutMaxDistPx) * 0.6f, 80.0f);
                approxGaussianLarge(outXField, w, h, sigma);
                approxGaussianLarge(outYField, w, h, sigma);
                approxGaussianLarge(maskField, w, h, sigma);
            }

            // Boundary correction: divide gradient by blurred mask.
            // Pixels where mask > threshold get corrected; exterior pixels become 0.
            for (int i = 0; i < w * h; ++i) {
                if (maskField[i] > 0.01f) {
                    outXField[i] /= maskField[i];
                    outYField[i] /= maskField[i];
                } else {
                    outXField[i] = 0.0f;
                    outYField[i] = 0.0f;
                }
            }

            // Scale so maximum corrected-gradient magnitude = 1.
            {
                float maxMag = 1e-6f;
                for (int i = 0; i < w * h; ++i)
                    maxMag = qMax(maxMag, std::sqrt(outXField[i]*outXField[i]
                                                  + outYField[i]*outYField[i]));
                const float inv = 1.0f / maxMag;
                for (int i = 0; i < w * h; ++i) { outXField[i] *= inv; outYField[i] *= inv; }
            }

            // Normalize BFS field to [0,1] (with light smoothing) for boundary AA.
            {
                const float sigmaDf = qBound(1.5f, float(donutMaxDistPx) * 0.04f, 8.0f);
                gaussianBlurSep(distField, w, h, sigmaDf);
                float dfMax = 0.0f;
                for (float v : distField) dfMax = qMax(dfMax, v);
                if (dfMax > 1e-6f)
                    for (float& v : distField) v /= dfMax;
            }

        } else if (m_fillType == FillType::Cone) {
            // Centroid of selection as shape centre; max distance → fitRadius.
            double sumX = 0, sumY = 0;
            int count = 0;
            for (int y2 = 0; y2 < h; ++y2) {
                const QRgb*  sp2  = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(y2));
                const uchar* sel2 = effectiveSel->constScanLine(y2);
                for (int x2 = 0; x2 < w; ++x2) {
                    if (qAlpha(sp2[x2]) == 0 || sel2[x2] == 0) continue;
                    sumX += x2; sumY += y2; ++count;
                }
            }
            if (count > 0) {
                centX = float(sumX / count);
                centY = float(sumY / count);
                float maxD = 0.0f;
                for (int y2 = 0; y2 < h; ++y2) {
                    const QRgb*  sp2  = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(y2));
                    const uchar* sel2 = effectiveSel->constScanLine(y2);
                    for (int x2 = 0; x2 < w; ++x2) {
                        if (qAlpha(sp2[x2]) == 0 || sel2[x2] == 0) continue;
                        const float ddx = float(x2) - centX;
                        const float ddy = float(y2) - centY;
                        maxD = qMax(maxD, std::sqrt(ddx*ddx + ddy*ddy));
                    }
                }
                if (maxD > 1e-3f) fitRadius = maxD;
            }
        }
    }

    // Emboss: always compute BFS + outward direction (independent of fit-to-selection).
    // Works with or without a selection; without one uses the sprite's alpha boundary.
    if (m_fillType == FillType::Emboss) {
        // Build BFS selection mask: use active selection or fall back to sprite alpha
        QImage syntheticSel;
        const QImage* bfsMask = effectiveSel;
        if (!bfsMask) {
            syntheticSel = QImage(w, h, QImage::Format_Grayscale8);
            for (int y2 = 0; y2 < h; ++y2) {
                const QRgb* sp2 = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(y2));
                uchar* s2 = syntheticSel.scanLine(y2);
                for (int x2 = 0; x2 < w; ++x2)
                    s2[x2] = qAlpha(sp2[x2]) > 0 ? 255 : 0;
            }
            bfsMask = &syntheticSel;
        }
        distField = buildDistField(m_sprite, *bfsMask, w, h, &donutMaxDistPx);

        // Light smoothing of the BFS field controls corner roundness.
        // Round corners: sigma≈1.5 blends direction near convex corners.
        // Sharp corners: sigma≈0 (skip blur) keeps directions hard-edged.
        {
            const float sigma = (m_embossCorner == 0) ? 1.5f : 0.0f;
            if (sigma > 0.5f) gaussianBlurSep(distField, w, h, sigma);
        }

        // Outward direction: raw gradient of the BFS field, blurred to remove the
        // 4-connectivity staircase, then normalized. Per-pixel normalization before
        // blurring locks in the staircase directions and causes banding artifacts.
        outXField.assign(w * h, 0.0f);
        outYField.assign(w * h, 0.0f);
        {
            std::vector<float> maskField(w * h, 0.0f);
            for (int yy = 0; yy < h; ++yy) {
                const uchar* bfsRow = bfsMask->constScanLine(yy);
                const QRgb*  spRow  = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
                for (int xx = 0; xx < w; ++xx) {
                    if (qAlpha(spRow[xx]) == 0 || bfsRow[xx] == 0) continue;
                    maskField[yy*w + xx] = 1.0f;
                    const float gX = (distField[yy*w + qMin(xx+1, w-1)]
                                    - distField[yy*w + qMax(xx-1, 0)]) * 0.5f;
                    const float gY = (distField[qMin(yy+1, h-1)*w + xx]
                                    - distField[qMax(yy-1, 0)*w + xx]) * 0.5f;
                    outXField[yy*w + xx] = -gX;  // toward boundary, un-normalized
                    outYField[yy*w + xx] = -gY;
                }
            }
            // Blur to smooth BFS staircase artifacts; a small sigma is enough
            // since the staircase period is ~1px, but scale with bevel width
            // so corner directions stay accurate at larger scales.
            const float dirSigma = qBound(2.0f, float(donutMaxDistPx) * 0.04f, 8.0f);
            approxGaussianLarge(outXField,  w, h, dirSigma);
            approxGaussianLarge(outYField,  w, h, dirSigma);
            approxGaussianLarge(maskField,  w, h, dirSigma);
            // Boundary correction + normalize
            for (int i = 0; i < w * h; ++i) {
                if (maskField[i] > 0.01f) {
                    outXField[i] /= maskField[i];
                    outYField[i] /= maskField[i];
                } else {
                    outXField[i] = outYField[i] = 0.0f;
                    continue;
                }
                const float len = std::sqrt(outXField[i]*outXField[i] + outYField[i]*outYField[i]);
                if (len > 1e-6f) { outXField[i] /= len; outYField[i] /= len; }
                else              { outXField[i] = outYField[i] = 0.0f; }
            }
        }

        // Normalize distField to [0,1] (needed for pixel-distance lookup in per-pixel).
        {
            float dfMax = 0.0f;
            for (float v : distField) dfMax = qMax(dfMax, v);
            if (dfMax > 1e-6f)
                for (float& v : distField) v /= dfMax;
        }
    }

    // Height field → normals via Sobel gradient.
    if (m_fillType == FillType::HeightField) {
        // Extract chosen channel as float [0,1]
        std::vector<float> hField(w * h, 0.0f);
        for (int yy = 0; yy < h; ++yy) {
            const QRgb* spRow = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
            for (int xx = 0; xx < w; ++xx) {
                const QRgb px = spRow[xx];
                float hv;
                switch (m_heightFieldSource) {
                    case 1:  hv = qRed(px)   / 255.0f; break;
                    case 2:  hv = qGreen(px) / 255.0f; break;
                    case 3:  hv = qBlue(px)  / 255.0f; break;
                    case 4:  hv = qAlpha(px) / 255.0f; break;
                    default: hv = (0.299f*qRed(px) + 0.587f*qGreen(px) + 0.114f*qBlue(px)) / 255.0f; break;
                }
                hField[yy*w + xx] = hv;
            }
        }
        // Optional pre-blur to smooth the height field before gradient
        if (m_heightFieldBlur > 0.5f)
            approxGaussianLarge(hField, w, h, m_heightFieldBlur);

        // Sobel gradient → scaled normal XY (Z is implicit 1, normalized per-pixel)
        outXField.assign(w * h, 0.0f);
        outYField.assign(w * h, 0.0f);
        for (int yy = 0; yy < h; ++yy) {
            const QRgb* spRow = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
            for (int xx = 0; xx < w; ++xx) {
                if (qAlpha(spRow[xx]) == 0) continue;
                const int x0 = qMax(xx-1, 0), x2 = qMin(xx+1, w-1);
                const int y0 = qMax(yy-1, 0), y2 = qMin(yy+1, h-1);
                const float gx = (
                    -hField[y0*w+x0] + hField[y0*w+x2]
                    - 2.0f*hField[yy*w+x0] + 2.0f*hField[yy*w+x2]
                    - hField[y2*w+x0] + hField[y2*w+x2]
                ) * (1.0f/8.0f);
                const float gy = (
                    -hField[y0*w+x0] - 2.0f*hField[y0*w+xx] - hField[y0*w+x2]
                    + hField[y2*w+x0] + 2.0f*hField[y2*w+xx] + hField[y2*w+x2]
                ) * (1.0f/8.0f);
                outXField[yy*w+xx] = -gx * m_heightFieldScale;
                outYField[yy*w+xx] = -gy * m_heightFieldScale;
            }
        }
    }

    // Edge trace: Sobel gradient direction painted directly as normals at detected edges.
    // Below-threshold pixels produce zero gradient → flat normal in per-pixel case.
    if (m_fillType == FillType::EdgeTrace) {
        // Extract and optionally blur source channel
        std::vector<float> srcField(w * h, 0.0f);
        for (int yy = 0; yy < h; ++yy) {
            const QRgb* spRow = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
            for (int xx = 0; xx < w; ++xx) {
                if (qAlpha(spRow[xx]) == 0) continue;
                const QRgb px = spRow[xx];
                float v;
                switch (m_edgeTraceSource) {
                    case 1:  v = qRed(px)   / 255.0f; break;
                    case 2:  v = qGreen(px) / 255.0f; break;
                    case 3:  v = qBlue(px)  / 255.0f; break;
                    default: v = (0.299f*qRed(px) + 0.587f*qGreen(px) + 0.114f*qBlue(px)) / 255.0f; break;
                }
                srcField[yy*w+xx] = v;
            }
        }
        if (m_edgeTraceBlur > 0.5f)
            approxGaussianLarge(srcField, w, h, m_edgeTraceBlur);

        // Sobel pass 1: find magnitude max for normalization
        float magMax = 0.0f;
        std::vector<float> gxField(w * h, 0.0f);
        std::vector<float> gyField(w * h, 0.0f);
        for (int yy = 0; yy < h; ++yy) {
            const QRgb* spRow = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
            for (int xx = 0; xx < w; ++xx) {
                if (qAlpha(spRow[xx]) == 0) continue;
                const int x0 = qMax(xx-1,0), x2 = qMin(xx+1,w-1);
                const int y0 = qMax(yy-1,0), y2 = qMin(yy+1,h-1);
                gxField[yy*w+xx] = (
                    -srcField[y0*w+x0] + srcField[y0*w+x2]
                    -2.0f*srcField[yy*w+x0] + 2.0f*srcField[yy*w+x2]
                    -srcField[y2*w+x0] + srcField[y2*w+x2]) * (1.0f/8.0f);
                gyField[yy*w+xx] = (
                    -srcField[y0*w+x0] - 2.0f*srcField[y0*w+xx] - srcField[y0*w+x2]
                    +srcField[y2*w+x0] + 2.0f*srcField[y2*w+xx] + srcField[y2*w+x2]) * (1.0f/8.0f);
                const float mag = std::sqrt(gxField[yy*w+xx]*gxField[yy*w+xx]
                                          + gyField[yy*w+xx]*gyField[yy*w+xx]);
                magMax = qMax(magMax, mag);
            }
        }

        // Threshold and store normalized gradient; zero out below-threshold and non-selected pixels
        outXField.assign(w * h, 0.0f);
        outYField.assign(w * h, 0.0f);
        std::vector<float> maskField(w * h, 0.0f);
        if (magMax > 1e-6f) {
            for (int yy = 0; yy < h; ++yy) {
                const QRgb*  spRow  = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
                const uchar* selRow = effectiveSel ? effectiveSel->constScanLine(yy) : nullptr;
                for (int xx = 0; xx < w; ++xx) {
                    if (qAlpha(spRow[xx]) == 0) continue;
                    if (selRow && selRow[xx] == 0) continue;
                    const int i = yy*w+xx;
                    const float normMag = std::sqrt(gxField[i]*gxField[i] + gyField[i]*gyField[i]) / magMax;
                    if (normMag < m_edgeTraceThreshold) continue;
                    maskField[i]  = 1.0f;
                    outXField[i]  = gxField[i] / magMax;
                    outYField[i]  = gyField[i] / magMax;
                }
            }
        }

        // Optional spread: blur the gradient field so edge normals bleed outward
        if (m_edgeTraceSpread > 0.5f) {
            approxGaussianLarge(outXField,  w, h, m_edgeTraceSpread);
            approxGaussianLarge(outYField,  w, h, m_edgeTraceSpread);
            approxGaussianLarge(maskField,  w, h, m_edgeTraceSpread);
            for (int i = 0; i < w*h; ++i) {
                if (maskField[i] > 0.01f) {
                    outXField[i] /= maskField[i];
                    outYField[i] /= maskField[i];
                } else {
                    outXField[i] = outYField[i] = 0.0f;
                }
            }
        }
        // distField unused; outXField/outYField carry the normalized gradient components
    }

    // Edge emboss: BFS from high-gradient (edge) pixels → bevel directed away from detected edges.
    if (m_fillType == FillType::EdgeEmboss) {
        // Extract and optionally blur the source channel
        std::vector<float> srcField(w * h, 0.0f);
        for (int yy = 0; yy < h; ++yy) {
            const QRgb* spRow = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
            for (int xx = 0; xx < w; ++xx) {
                if (qAlpha(spRow[xx]) == 0) continue;
                const QRgb px = spRow[xx];
                float v;
                switch (m_edgeEmbossSource) {
                    case 1:  v = qRed(px)   / 255.0f; break;
                    case 2:  v = qGreen(px) / 255.0f; break;
                    case 3:  v = qBlue(px)  / 255.0f; break;
                    default: v = (0.299f*qRed(px) + 0.587f*qGreen(px) + 0.114f*qBlue(px)) / 255.0f; break;
                }
                srcField[yy*w+xx] = v;
            }
        }
        if (m_edgeEmbossBlur > 0.5f)
            approxGaussianLarge(srcField, w, h, m_edgeEmbossBlur);

        // Sobel magnitude, normalized to [0,1]
        std::vector<float> edgeMag(w * h, 0.0f);
        {
            float magMax = 0.0f;
            for (int yy = 0; yy < h; ++yy) {
                const QRgb* spRow = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
                for (int xx = 0; xx < w; ++xx) {
                    if (qAlpha(spRow[xx]) == 0) continue;
                    const int x0 = qMax(xx-1,0), x2 = qMin(xx+1,w-1);
                    const int y0 = qMax(yy-1,0), y2 = qMin(yy+1,h-1);
                    const float gx = (
                        -srcField[y0*w+x0] + srcField[y0*w+x2]
                        -2.0f*srcField[yy*w+x0] + 2.0f*srcField[yy*w+x2]
                        -srcField[y2*w+x0] + srcField[y2*w+x2]) * (1.0f/8.0f);
                    const float gy = (
                        -srcField[y0*w+x0] - 2.0f*srcField[y0*w+xx] - srcField[y0*w+x2]
                        +srcField[y2*w+x0] + 2.0f*srcField[y2*w+xx] + srcField[y2*w+x2]) * (1.0f/8.0f);
                    edgeMag[yy*w+xx] = std::sqrt(gx*gx + gy*gy);
                    magMax = qMax(magMax, edgeMag[yy*w+xx]);
                }
            }
            if (magMax > 1e-6f)
                for (float& v : edgeMag) v /= magMax;
        }

        // BFS from edge pixels (those above threshold) within opaque/selected region.
        // Unreachable pixels get sentinel value (> bevel width) so they stay flat.
        const float sentinel = m_edgeEmbossWidth + 1.0f;
        distField.assign(w * h, sentinel);
        std::vector<int> bfsDist(w * h, -1);
        {
            std::queue<std::pair<int,int>> bfsQ;
            for (int yy = 0; yy < h; ++yy) {
                const QRgb* spRow = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
                const uchar* selRow = effectiveSel ? effectiveSel->constScanLine(yy) : nullptr;
                for (int xx = 0; xx < w; ++xx) {
                    if (qAlpha(spRow[xx]) == 0) continue;
                    if (selRow && selRow[xx] == 0) continue;
                    if (edgeMag[yy*w+xx] >= m_edgeEmbossThreshold) {
                        bfsDist[yy*w+xx] = 0;
                        distField[yy*w+xx] = 0.0f;
                        bfsQ.push({xx, yy});
                    }
                }
            }
            donutMaxDistPx = 0;
            constexpr int kDx[] = {1,-1,0,0};
            constexpr int kDy[] = {0,0,1,-1};
            while (!bfsQ.empty()) {
                auto [cx, cy] = bfsQ.front(); bfsQ.pop();
                const int cd = bfsDist[cy*w+cx];
                for (int d = 0; d < 4; ++d) {
                    const int nx = cx+kDx[d], ny = cy+kDy[d];
                    if (nx<0||nx>=w||ny<0||ny>=h) continue;
                    if (bfsDist[ny*w+nx] >= 0) continue;
                    const QRgb* spRow = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(ny));
                    if (qAlpha(spRow[nx]) == 0) continue;
                    if (effectiveSel && effectiveSel->constScanLine(ny)[nx] == 0) continue;
                    bfsDist[ny*w+nx] = cd + 1;
                    distField[ny*w+nx] = float(bfsDist[ny*w+nx]);
                    donutMaxDistPx = qMax(donutMaxDistPx, bfsDist[ny*w+nx]);
                    bfsQ.push({nx, ny});
                }
            }
        }

        // Optional corner rounding
        if (m_edgeEmbossCorner == 0) gaussianBlurSep(distField, w, h, 1.5f);

        // Direction field: gradient of distField (clamped at unreachable neighbors),
        // blurred to remove BFS staircase, then normalized.
        outXField.assign(w * h, 0.0f);
        outYField.assign(w * h, 0.0f);
        {
            std::vector<float> maskField(w * h, 0.0f);
            for (int yy = 0; yy < h; ++yy) {
                const QRgb* spRow = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
                const uchar* selRow = effectiveSel ? effectiveSel->constScanLine(yy) : nullptr;
                for (int xx = 0; xx < w; ++xx) {
                    if (qAlpha(spRow[xx]) == 0) continue;
                    if (selRow && selRow[xx] == 0) continue;
                    if (bfsDist[yy*w+xx] < 0) continue;  // unreachable
                    maskField[yy*w+xx] = 1.0f;
                    // Clamp neighbors at unreachable pixels to avoid sentinel-driven gradients
                    const int xm = qMax(xx-1,0), xp = qMin(xx+1,w-1);
                    const int ym = qMax(yy-1,0), yp = qMin(yy+1,h-1);
                    const float dC = distField[yy*w+xx];
                    const float dL = (bfsDist[yy*w+xm] >= 0) ? distField[yy*w+xm] : dC;
                    const float dR = (bfsDist[yy*w+xp] >= 0) ? distField[yy*w+xp] : dC;
                    const float dU = (bfsDist[ym*w+xx] >= 0) ? distField[ym*w+xx] : dC;
                    const float dD = (bfsDist[yp*w+xx] >= 0) ? distField[yp*w+xx] : dC;
                    outXField[yy*w+xx] = -(dR - dL) * 0.5f;
                    outYField[yy*w+xx] = -(dD - dU) * 0.5f;
                }
            }
            const float dirSigma = qBound(2.0f, float(donutMaxDistPx) * 0.04f, 8.0f);
            approxGaussianLarge(outXField, w, h, dirSigma);
            approxGaussianLarge(outYField, w, h, dirSigma);
            approxGaussianLarge(maskField, w, h, dirSigma);
            for (int i = 0; i < w*h; ++i) {
                if (maskField[i] > 0.01f) {
                    outXField[i] /= maskField[i];
                    outYField[i] /= maskField[i];
                } else {
                    outXField[i] = outYField[i] = 0.0f;
                    continue;
                }
                const float len = std::sqrt(outXField[i]*outXField[i] + outYField[i]*outYField[i]);
                if (len > 1e-6f) { outXField[i] /= len; outYField[i] /= len; }
                else              { outXField[i] = outYField[i] = 0.0f; }
            }
        }
        // distField is already in pixels; no normalization needed.
        // donutMaxDistPx is set above for dirSigma scaling only.
    }

    // Auto Normal: additive blend of three components stored in outXField/outYField.
    //   Pillow  — BFS dome from alpha boundary × (1-distNorm): gives volume.
    //   Detail  — luminance Sobel height-field: gives surface texture.
    //   Panel   — concave EdgeEmboss at detected luminance edges: gives grooves.
    // Per-pixel: N = normalize(outX, outY, 1) — same height-field convention as HeightField.
    if (m_fillType == FillType::AutoNormal) {
        // ── Extract + blur source channel ────────────────────────────────────
        std::vector<float> srcField(w * h, 0.0f);
        for (int yy = 0; yy < h; ++yy) {
            const QRgb* spRow = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
            for (int xx = 0; xx < w; ++xx) {
                if (qAlpha(spRow[xx]) == 0) continue;
                const QRgb px = spRow[xx];
                float v;
                switch (m_autoSource) {
                    case 1:  v = qRed(px)   / 255.0f; break;
                    case 2:  v = qGreen(px) / 255.0f; break;
                    case 3:  v = qBlue(px)  / 255.0f; break;
                    default: v = (0.299f*qRed(px) + 0.587f*qGreen(px) + 0.114f*qBlue(px)) / 255.0f; break;
                }
                srcField[yy*w + xx] = v;
            }
        }
        if (m_autoSmooth > 0.5f)
            approxGaussianLarge(srcField, w, h, m_autoSmooth);

        // ── Component 1: Pillow — BFS from alpha boundary ─────────────────
        // Build synthetic selection from alpha so BFS always covers the full sprite.
        QImage autoSel(w, h, QImage::Format_Grayscale8);
        for (int yy = 0; yy < h; ++yy) {
            const QRgb* sp2 = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
            uchar* s2 = autoSel.scanLine(yy);
            for (int xx = 0; xx < w; ++xx)
                s2[xx] = qAlpha(sp2[xx]) > 0 ? 255 : 0;
        }
        // Use active selection if present (intersect: opaque AND selected)
        if (effectiveSel) {
            for (int yy = 0; yy < h; ++yy) {
                uchar*       s2  = autoSel.scanLine(yy);
                const uchar* sel = effectiveSel->constScanLine(yy);
                for (int xx = 0; xx < w; ++xx)
                    if (sel[xx] == 0) s2[xx] = 0;
            }
        }
        // Pillow uses the Dome approach: very large blur sigma so opposing gradients at
        // the medial axis of narrow protrusions (knees, fingertips) cancel each other.
        // The resulting gradient magnitude is naturally high near the boundary (steep rim)
        // and near-zero at the center/medial axis (flat top), so no per-pixel normalization
        // is needed — the magnitude encodes the dome fade automatically.
        int pillowMaxDist = 1;
        std::vector<float> pillowDist = buildDistField(m_sprite, autoSel, w, h, &pillowMaxDist);
        std::vector<float> pillowDirX(w * h, 0.0f);
        std::vector<float> pillowDirY(w * h, 0.0f);
        {
            std::vector<float> maskF(w * h, 0.0f);
            for (int yy = 0; yy < h; ++yy) {
                const uchar* aRow  = autoSel.constScanLine(yy);
                const QRgb*  spRow = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
                for (int xx = 0; xx < w; ++xx) {
                    if (qAlpha(spRow[xx]) == 0 || aRow[xx] == 0) continue;
                    maskF[yy*w + xx] = 1.0f;
                    const float gX = (pillowDist[yy*w + qMin(xx+1, w-1)]
                                    - pillowDist[yy*w + qMax(xx-1, 0)]) * 0.5f;
                    const float gY = (pillowDist[qMin(yy+1, h-1)*w + xx]
                                    - pillowDist[qMax(yy-1, 0)*w + xx]) * 0.5f;
                    pillowDirX[yy*w + xx] = -gX;
                    pillowDirY[yy*w + xx] = -gY;
                }
            }
            // Large sigma — same as Dome — allows opposing wavefronts to cancel at the
            // medial axis, eliminating cone/needle artifacts at narrow features.
            const float ds = qBound(3.0f, float(pillowMaxDist) * 0.5f, 80.0f);
            approxGaussianLarge(pillowDirX, w, h, ds);
            approxGaussianLarge(pillowDirY, w, h, ds);
            approxGaussianLarge(maskF, w, h, ds);
            // Boundary correction only — preserve magnitude (encodes the dome fade).
            for (int i = 0; i < w * h; ++i) {
                if (maskF[i] > 0.01f) {
                    pillowDirX[i] /= maskF[i];
                    pillowDirY[i] /= maskF[i];
                } else {
                    pillowDirX[i] = pillowDirY[i] = 0.0f;
                }
            }
            // Scale so maximum gradient magnitude = 1.
            float maxMag = 1e-6f;
            for (int i = 0; i < w * h; ++i)
                maxMag = qMax(maxMag, std::sqrt(pillowDirX[i]*pillowDirX[i]
                                              + pillowDirY[i]*pillowDirY[i]));
            const float inv = 1.0f / maxMag;
            for (int i = 0; i < w * h; ++i) { pillowDirX[i] *= inv; pillowDirY[i] *= inv; }
        }

        // ── Component 2: Detail — Sobel height-field on source channel ─────
        std::vector<float> detailGX(w * h, 0.0f);
        std::vector<float> detailGY(w * h, 0.0f);
        for (int yy = 0; yy < h; ++yy) {
            const QRgb* spRow = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
            for (int xx = 0; xx < w; ++xx) {
                if (qAlpha(spRow[xx]) == 0) continue;
                const int x0 = qMax(xx-1, 0), x2 = qMin(xx+1, w-1);
                const int y0 = qMax(yy-1, 0), y2 = qMin(yy+1, h-1);
                const float gx = (
                    -srcField[y0*w+x0] + srcField[y0*w+x2]
                    - 2.0f*srcField[yy*w+x0] + 2.0f*srcField[yy*w+x2]
                    - srcField[y2*w+x0] + srcField[y2*w+x2]) * (1.0f/8.0f);
                const float gy = (
                    -srcField[y0*w+x0] - 2.0f*srcField[y0*w+xx] - srcField[y0*w+x2]
                    + srcField[y2*w+x0] + 2.0f*srcField[y2*w+xx] + srcField[y2*w+x2]) * (1.0f/8.0f);
                detailGX[yy*w+xx] = -gx;
                detailGY[yy*w+xx] = -gy;
            }
        }

        // ── Component 3: Panel — concave groove at detected edges ──────────
        // Sobel magnitude → threshold → BFS from edge pixels → symmetric ridge bevel.
        std::vector<float> panelDirX(w * h, 0.0f);
        std::vector<float> panelDirY(w * h, 0.0f);
        std::vector<float> panelProfile(w * h, 0.0f);
        if (m_autoPanelStrength > 1e-6f) {
            // Normalized Sobel magnitude
            float magMax = 0.0f;
            std::vector<float> gxF(w * h, 0.0f);
            std::vector<float> gyF(w * h, 0.0f);
            for (int yy = 0; yy < h; ++yy) {
                const QRgb* spRow = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
                for (int xx = 0; xx < w; ++xx) {
                    if (qAlpha(spRow[xx]) == 0) continue;
                    const int x0 = qMax(xx-1, 0), x2 = qMin(xx+1, w-1);
                    const int y0 = qMax(yy-1, 0), y2 = qMin(yy+1, h-1);
                    gxF[yy*w+xx] = (
                        -srcField[y0*w+x0] + srcField[y0*w+x2]
                        -2.0f*srcField[yy*w+x0] + 2.0f*srcField[yy*w+x2]
                        -srcField[y2*w+x0] + srcField[y2*w+x2]) * (1.0f/8.0f);
                    gyF[yy*w+xx] = (
                        -srcField[y0*w+x0] - 2.0f*srcField[y0*w+xx] - srcField[y0*w+x2]
                        +srcField[y2*w+x0] + 2.0f*srcField[y2*w+xx] + srcField[y2*w+x2]) * (1.0f/8.0f);
                    const float mag = std::sqrt(gxF[yy*w+xx]*gxF[yy*w+xx] + gyF[yy*w+xx]*gyF[yy*w+xx]);
                    magMax = qMax(magMax, mag);
                }
            }
            // BFS from threshold-exceeded pixels
            const float sentinel = m_autoPanelWidth + 1.0f;
            std::vector<float> pDist(w * h, -1.0f);
            std::queue<int> bfsQ;
            if (magMax > 1e-6f) {
                for (int yy = 0; yy < h; ++yy) {
                    const QRgb*  spRow  = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
                    const uchar* selRow = effectiveSel ? effectiveSel->constScanLine(yy) : nullptr;
                    for (int xx = 0; xx < w; ++xx) {
                        if (qAlpha(spRow[xx]) == 0) continue;
                        if (selRow && selRow[xx] == 0) continue;
                        const float normMag = std::sqrt(gxF[yy*w+xx]*gxF[yy*w+xx] + gyF[yy*w+xx]*gyF[yy*w+xx]) / magMax;
                        if (normMag >= m_autoPanelThreshold) {
                            pDist[yy*w+xx] = 0.0f;
                            bfsQ.push(yy*w+xx);
                        }
                    }
                }
            }
            // BFS propagation
            std::vector<float> panelDistPx(w * h, sentinel);
            while (!bfsQ.empty()) {
                const int idx = bfsQ.front(); bfsQ.pop();
                const int yy = idx / w, xx = idx % w;
                panelDistPx[idx] = pDist[idx];
                const int nbrs[4][2] = {{xx-1,yy},{xx+1,yy},{xx,yy-1},{xx,yy+1}};
                for (auto& nb : nbrs) {
                    const int nx2 = nb[0], ny2 = nb[1];
                    if (nx2 < 0 || nx2 >= w || ny2 < 0 || ny2 >= h) continue;
                    const QRgb* spRow2 = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(ny2));
                    if (qAlpha(spRow2[nx2]) == 0) continue;
                    const int ni = ny2*w + nx2;
                    const float nd = pDist[idx] + 1.0f;
                    if (pDist[ni] < 0.0f && nd < m_autoPanelWidth) {
                        pDist[ni] = nd;
                        bfsQ.push(ni);
                    }
                }
            }
            // Outward direction from panel edges: gradient → blur → boundary correction → normalize
            std::vector<float> pmaskF(w * h, 0.0f);
            for (int yy = 0; yy < h; ++yy) {
                const QRgb* spRow = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(yy));
                for (int xx = 0; xx < w; ++xx) {
                    if (qAlpha(spRow[xx]) == 0) continue;
                    const float dC = panelDistPx[yy*w+xx];
                    if (dC >= m_autoPanelWidth) continue;
                    pmaskF[yy*w+xx] = 1.0f;
                    const int xm = qMax(xx-1, 0), xp = qMin(xx+1, w-1);
                    const int ym = qMax(yy-1, 0), yp = qMin(yy+1, h-1);
                    const float dL = (pDist[yy*w+xm] >= 0) ? panelDistPx[yy*w+xm] : dC;
                    const float dR = (pDist[yy*w+xp] >= 0) ? panelDistPx[yy*w+xp] : dC;
                    const float dU = (pDist[ym*w+xx] >= 0) ? panelDistPx[ym*w+xx] : dC;
                    const float dD = (pDist[yp*w+xx] >= 0) ? panelDistPx[yp*w+xx] : dC;
                    const float gX = (dR - dL) * 0.5f;
                    const float gY = (dD - dU) * 0.5f;
                    panelDirX[yy*w+xx] = -gX;
                    panelDirY[yy*w+xx] = -gY;
                }
            }
            const float panelDirSigma = qBound(2.0f, m_autoPanelWidth * 0.04f, 8.0f);
            approxGaussianLarge(panelDirX, w, h, panelDirSigma);
            approxGaussianLarge(panelDirY, w, h, panelDirSigma);
            approxGaussianLarge(pmaskF, w, h, panelDirSigma);
            for (int i = 0; i < w * h; ++i) {
                if (pmaskF[i] > 0.01f) {
                    panelDirX[i] /= pmaskF[i];
                    panelDirY[i] /= pmaskF[i];
                } else {
                    panelDirX[i] = panelDirY[i] = 0.0f;
                    continue;
                }
                const float len = std::sqrt(panelDirX[i]*panelDirX[i] + panelDirY[i]*panelDirY[i]);
                if (len > 1e-6f) { panelDirX[i] /= len; panelDirY[i] /= len; }
                else              { panelDirX[i] = panelDirY[i] = 0.0f; }
            }
            // Panel profile: symmetric smoothstep ridge (t=0 at edge, t=1 at bevel inner end)
            for (int i = 0; i < w * h; ++i) {
                const float d = panelDistPx[i];
                if (d >= m_autoPanelWidth || d < 0.0f) { panelProfile[i] = 0.0f; continue; }
                const float t = (m_autoPanelWidth > 1e-3f) ? d / m_autoPanelWidth : 0.0f;
                // Symmetric ridge: q = t/0.5 for t<=0.5, (1-t)/0.5 for t>0.5
                const float q = (t <= 0.5f) ? t * 2.0f : (1.0f - t) * 2.0f;
                panelProfile[i] = q * q * (3.0f - 2.0f * q); // smoothstep
            }
        }

        // ── Combine all three components into outXField/outYField ──────────
        // Pillow gradient magnitude already encodes the dome fade (high at rim, ~0 at center),
        // so no explicit (1 - distNorm) multiplier is needed.
        outXField.assign(w * h, 0.0f);
        outYField.assign(w * h, 0.0f);
        const bool invertDetail = m_fillInvert;
        for (int i = 0; i < w * h; ++i) {
            outXField[i] = pillowDirX[i] * m_autoPillowStrength
                         + detailGX[i] * m_autoDetailStrength * (invertDetail ? -1.0f : 1.0f)
                         - panelDirX[i] * panelProfile[i] * m_autoPanelStrength;
            outYField[i] = pillowDirY[i] * m_autoPillowStrength
                         + detailGY[i] * m_autoDetailStrength * (invertDetail ? -1.0f : 1.0f)
                         - panelDirY[i] * panelProfile[i] * m_autoPanelStrength;
        }
    }

    // Remap a raw linear projection value into [0,1] using fitTMin/fitTMax
    auto remapT = [&](float proj) -> float {
        const float span = fitTMax - fitTMin;
        return (span > 1e-6f) ? qBound(0.0f, (proj - fitTMin) / span, 1.0f)
                               : qBound(0.0f, proj, 1.0f);
    };

    for (int y = 0; y < h; ++y) {
        const QRgb*  spLine  = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(y));
        QRgb*        cnLine  = reinterpret_cast<QRgb*>(m_companion.scanLine(y));
        const uchar* selLine = hasSel ? m_selectionMask.constScanLine(y) : nullptr;

        for (int x = 0; x < w; ++x) {
            if (qAlpha(spLine[x]) == 0) continue;
            if (hasSel && selLine[x] == 0) continue;

            int rOut = sr, gOut = sg, bOut = sb;

            switch (m_fillType) {

            case FillType::Slope:
                [[fallthrough]];
            case FillType::TwoSides: {
                float t = 0.0f;
                if (lenSq > 1e-6f) {
                    if (m_fillFitToSel && effectiveHasSel && !slopeSmMin.empty()) {
                        // Per-band fit: look up the Gaussian-smoothed [min,max] for this
                        // pixel's perpendicular strip and normalise the projection within it.
                        const float proj = slopeNdx * float(x - start.x()) + slopeNdy * float(y - start.y());
                        const float perp = -slopeNdy * float(x - start.x()) + slopeNdx * float(y - start.y());
                        const int band = int(std::floor(perp + 0.5f));
                        const int bi = qBound(0, band - slopeBandLo, int(slopeSmMin.size()) - 1);
                        const float mn = slopeSmMin[bi], mx = slopeSmMax[bi];
                        t = (mx > mn + 1e-6f) ? qBound(0.0f, (proj - mn) / (mx - mn), 1.0f) : 0.5f;
                    } else {
                        const float proj = (float(x - start.x()) * float(delta.x()) +
                                            float(y - start.y()) * float(delta.y())) / lenSq;
                        t = remapT(proj);
                    }
                }
                if (m_fillInvert) t = 1.0f - t;
                rOut = sr + int(float(er - sr) * t + 0.5f);
                gOut = sg + int(float(eg - sg) * t + 0.5f);
                bOut = sb + int(float(eb - sb) * t + 0.5f);
                break;
            }

            case FillType::Prism: {
                // Rectangular prism: flat center + 4 independent side gradient zones.
                // Each zone is like a CSS border: its width is a fraction [0,1] of the
                // rectangle's half-dimension on that axis. Pixels outside all zones
                // receive a flat normal. Corner pixels are assigned to the side whose
                // zone they are deepest into (highest normalised t).
                float rx = 0.0f, ry = 0.0f;
                float hw = 0.0f, hh = 0.0f;  // half-dimensions in pixels (for AA)

                const bool fitMode = m_fillFitToSel && effectiveHasSel
                                  && prismFitX1 > prismFitX0 && prismFitY1 > prismFitY0;
                if (fitMode) {
                    hw = (prismFitX1 - prismFitX0) * 0.5f;
                    hh = (prismFitY1 - prismFitY0) * 0.5f;
                    const float cx = (prismFitX0 + prismFitX1) * 0.5f;
                    const float cy = (prismFitY0 + prismFitY1) * 0.5f;
                    rx = (hw > 0.5f) ? (float(x) - cx) / hw : 0.0f;
                    ry = (hh > 0.5f) ? (float(y) - cy) / hh : 0.0f;
                } else {
                    const float x0 = qMin(float(start.x()), float(end.x()));
                    const float y0 = qMin(float(start.y()), float(end.y()));
                    const float x1 = qMax(float(start.x()), float(end.x()));
                    const float y1 = qMax(float(start.y()), float(end.y()));
                    hw = (x1 - x0) * 0.5f;
                    hh = (y1 - y0) * 0.5f;
                    if (hw < 0.5f || hh < 0.5f) { rOut = 128; gOut = 128; bOut = 255; break; }
                    rx = (float(x) - (x0 + hw)) / hw;
                    ry = (float(y) - (y0 + hh)) / hh;
                }

                // t for each side: 0 at the inner zone boundary, 1 at the outer edge.
                // Negative → pixel is outside that side's zone.
                const float tR = (m_prismSideR > 1e-6f && rx > 0.0f)
                               ? (rx - (1.0f - m_prismSideR)) / m_prismSideR : -1.0f;
                const float tL = (m_prismSideL > 1e-6f && rx < 0.0f)
                               ? (-rx - (1.0f - m_prismSideL)) / m_prismSideL : -1.0f;
                const float tB = (m_prismSideB > 1e-6f && ry > 0.0f)
                               ? (ry - (1.0f - m_prismSideB)) / m_prismSideB : -1.0f;
                const float tT = (m_prismSideT > 1e-6f && ry < 0.0f)
                               ? (-ry - (1.0f - m_prismSideT)) / m_prismSideT : -1.0f;

                // Winning side = deepest active zone (highest t > 0)
                float tMax   = 0.0f;
                int   winner = -1;
                if (tR > tMax) { tMax = tR; winner = 0; }
                if (tL > tMax) { tMax = tL; winner = 1; }
                if (tB > tMax) { tMax = tB; winner = 2; }
                if (tT > tMax) { tMax = tT; winner = 3; }

                if (winner < 0) {
                    // Flat center region
                    rOut = 128; gOut = 128; bOut = 255;
                    break;
                }

                float outX = 0.0f, outY = 0.0f;
                if      (winner == 0) { outX =  1.0f; }
                else if (winner == 1) { outX = -1.0f; }
                else if (winner == 2) { outY =  1.0f; }
                else                  { outY = -1.0f; }

                // Invert = negate XY (normals point inward → groove instead of ridge)
                if (m_fillInvert) { outX = -outX; outY = -outY; }

                // Side normal: inclination angle controls the slope.
                // 90° = vertical side (nz=0, fully lateral).
                // 0°  = flat (same as center, nz=1, no lateral).
                constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
                const float lat = std::sin(m_prismAngle * kDegToRad);
                const float nz  = std::cos(m_prismAngle * kDegToRad);

                // Anti-alias: blend toward flat at the inner edge (zone→center boundary)
                // and outer edge (zone→rectangle boundary). Zone width in pixels = sideSize*halfDim.
                const float sideSize = (winner==0) ? m_prismSideR : (winner==1) ? m_prismSideL
                                     : (winner==2) ? m_prismSideB : m_prismSideT;
                const float halfDim  = (winner==0 || winner==1) ? hw : hh;
                const float zonePx   = sideSize * halfDim;
                constexpr float kAA  = 1.5f;  // anti-alias width in pixels
                const float aa_inner = (zonePx > 0.5f) ? qBound(0.0f, tMax * zonePx / kAA, 1.0f) : 1.0f;
                const float aa_outer = (zonePx > 0.5f) ? qBound(0.0f, (1.0f - tMax) * zonePx / kAA, 1.0f) : 1.0f;
                const float aa       = aa_inner * aa_outer;
                rOut = qBound(0, int((outX * lat * aa + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                gOut = qBound(0, int((outY * lat * aa + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                bOut = qBound(0, int((aa * nz + (1.0f - aa)) * 255.0f + 0.5f), 255);
                break;
            }

            case FillType::Dome: {
                // Fit mode:     blurred-gradient balloon fills the selection shape.
                //               outXField/outYField already encode direction + lateral strength:
                //               magnitude ≈ 1 near boundary (rim), ≈ 0 at medial axis (flat top).
                //               No normalization → no direction flips → no peaks.
                // Non-fit mode: hemisphere clipped to the drag ellipse (rx=|Δx|, ry=|Δy|).
                const bool adapted = m_fillFitToSel && effectiveHasSel;

                if (adapted && !distField.empty()) {
                    // Blurred un-normalized gradient: encodes both XY direction and lateral strength.
                    const float df = distField[y * w + x];
                    float oxF = outXField[y * w + x];
                    float oyF = outYField[y * w + x];
                    // AA: 1.5px fade at selection boundary (df → 0)
                    const float domeAA = qBound(0.0f, df * float(donutMaxDistPx) / 1.5f, 1.0f);
                    oxF *= domeAA * m_fillRoundness;
                    oyF *= domeAA * m_fillRoundness;
                    if (m_fillInvert) { oxF = -oxF; oyF = -oyF; }
                    // nz derived directly from XY magnitude; no separate r needed
                    const float nz = std::sqrt(std::max(0.0f, 1.0f - qMin(1.0f, oxF*oxF + oyF*oyF)));
                    rOut = qBound(0, int((oxF + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                    gOut = qBound(0, int((oyF + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                    bOut = qBound(0, int(nz * 255.0f + 0.5f), 255);
                } else if (!adapted) {
                    // Non-fit: ellipse defined by drag (center=start, rx=|Δx|, ry=|Δy|)
                    const float dxPx = float(x) - float(start.x());
                    const float dyPx = float(y) - float(start.y());
                    const float ellRx = std::abs(float(delta.x()));
                    const float ellRy = std::abs(float(delta.y()));
                    if (ellRx < 0.5f && ellRy < 0.5f) {
                        rOut = 128; gOut = 128; bOut = 255; break;
                    }
                    const float safeRx = std::max(ellRx, 0.5f);
                    const float safeRy = std::max(ellRy, 0.5f);
                    const float eD = std::sqrt((dxPx/safeRx)*(dxPx/safeRx)
                                             + (dyPx/safeRy)*(dyPx/safeRy));
                    if (eD > 1.0f) { rOut = 128; gOut = 128; bOut = 255; break; }
                    constexpr float kAA = 1.5f;
                    const float domeAA = qBound(0.0f, (1.0f - eD) * std::min(ellRx, ellRy) / kAA, 1.0f);
                    const float r = eD * m_fillRoundness;
                    float outX = 0.0f, outY = 0.0f;
                    const float d = std::sqrt(dxPx*dxPx + dyPx*dyPx);
                    if (d > 0.5f) { outX = dxPx/d; outY = dyPx/d; }
                    if (m_fillInvert) { outX = -outX; outY = -outY; }
                    const float nz = std::sqrt(std::max(0.0f, 1.0f - r * r));
                    rOut = qBound(0, int((outX * r * domeAA + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                    gOut = qBound(0, int((outY * r * domeAA + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                    bOut = qBound(0, int((domeAA * nz + (1.0f - domeAA)) * 255.0f + 0.5f), 255);
                } else {
                    rOut = 128; gOut = 128; bOut = 255;  // fallback: flat
                }
                break;
            }

            case FillType::Cone: {
                // Automatic cone: apex → flat (0,0,1), rim → radially outward (nz=0).
                // XY always points radially outward; nz decreases linearly (cone profile).
                // Non-adapted: apex = drag start, rim = drag end (matches preview circle).
                // Adapted: apex = selection centroid, fitRadius = max dist to selection edge.
                const bool  adapted = m_fillFitToSel && effectiveHasSel;
                const float apexX   = adapted ? centX : float(start.x());
                const float apexY   = adapted ? centY : float(start.y());
                const float dx      = float(x) - apexX;
                const float dy      = float(y) - apexY;
                const float d       = std::sqrt(dx*dx + dy*dy);
                if (d > 0.5f && (adapted || d <= fitRadius)) {
                    const float t    = qBound(0.0f, fitRadius > 1e-3f ? d / fitRadius : 0.0f, 1.0f);
                    // Anti-alias rim: fade the lateral component to flat over 1.5px at the rim.
                    const float rimAA = qBound(0.0f, (fitRadius - d) / 1.5f, 1.0f);
                    const float t_eff = t * rimAA;  // reduces to 0 (flat) at rim
                    // nz linear 1→0 (apex flat, rim fully lateral); roundness limits max tilt
                    const float nzAt = 1.0f - t_eff * m_fillRoundness;
                    const float rHAt = std::sqrt(std::max(0.0f, 1.0f - nzAt * nzAt));
                    // Invert = negate XY (normals point inward → conical hole)
                    const float sign = m_fillInvert ? -1.0f : 1.0f;
                    rOut = qBound(0, int((sign * (dx/d) * rHAt + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                    gOut = qBound(0, int((sign * (dy/d) * rHAt + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                    bOut = qBound(0, int(nzAt * 255.0f + 0.5f), 255);
                } else {
                    // Apex pixel: flat normal
                    rOut = 128; gOut = 128; bOut = 255;
                }
                break;
            }

            case FillType::Donut: {
                // Automatic torus: a cylinder wrapped into an elliptical ring.
                // Ring edges (t=0) → fully lateral; ring axis (t=1) → flat (0,0,1).
                // Outer edge points outward, inner edge points inward.
                // Non-fit: drag defines centre + ellipse radii (rx=|dx|, ry=|dy|).
                float t = 0.0f;
                float outX = 0.0f, outY = 0.0f;
                float donutAA = 1.0f;  // anti-alias factor at ring edges

                if (m_fillFitToSel && effectiveHasSel && !distField.empty()) {
                    // BFS-based ring: distField = 0 at selection boundary, 1 at interior peak.
                    // The ring band occupies distField ∈ [0, thickness].
                    // Pixels further inside (> thickness) are the hole → keep t=0, flat normal.
                    const float df = distField[y * w + x];
                    const float thickness = qMax(m_donutLength, 0.01f);
                    if (df <= thickness) {
                        const float p = df / thickness;  // 0 = outer edge, 1 = inner (hole) edge
                        t = 1.0f - 2.0f * std::abs(p - 0.5f);
                        // outXField points toward boundary (outward from selection interior)
                        const float ox = outXField[y * w + x];
                        const float oy = outYField[y * w + x];
                        // Outer half: normal faces outward; inner half: normal faces into hole
                        outX = (p < 0.5f) ? ox : -ox;
                        outY = (p < 0.5f) ? oy : -oy;
                        // AA: 1.5px fade at outer edge (selection boundary) and hole boundary.
                        // donutMaxDistPx converts normalised BFS units to pixels.
                        const float outerPx = df * float(donutMaxDistPx);
                        const float holePx  = (thickness - df) * float(donutMaxDistPx);
                        donutAA = qBound(0.0f, outerPx / 1.5f, 1.0f)
                                * qBound(0.0f, holePx  / 1.5f, 1.0f);
                    }
                    // df > thickness: interior hole → t=0, outX/Y=0 → flat normal
                } else {
                    // Outer ellipse = drag end; inner ellipse = outer * (1 - thickness).
                    // Ring occupies eD in [innerFrac, 1]. Ring axis at eD = (1+innerFrac)*0.5.
                    const float ell_rx = std::abs(float(end.x() - start.x()));
                    const float ell_ry = std::abs(float(end.y() - start.y()));
                    if (ell_rx > 0.5f && ell_ry > 0.5f) {
                        const float thickness = qMax(m_donutLength, 0.01f);
                        const float innerFrac = 1.0f - thickness;

                        const float ddx = float(x) - float(start.x());
                        const float ddy = float(y) - float(start.y());
                        const float eD  = std::sqrt((ddx/ell_rx)*(ddx/ell_rx)
                                                  + (ddy/ell_ry)*(ddy/ell_ry));

                        if (eD >= innerFrac && eD <= 1.0f) {
                            // p in [0,1]: 0=inner edge, 1=outer edge; axis at 0.5
                            const float p = (eD - innerFrac) / thickness;
                            t = 1.0f - 2.0f * std::abs(p - 0.5f);

                            // Outward direction = gradient of eD, normalized
                            const float gx   = ddx / (ell_rx * ell_rx);
                            const float gy   = ddy / (ell_ry * ell_ry);
                            const float gLen = std::sqrt(gx*gx + gy*gy);
                            if (gLen > 1e-6f) {
                                const float ox = gx / gLen;
                                const float oy = gy / gLen;
                                // outer half (p≥0.5): outward; inner half (p<0.5): inward
                                outX = (p >= 0.5f) ? ox : -ox;
                                outY = (p >= 0.5f) ? oy : -oy;
                            }
                            // AA: 1.5px fade at inner edge (hole) and outer edge.
                            // Approximate pixel distance using min(ell_rx, ell_ry) as the scale.
                            const float minR = qMin(ell_rx, ell_ry);
                            donutAA = qBound(0.0f, (eD - innerFrac) * minR / 1.5f, 1.0f)
                                    * qBound(0.0f, (1.0f - eD)      * minR / 1.5f, 1.0f);
                        }
                    }
                }

                // Invert = negate XY (outer edge points inward, inner edge points out → groove)
                if (m_fillInvert) { outX = -outX; outY = -outY; }

                if (outX == 0.0f && outY == 0.0f) {
                    // At ring axis (gradient zero) or undefined position: flat normal
                    rOut = 128; gOut = 128; bOut = 255;
                } else {
                    // Cylinder profile: edge → lateral, axis → flat; roundness limits max tilt
                    constexpr float kHalfPi = 1.5707963267948966f;
                    const float angle = (1.0f - t) * kHalfPi * m_fillRoundness;
                    const float nz_raw  = std::cos(angle);
                    const float lat_raw = std::sin(angle);
                    // Apply AA: scale lateral by donutAA, blend nz toward 1 (flat)
                    rOut = qBound(0, int((outX * lat_raw * donutAA + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                    gOut = qBound(0, int((outY * lat_raw * donutAA + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                    bOut = qBound(0, int((donutAA * nz_raw + (1.0f - donutAA)) * 255.0f + 0.5f), 255);
                }
                break;
            }

            case FillType::Cylinder: {
                // Drag across the cylinder surface: start = one edge, end = other edge.
                // t=0 → left-facing, t=0.5 → viewer-facing (0,0,1), t=1 → right-facing.
                // From/To normals are not used; shape is purely geometric.
                float t = 0.5f;
                if (lenSq > 1e-6f) {
                    if (m_fillFitToSel && effectiveHasSel && !slopeSmMin.empty()) {
                        // Per-band fit: stretch each strip along the cylinder axis independently.
                        const float proj = slopeNdx * float(x - start.x()) + slopeNdy * float(y - start.y());
                        const float perp = -slopeNdy * float(x - start.x()) + slopeNdx * float(y - start.y());
                        const int band = int(std::floor(perp + 0.5f));
                        const int bi = qBound(0, band - slopeBandLo, int(slopeSmMin.size()) - 1);
                        const float mn = slopeSmMin[bi], mx = slopeSmMax[bi];
                        t = (mx > mn + 1e-6f) ? qBound(0.0f, (proj - mn) / (mx - mn), 1.0f) : 0.5f;
                    } else {
                        const float proj = (float(x - start.x()) * float(delta.x()) +
                                            float(y - start.y()) * float(delta.y())) / lenSq;
                        t = remapT(proj);
                    }
                }
                if (m_fillInvert) t = 1.0f - t;
                const float axLen = std::sqrt(lenSq);
                const float axX   = axLen > 1e-6f ? float(delta.x()) / axLen : 1.0f;
                const float axY   = axLen > 1e-6f ? float(delta.y()) / axLen : 0.0f;
                // Sweep ±π/2 around the drag direction; roundness limits max tilt at edges
                constexpr float kHalfPi = 1.5707963267948966f;
                const float angle = (t - 0.5f) * 2.0f * kHalfPi * m_fillRoundness;
                const float sinA  = std::sin(angle);
                const float cosA  = std::cos(angle);
                rOut = qBound(0, int((sinA * axX + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                gOut = qBound(0, int((sinA * axY + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                bOut = qBound(0, int(cosA * 255.0f + 0.5f), 255);
                break;
            }

            case FillType::Emboss: {
                // Bevel at the selection/sprite boundary.
                // dfPx = pixel distance from boundary (0=at boundary, >innerWidth=interior)
                if (distField.empty()) { rOut = 128; gOut = 128; bOut = 255; break; }
                const float dfPx = distField[y * w + x] * float(donutMaxDistPx);
                const float bevelWidth = m_embossInnerWidth;

                if (dfPx >= bevelWidth) {
                    // Interior: flat normal
                    rOut = 128; gOut = 128; bOut = 255; break;
                }

                // t: 0=boundary edge, 1=inner edge of bevel
                const float t = (bevelWidth > 1e-3f) ? dfPx / bevelWidth : 1.0f;

                // Profile shape: maps t → q ∈ [0,1] (q=1 at peak of bevel)
                // midpoint=0: simple slope (peak at boundary); midpoint>0: ridge
                const float mp = m_embossMidpoint;
                float q;
                if (mp < 0.01f) {
                    q = 1.0f - t;  // peak at boundary
                } else {
                    q = (t <= mp) ? t / mp : (1.0f - t) / (1.0f - mp);
                }
                // Bevel shape curve
                float profile;
                constexpr float kPi = 3.14159265358979f;
                switch (m_embossBevelShape) {
                    case 1:  profile = q*q*(3.0f - 2.0f*q); break;              // smoothstep
                    case 2:  profile = std::sqrt(qMax(0.0f, 2.0f*q - q*q)); break; // rounded arc
                    case 3:  profile = (1.0f - std::cos(q * kPi)) * 0.5f; break;  // cosine
                    case 4:  profile = std::sin(q * kPi * 0.5f); break;           // sine
                    default: profile = q; break;                                   // linear
                }

                // Lateral magnitude: profile * height, clamped to [0,1]
                const float lat = qBound(0.0f, profile * m_embossHeight, 1.0f);

                // Outward direction from BFS (symmetric bevel)
                float oX = outXField[y * w + x];
                float oY = outYField[y * w + x];

                // Angle direction: controls global directional bias
                const float kDegToRad = kPi / 180.0f;
                const float aX =  std::cos(m_embossAngle * kDegToRad);
                const float aY = -std::sin(m_embossAngle * kDegToRad);  // -Y: screen vs math

                // Blend: strength=0 → symmetric outward; strength=1 → purely directional
                float bX = oX + (aX - oX) * m_embossStrength;
                float bY = oY + (aY - oY) * m_embossStrength;
                const float bLen = std::sqrt(bX*bX + bY*bY);
                if (bLen > 1e-6f) { bX /= bLen; bY /= bLen; }

                // Invert = flip the bevel direction (raised ↔ recessed)
                if (m_fillInvert) { bX = -bX; bY = -bY; }

                const float nz = std::sqrt(qMax(0.0f, 1.0f - lat*lat));
                rOut = qBound(0, int((bX * lat + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                gOut = qBound(0, int((bY * lat + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                bOut = qBound(0, int(nz * 255.0f + 0.5f), 255);
                break;
            }

            case FillType::EdgeEmboss: {
                if (distField.empty()) { rOut = 128; gOut = 128; bOut = 255; break; }
                // distField stores raw pixel distances (not normalized)
                const float dfPx = distField[y * w + x];
                if (dfPx >= m_edgeEmbossWidth) { rOut = 128; gOut = 128; bOut = 255; break; }

                const float t = (m_edgeEmbossWidth > 1e-3f) ? dfPx / m_edgeEmbossWidth : 1.0f;
                const float mp = m_edgeEmbossMidpoint;
                float q;
                if (mp < 0.01f) { q = 1.0f - t; }
                else             { q = (t <= mp) ? t / mp : (1.0f - t) / (1.0f - mp); }

                float profile;
                constexpr float kPiEE = 3.14159265358979f;
                switch (m_edgeEmbossBevelShape) {
                    case 1:  profile = q*q*(3.0f - 2.0f*q); break;
                    case 2:  profile = std::sqrt(qMax(0.0f, 2.0f*q - q*q)); break;
                    case 3:  profile = (1.0f - std::cos(q * kPiEE)) * 0.5f; break;
                    case 4:  profile = std::sin(q * kPiEE * 0.5f); break;
                    default: profile = q; break;
                }
                const float lat = qBound(0.0f, profile * m_edgeEmbossHeight, 1.0f);
                float bX = outXField[y * w + x];
                float bY = outYField[y * w + x];
                if (m_fillInvert) { bX = -bX; bY = -bY; }
                const float nz = std::sqrt(qMax(0.0f, 1.0f - lat*lat));
                rOut = qBound(0, int((bX * lat + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                gOut = qBound(0, int((bY * lat + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                bOut = qBound(0, int(nz * 255.0f + 0.5f), 255);
                break;
            }

            case FillType::EdgeTrace: {
                // outXField/outYField hold thresholded, optionally spread Sobel gradient.
                // Zero gradient (below-threshold pixels) → flat normal naturally.
                float nx = outXField.empty() ? 0.0f : outXField[y*w+x] * m_edgeTraceScale;
                float ny = outYField.empty() ? 0.0f : outYField[y*w+x] * m_edgeTraceScale;
                if (m_fillInvert) { nx = -nx; ny = -ny; }
                const float len = std::sqrt(nx*nx + ny*ny + 1.0f);
                rOut = qBound(0, int((nx/len + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                gOut = qBound(0, int((ny/len + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                bOut = qBound(0, int(255.0f / len + 0.5f), 255);
                break;
            }

            case FillType::HeightField: {
                if (outXField.empty()) { rOut = 128; gOut = 128; bOut = 255; break; }
                float nx = outXField[y*w+x];
                float ny = outYField[y*w+x];
                if (m_fillInvert) { nx = -nx; ny = -ny; }
                // N = normalize(nx, ny, 1) — Z=1 gives height-field normal convention
                const float len = std::sqrt(nx*nx + ny*ny + 1.0f);
                nx /= len; ny /= len;
                const float nz = 1.0f / len;
                rOut = qBound(0, int((nx + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                gOut = qBound(0, int((ny + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                bOut = qBound(0, int(nz * 255.0f + 0.5f), 255);
                break;
            }

            case FillType::AutoNormal: {
                // outXField/outYField already hold the blended (pillow+detail-panel) XY gradient.
                // N = normalize(nx, ny, 1): implicit Z=1 height-field convention.
                if (outXField.empty()) { rOut = 128; gOut = 128; bOut = 255; break; }
                const float nx = outXField[y*w+x];
                const float ny = outYField[y*w+x];
                const float len = std::sqrt(nx*nx + ny*ny + 1.0f);
                rOut = qBound(0, int((nx/len + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                gOut = qBound(0, int((ny/len + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
                bOut = qBound(0, int(255.0f / len + 0.5f), 255);
                break;
            }

            }

            cnLine[x] = qRgba(rOut, gOut, bOut, 255);
        }
    }

    m_modified = true;
    updateCompanionItem();
    emit companionModified();
}

// ---------------------------------------------------------------------------
// Pencil type
// ---------------------------------------------------------------------------

void NormalMapPaintCanvas::setPencilType(PencilType type)
{
    m_pencilType = type;
}

// ---------------------------------------------------------------------------
// Normal direction → paint color
// ---------------------------------------------------------------------------

void NormalMapPaintCanvas::setNormal(float nx, float ny, float nz)
{
    const int r = qBound(0, int((nx + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
    const int g = qBound(0, int((ny + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
    const int b = qBound(0, int(nz * 255.0f + 0.5f), 255);
    setPaintColor(QColor(r, g, b, 255));
}

// ---------------------------------------------------------------------------
// paintBrushAt override
// ---------------------------------------------------------------------------

void NormalMapPaintCanvas::paintBrushAt(QPoint center)
{
    if ((m_tool == Tool::Pencil || m_tool == Tool::Curve) &&
               (m_pencilType == PencilType::BevelRise || m_pencilType == PencilType::BevelCarve)) {
        // Bevel: accumulate per-stamp dome heights into an ephemeral float buffer (max-blend),
        // then derive normals via Sobel.  This avoids RGB-space blending artefacts at stroke
        // edges and fills the gaps that appear between stamps when painting diagonally.
        if (m_companion.isNull() || m_sprite.isNull()) return;

        const int   imgW  = m_companion.width();
        const int   imgH  = m_companion.height();
        const int   half  = m_brushSize / 2;
        const bool  hard  = (m_brushHardness >= 1.0f) || (half == 0);
        const float fhalf = (half > 0) ? float(half) : 1.0f;
        const bool  carve = (m_pencilType == PencilType::BevelCarve);
        const float maxTilt = m_blurStrength * (float(M_PI) / 2.0f);

        // Lazy-initialise stroke buffers on the first stamp of a new stroke.
        if (!m_bevelStrokeActive) {
            m_bevelBase = m_companion.copy();
            m_bevelHeightBuf.assign(imgW * imgH, 0.0f);
            m_bevelStrokeActive = true;
        }

        // Accumulate dome heights with max-blend so overlapping stamps merge smoothly.
        for (int dy = -half; dy <= half; ++dy) {
            const int py = center.y() + dy;
            if (py < 0 || py >= imgH) continue;
            const QRgb* spriteLine = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(py));

            for (int dx = -half; dx <= half; ++dx) {
                const int px = center.x() + dx;
                if (px < 0 || px >= imgW) continue;
                if (qAlpha(spriteLine[px]) == 0) continue;
                float selWeight = 1.0f;
                if (m_hasSelection) {
                    const int mv = m_selectionMask.constScanLine(py)[px];
                    if (mv == 0) continue;
                    selWeight = mv / 255.0f;
                }

                float weight;
                if (hard) {
                    if (m_brushShape == BrushShape::Circle) {
                        if (dx * dx + dy * dy > half * half + half) continue;
                    }
                    weight = 1.0f;
                } else {
                    float t;
                    if (m_brushShape == BrushShape::Circle) {
                        t = std::sqrt(float(dx * dx + dy * dy)) / fhalf;
                        if (t > 1.0f) continue;
                    } else {
                        t = std::max(std::abs(float(dx)), std::abs(float(dy))) / fhalf;
                        if (t > 1.0f) continue;
                    }
                    weight = (t <= m_brushHardness)
                        ? 1.0f
                        : (1.0f - t) / (1.0f - m_brushHardness);
                    weight = qBound(0.0f, weight, 1.0f);
                }
                weight *= selWeight;
                if (weight <= 0.0f) continue;

                // Cosine dome profile: weight attenuates the rim so soft brushes taper
                // smoothly to zero coverage, eliminating the hard-edge artefact.
                const float r     = std::sqrt(float(dx * dx + dy * dy));
                const float domeH = std::cos(maxTilt * r / fhalf) * weight;
                float& bufH = m_bevelHeightBuf[py * imgW + px];
                if (domeH > bufH)
                    bufH = domeH;
            }
        }

        // Recompute normals from the height buffer for the stamp region (+1 for Sobel neighbours).
        const QRect stampRegion(
            qMax(0,        center.x() - half - 1),
            qMax(0,        center.y() - half - 1),
            qMin(imgW - 1, center.x() + half + 1) - qMax(0, center.x() - half - 1) + 1,
            qMin(imgH - 1, center.y() + half + 1) - qMax(0, center.y() - half - 1) + 1);
        applyBevelHeight(imgW, imgH, stampRegion, maxTilt, fhalf, carve);
    } else {
        MapPaintCanvas::paintBrushAt(center);
    }
    m_glNormalDirty = true;
}

// ---------------------------------------------------------------------------
// applyBevelHeight – convert height buffer → normals and composite onto m_companion
// ---------------------------------------------------------------------------

void NormalMapPaintCanvas::applyBevelHeight(
    int imgW, int imgH, const QRect& region,
    float maxTilt, float fhalf, bool carve)
{
    // nz_scale calibrates the Sobel gradient so the normal tilt at r=fhalf equals maxTilt.
    // Derived from: tan(maxTilt) = (sin(maxTilt)*maxTilt/fhalf) / nz_scale
    //   => nz_scale = cos(maxTilt) * maxTilt / fhalf
    const float nz_scale = std::cos(maxTilt) * maxTilt / fhalf;

    const float* hBuf = m_bevelHeightBuf.data();

    for (int y = region.top(); y <= region.bottom(); ++y) {
        if (y < 0 || y >= imgH) continue;
        const QRgb* spriteLine = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(y));
        const QRgb* baseLine   = reinterpret_cast<const QRgb*>(m_bevelBase.constScanLine(y));
        QRgb*       compLine   = reinterpret_cast<QRgb*>(m_companion.scanLine(y));

        for (int x = region.left(); x <= region.right(); ++x) {
            if (x < 0 || x >= imgW) continue;
            if (qAlpha(spriteLine[x]) == 0) continue;

            const float h = hBuf[y * imgW + x];
            if (h <= 0.0f) {
                compLine[x] = baseLine[x];
                continue;
            }

            // Sobel gradient of the height buffer (central differences, clamp at edges).
            const float hL = (x > 0)        ? hBuf[y * imgW + x - 1] : h;
            const float hR = (x < imgW - 1) ? hBuf[y * imgW + x + 1] : h;
            const float hU = (y > 0)        ? hBuf[(y - 1) * imgW + x] : h;
            const float hD = (y < imgH - 1) ? hBuf[(y + 1) * imgW + x] : h;
            const float gx = (hR - hL) * 0.5f;
            const float gy = (hD - hU) * 0.5f;

            // Normal from gradient: surface rises toward the brush centre so invert gradient.
            float nx = -gx;
            float ny = -gy;
            float nz = nz_scale;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
            else              { nx = 0.0f; ny = 0.0f; nz = 1.0f; }

            if (carve) { nx = -nx; ny = -ny; }

            // Blend the bevel normal with the base using h as coverage [0,1].
            const QRgb  base    = baseLine[x];
            const float nx_base = qRed(base)   / 127.5f - 1.0f;
            const float ny_base = qGreen(base) / 127.5f - 1.0f;
            const float nz_base = qBlue(base)  / 255.0f;
            const float alpha   = qBound(0.0f, h, 1.0f);
            const float w0      = 1.0f - alpha;

            float nx_out = nx_base * w0 + nx * alpha;
            float ny_out = ny_base * w0 + ny * alpha;
            float nz_out = nz_base * w0 + nz * alpha;
            const float outLen = std::sqrt(nx_out * nx_out + ny_out * ny_out + nz_out * nz_out);
            if (outLen > 1e-6f) { nx_out /= outLen; ny_out /= outLen; nz_out /= outLen; }
            else                { nx_out = 0.0f;    ny_out = 0.0f;    nz_out = 1.0f; }

            const int rOut = qBound(0, int((nx_out + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
            const int gOut = qBound(0, int((ny_out + 1.0f) * 0.5f * 255.0f + 0.5f), 255);
            const int bOut = qBound(0, int(nz_out * 255.0f + 0.5f), 255);
            compLine[x] = qRgba(rOut, gOut, bOut, 255);
        }
    }
}

// ---------------------------------------------------------------------------
// View
// ---------------------------------------------------------------------------

void NormalMapPaintCanvas::setViewBlend(float blend)
{
    m_viewBlend = qBound(0.0f, blend, 1.0f);
    if (m_companionItem)
        m_companionItem->setVisible(m_viewBlend < 1.0f);
    if (m_litItem)
        m_litItem->setOpacity(m_viewBlend);
}

void NormalMapPaintCanvas::setViewVisible(bool visible)
{
    if (m_companionItem) m_companionItem->setVisible(visible);
    if (m_litItem)       m_litItem->setVisible(visible);
}

// ---------------------------------------------------------------------------
// Lighting
// ---------------------------------------------------------------------------

void NormalMapPaintCanvas::setLightColor(const QColor& color)
{
    m_lightColor = color;
    updateLightIndicator();
    recomputeLit();
    updateCompanionItem();
}

void NormalMapPaintCanvas::setLightStrength(float strength)
{
    m_lightStrength = qMax(0.0f, strength);
    updateLightIndicator();
    recomputeLit();
    updateCompanionItem();
    emit lightStrengthChanged(m_lightStrength);
}

void NormalMapPaintCanvas::setAmbientStrength(float ambient)
{
    m_ambientStrength = qBound(0.0f, ambient, 1.0f);
    recomputeLit();
    updateCompanionItem();
}

void NormalMapPaintCanvas::setLightXY(int x, int y)
{
    m_lightX = x;
    m_lightY = y;
    updateLightIndicator();
    recomputeLit();
    updateCompanionItem();
}

void NormalMapPaintCanvas::setLightZ(int z)
{
    m_lightZ = qMax(0, z);
    updateLightIndicator();
    recomputeLit();
    updateCompanionItem();
    emit lightZChanged(m_lightZ);
}

// ---------------------------------------------------------------------------
// Companion item update
// ---------------------------------------------------------------------------

void NormalMapPaintCanvas::updateCompanionItem()
{
    if (m_companion.isNull()) return;

    // Composite all visible layers; used for display and GL lighting
    m_compositeCache = compositeLayers();

    // Bottom layer (z=1): normal map — hidden when lit preview is fully opaque
    if (m_companionItem) {
        m_companionItem->setPixmap(QPixmap::fromImage(m_compositeCache));
        m_companionItem->setVisible(m_viewBlend < 1.0f);
    }

    // Top layer (z=1.5): lit preview at m_viewBlend opacity
    if (m_litItem) {
        m_glNormalDirty = true;   // composite just refreshed — force GL texture re-upload
        recomputeLit();
        m_litItem->setPixmap(m_litCache.isNull()
            ? QPixmap() : QPixmap::fromImage(m_litCache));
        m_litItem->setOpacity(m_viewBlend);
    }

    // Sprite item hidden; companion and lit items cover it
    if (m_spriteItem)
        m_spriteItem->setVisible(false);
}

// ---------------------------------------------------------------------------
// Lighting composite — CPU fallback
// ---------------------------------------------------------------------------

void NormalMapPaintCanvas::recomputeLitCPU()
{
    if (m_sprite.isNull() || m_compositeCache.isNull()) {
        m_litCache = QImage();
        return;
    }

    const float lr = m_lightColor.redF();
    const float lg = m_lightColor.greenF();
    const float lb = m_lightColor.blueF();
    const int w = m_sprite.width();
    const int h = m_sprite.height();

    const float lightPosX = w * 0.5f + float(m_lightX);
    const float lightPosY = h * 0.5f + float(m_lightY);
    const float lightPosZ = float(m_lightZ);

    m_litCache = QImage(w, h, QImage::Format_ARGB32);

    for (int y = 0; y < h; ++y) {
        const QRgb* spLine = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(y));
        const QRgb* cnLine = reinterpret_cast<const QRgb*>(m_compositeCache.constScanLine(y));
        QRgb*       ltLine = reinterpret_cast<QRgb*>(m_litCache.scanLine(y));

        for (int x = 0; x < w; ++x) {
            const QRgb sp = spLine[x];
            if (qAlpha(sp) == 0) { ltLine[x] = 0; continue; }

            const float Lx = lightPosX - (x + 0.5f);
            const float Ly = lightPosY - (y + 0.5f);
            const float Llen = std::sqrt(Lx*Lx + Ly*Ly + lightPosZ*lightPosZ);
            const float lnx = (Llen > 1e-6f) ? Lx        / Llen : 0.0f;
            const float lny = (Llen > 1e-6f) ? Ly        / Llen : 0.0f;
            const float lnz = (Llen > 1e-6f) ? lightPosZ / Llen : 1.0f;

            const QRgb cn = cnLine[x];
            float nx, ny, nz;
            if (qAlpha(cn) > 0) {
                nx = (qRed(cn)   / 255.0f) * 2.0f - 1.0f;
                ny = (qGreen(cn) / 255.0f) * 2.0f - 1.0f;
                nz = (qBlue(cn)  / 255.0f) * 2.0f - 1.0f;
            } else {
                nx = 0.0f; ny = 0.0f; nz = 1.0f;
            }
            const float nlen = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (nlen > 0.001f) { nx /= nlen; ny /= nlen; nz /= nlen; }

            const float diff = qMax(0.0f, nx * lnx + ny * lny + nz * lnz);

            const float sr = qRed(sp)   / 255.0f;
            const float sg = qGreen(sp) / 255.0f;
            const float sb = qBlue(sp)  / 255.0f;

            const float fr = qBound(0.0f, sr * (m_ambientStrength + diff * m_lightStrength * lr), 1.0f);
            const float fg = qBound(0.0f, sg * (m_ambientStrength + diff * m_lightStrength * lg), 1.0f);
            const float fb = qBound(0.0f, sb * (m_ambientStrength + diff * m_lightStrength * lb), 1.0f);

            ltLine[x] = qRgba(int(fr*255.0f+0.5f), int(fg*255.0f+0.5f), int(fb*255.0f+0.5f), qAlpha(sp));
        }
    }
}

// ---------------------------------------------------------------------------
// GL offscreen compositing
// ---------------------------------------------------------------------------

void NormalMapPaintCanvas::initGL()
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 2);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(0);
    fmt.setStencilBufferSize(0);

    m_glSurface = new QOffscreenSurface(nullptr, this);
    m_glSurface->setFormat(fmt);
    m_glSurface->create();
    if (!m_glSurface->isValid()) return;

    m_glCtx = new QOpenGLContext(this);
    m_glCtx->setFormat(fmt);
    if (!m_glCtx->create() || !m_glCtx->makeCurrent(m_glSurface)) return;

    static const char* vertSrc = R"(
#version 150 core
in  vec2 aPos;
out vec2 vUv;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUv = vec2(aPos.x * 0.5 + 0.5, 0.5 - aPos.y * 0.5);
}
)";
    static const char* fragSrc = R"(
#version 150 core
uniform sampler2D uSprite;
uniform sampler2D uNormal;
uniform vec2      uSpriteSize;
uniform vec2      uLightPos;
uniform float     uLightZ;
uniform vec3      uLightColor;
uniform float     uLightStrength;
uniform float     uAmbient;
in  vec2 vUv;
out vec4 fragColor;
void main() {
    vec4 sp = texture(uSprite, vUv);
    if (sp.a == 0.0) { fragColor = vec4(0.0); return; }
    vec4 nm = texture(uNormal, vUv);
    vec3 N = (nm.a > 0.0) ? normalize(nm.rgb * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0);
    vec2 pixelPos = vUv * uSpriteSize;
    vec3 L = normalize(vec3(uLightPos - pixelPos, uLightZ));
    float diff = max(dot(N, L), 0.0);
    vec3 lit = clamp(sp.rgb * (uAmbient + diff * uLightStrength * uLightColor), 0.0, 1.0);
    fragColor = vec4(lit, sp.a);
}
)";

    m_glProgram = new QOpenGLShaderProgram();
    if (!m_glProgram->addShaderFromSourceCode(QOpenGLShader::Vertex,   vertSrc) ||
        !m_glProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fragSrc) ||
        !m_glProgram->link()) {
        qWarning("NormalMapPaintCanvas: GL shader failed:\n%s",
                 qPrintable(m_glProgram->log()));
        delete m_glProgram; m_glProgram = nullptr;
        m_glCtx->doneCurrent();
        return;
    }

    static const float quad[] = { -1.f,-1.f,  1.f,-1.f,  -1.f,1.f,  1.f,1.f };

    m_glVao = new QOpenGLVertexArrayObject();
    m_glVao->create();
    m_glVao->bind();

    m_glQuadVbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    m_glQuadVbo->create();
    m_glQuadVbo->bind();
    m_glQuadVbo->setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_glQuadVbo->allocate(quad, sizeof(quad));

    const int posLoc = m_glProgram->attributeLocation("aPos");
    m_glProgram->enableAttributeArray(posLoc);
    m_glProgram->setAttributeBuffer(posLoc, GL_FLOAT, 0, 2, 2 * sizeof(float));

    m_glVao->release();
    m_glQuadVbo->release();
    m_glCtx->doneCurrent();
    m_glOk = true;
}

void NormalMapPaintCanvas::destroyGL()
{
    if (!m_glCtx || !m_glSurface) return;
    if (m_glCtx->makeCurrent(m_glSurface)) {
        delete m_glFbo;       m_glFbo       = nullptr;
        delete m_glSpriteTex; m_glSpriteTex = nullptr;
        delete m_glNormalTex; m_glNormalTex = nullptr;
        delete m_glVao;       m_glVao       = nullptr;
        delete m_glQuadVbo;   m_glQuadVbo   = nullptr;
        delete m_glProgram;   m_glProgram   = nullptr;
        m_glCtx->doneCurrent();
    }
    m_glOk = false;
}

void NormalMapPaintCanvas::recomputeLit()
{
    if (!m_glInitAttempted) {
        m_glInitAttempted = true;
        initGL();
    }
    if (m_glOk) recomputeLitGL();
    else         recomputeLitCPU();
}

void NormalMapPaintCanvas::recomputeLitGL()
{
    if (m_sprite.isNull() || m_compositeCache.isNull()) { m_litCache = QImage(); return; }
    if (!m_glCtx->makeCurrent(m_glSurface)) { recomputeLitCPU(); return; }

    auto uploadTex = [](QOpenGLTexture*& tex, const QImage& src) {
        const QImage rgba = src.convertToFormat(QImage::Format_RGBA8888);
        if (tex && tex->width() == rgba.width() && tex->height() == rgba.height()) {
            tex->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, rgba.constBits());
        } else {
            delete tex;
            tex = new QOpenGLTexture(rgba, QOpenGLTexture::DontGenerateMipMaps);
            tex->setMinificationFilter(QOpenGLTexture::Nearest);
            tex->setMagnificationFilter(QOpenGLTexture::Nearest);
            tex->setWrapMode(QOpenGLTexture::ClampToEdge);
        }
    };

    if (m_glSpriteDirty) { uploadTex(m_glSpriteTex, m_sprite);          m_glSpriteDirty = false; }
    if (m_glNormalDirty) { uploadTex(m_glNormalTex, m_compositeCache);   m_glNormalDirty = false; }

    if (!m_glSpriteTex || !m_glNormalTex) {
        m_glCtx->doneCurrent();
        recomputeLitCPU();
        return;
    }

    const int w = m_sprite.width(), h = m_sprite.height();
    if (!m_glFbo || m_glFbo->width() != w || m_glFbo->height() != h) {
        delete m_glFbo;
        m_glFbo = new QOpenGLFramebufferObject(w, h);
    }

    m_glFbo->bind();
    QOpenGLFunctions* f = m_glCtx->functions();
    f->glViewport(0, 0, w, h);
    f->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    f->glClear(GL_COLOR_BUFFER_BIT);

    m_glProgram->bind();
    m_glProgram->setUniformValue("uSprite",        0);
    m_glProgram->setUniformValue("uNormal",        1);
    m_glProgram->setUniformValue("uSpriteSize",    QVector2D(float(w), float(h)));
    m_glProgram->setUniformValue("uLightPos",      QVector2D(w * 0.5f + float(m_lightX),
                                                             h * 0.5f + float(m_lightY)));
    m_glProgram->setUniformValue("uLightZ",        float(m_lightZ));
    m_glProgram->setUniformValue("uLightColor",    QVector3D(float(m_lightColor.redF()),
                                                             float(m_lightColor.greenF()),
                                                             float(m_lightColor.blueF())));
    m_glProgram->setUniformValue("uLightStrength", m_lightStrength);
    m_glProgram->setUniformValue("uAmbient",       m_ambientStrength);

    f->glActiveTexture(GL_TEXTURE0); m_glSpriteTex->bind();
    f->glActiveTexture(GL_TEXTURE1); m_glNormalTex->bind();

    m_glVao->bind();
    f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_glVao->release();

    f->glActiveTexture(GL_TEXTURE1); m_glNormalTex->release();
    f->glActiveTexture(GL_TEXTURE0); m_glSpriteTex->release();
    m_glProgram->release();
    m_glFbo->release();

    m_litCache = m_glFbo->toImage();
    m_glCtx->doneCurrent();
}

// ---------------------------------------------------------------------------
// Drag-and-drop: import a normal map image
// ---------------------------------------------------------------------------

static bool isNormalMapDropExtension(const QString& path)
{
    static const QStringList exts = {
        "png","jpg","jpeg","bmp","tga","tif","tiff","webp"
    };
    return exts.contains(QFileInfo(path).suffix().toLower());
}

void NormalMapPaintCanvas::dragEnterEvent(QDragEnterEvent* event)
{
    if (!m_sprite.isNull() && event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile() && isNormalMapDropExtension(url.toLocalFile())) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    MapPaintCanvas::dragEnterEvent(event);
}

void NormalMapPaintCanvas::dragMoveEvent(QDragMoveEvent* event)
{
    if (!m_sprite.isNull() && event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile() && isNormalMapDropExtension(url.toLocalFile())) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    MapPaintCanvas::dragMoveEvent(event);
}

void NormalMapPaintCanvas::dropEvent(QDropEvent* event)
{
    if (!m_sprite.isNull() && event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (!url.isLocalFile()) continue;
            const QString path = url.toLocalFile();
            if (!isNormalMapDropExtension(path)) continue;
            const QImage img(path);
            if (img.isNull()) continue;
            const QImage loaded = (img.size() == m_sprite.size())
                ? img.convertToFormat(QImage::Format_ARGB32)
                : img.scaled(m_sprite.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                     .convertToFormat(QImage::Format_ARGB32);
            loadCompanion(loaded);
            event->acceptProposedAction();
            return;
        }
    }
    MapPaintCanvas::dropEvent(event);
}

// ---------------------------------------------------------------------------
// Layer management
// ---------------------------------------------------------------------------

void NormalMapPaintCanvas::syncActiveLayerToCache()
{
    if (m_activeLayerIdx >= 0 && m_activeLayerIdx < m_layers.size())
        m_layers[m_activeLayerIdx].image = m_companion.copy();
}

void NormalMapPaintCanvas::syncCompanionFromActiveLayer()
{
    if (m_activeLayerIdx >= 0 && m_activeLayerIdx < m_layers.size())
        m_companion = m_layers[m_activeLayerIdx].image.copy();
}

QImage NormalMapPaintCanvas::compositeLayers() const
{
    if (m_layers.isEmpty())
        return m_companion;

    const QSize sz = m_companion.isNull()
        ? m_layers[0].image.size()
        : m_companion.size();
    if (sz.isEmpty())
        return m_companion;

    QImage result(sz, QImage::Format_ARGB32);
    result.fill(qRgba(128, 128, 255, 0));  // transparent flat-normal base

    QPainter p(&result);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    for (int i = 0; i < m_layers.size(); ++i) {
        if (!m_layers[i].visible) continue;
        p.setOpacity(qBound(0.0, double(m_layers[i].opacity), 1.0));
        // Active layer: use m_companion which has the latest paint data
        const QImage& src = (i == m_activeLayerIdx && !m_companion.isNull())
                                ? m_companion : m_layers[i].image;
        if (!src.isNull())
            p.drawImage(QPoint(0, 0), src);
    }
    p.end();
    return result;
}

QImage NormalMapPaintCanvas::flattenedNormalMap() const
{
    return compositeLayers();
}

void NormalMapPaintCanvas::addLayer()
{
    if (m_sprite.isNull()) return;
    syncActiveLayerToCache();

    NormalLayer newLayer;
    newLayer.image = QImage(m_sprite.size(), QImage::Format_ARGB32);
    newLayer.image.fill(Qt::transparent);
    newLayer.name = tr("Layer %1").arg(m_layers.size() + 1);

    // Insert directly above the current active layer
    const int insertIdx = m_activeLayerIdx + 1;
    m_layers.insert(insertIdx, newLayer);
    m_activeLayerIdx = insertIdx;

    syncCompanionFromActiveLayer();
    m_glNormalDirty = true;
    m_modified = true;
    updateCompanionItem();
    emit layersChanged();
    emit companionModified();
}

void NormalMapPaintCanvas::removeLayer(int idx)
{
    if (m_layers.size() <= 1 || idx < 0 || idx >= m_layers.size()) return;
    syncActiveLayerToCache();

    m_layers.remove(idx);

    if (m_activeLayerIdx >= m_layers.size())
        m_activeLayerIdx = m_layers.size() - 1;

    syncCompanionFromActiveLayer();
    m_glNormalDirty = true;
    m_modified = true;
    updateCompanionItem();
    emit layersChanged();
    emit companionModified();
}

void NormalMapPaintCanvas::setActiveLayer(int idx)
{
    if (idx < 0 || idx >= m_layers.size() || idx == m_activeLayerIdx) return;
    syncActiveLayerToCache();
    m_activeLayerIdx = idx;
    syncCompanionFromActiveLayer();
    m_glNormalDirty = true;
    updateCompanionItem();
    emit layersChanged();
}

void NormalMapPaintCanvas::setLayerName(int idx, const QString& name)
{
    if (idx < 0 || idx >= m_layers.size()) return;
    m_layers[idx].name = name;
    emit layersChanged();
}

void NormalMapPaintCanvas::setLayerVisible(int idx, bool visible)
{
    if (idx < 0 || idx >= m_layers.size()) return;
    m_layers[idx].visible = visible;
    m_glNormalDirty = true;
    m_modified = true;
    updateCompanionItem();
    emit layersChanged();
    emit companionModified();
}

void NormalMapPaintCanvas::moveLayerUp(int idx)
{
    // "Up" in stack = higher draw order = toward end of m_layers vector
    if (idx < 0 || idx >= m_layers.size() - 1) return;
    syncActiveLayerToCache();
    m_layers.swapItemsAt(idx, idx + 1);
    if      (m_activeLayerIdx == idx)     m_activeLayerIdx = idx + 1;
    else if (m_activeLayerIdx == idx + 1) m_activeLayerIdx = idx;
    syncCompanionFromActiveLayer();
    m_glNormalDirty = true;
    m_modified = true;
    updateCompanionItem();
    emit layersChanged();
    emit companionModified();
}

void NormalMapPaintCanvas::moveLayerDown(int idx)
{
    // "Down" in stack = lower draw order = toward beginning of m_layers vector
    if (idx <= 0 || idx >= m_layers.size()) return;
    syncActiveLayerToCache();
    m_layers.swapItemsAt(idx, idx - 1);
    if      (m_activeLayerIdx == idx)     m_activeLayerIdx = idx - 1;
    else if (m_activeLayerIdx == idx - 1) m_activeLayerIdx = idx;
    syncCompanionFromActiveLayer();
    m_glNormalDirty = true;
    m_modified = true;
    updateCompanionItem();
    emit layersChanged();
    emit companionModified();
}

void NormalMapPaintCanvas::saveLayers(const QDir& dir, const QString& baseName)
{
    syncActiveLayerToCache();

    if (m_layers.size() <= 1) {
        // Remove any stale multi-layer metadata
        const QString metaPath = dir.filePath(baseName + QStringLiteral("_normal_layers.json"));
        QFile::remove(metaPath);
        return;
    }

    saveLayersJson(dir.filePath(baseName + QStringLiteral("_normal_layers.json")),
                   dir.absolutePath(), baseName);
}

void NormalMapPaintCanvas::saveLayersJson(
    const QString& jsonPath, const QString& dir, const QString& baseName) const
{
    QJsonArray arr;
    for (int i = 0; i < m_layers.size(); ++i) {
        const auto& l = m_layers[i];
        const QString fileName = baseName + QStringLiteral("_normal_layer_%1.png").arg(i);
        const QString fullPath = dir + QChar('/') + fileName;
        l.image.save(fullPath, "PNG");

        QJsonObject obj;
        obj[QStringLiteral("name")]    = l.name;
        obj[QStringLiteral("visible")] = l.visible;
        obj[QStringLiteral("opacity")] = double(l.opacity);
        obj[QStringLiteral("file")]    = fileName;
        arr.append(obj);
    }

    QJsonObject root;
    root[QStringLiteral("layers")] = arr;

    QFile f(jsonPath);
    if (f.open(QFile::WriteOnly))
        f.write(QJsonDocument(root).toJson());
}

void NormalMapPaintCanvas::loadLayersFromJson(
    const QString& jsonPath, const QDir& dir, const QString& baseName)
{
    Q_UNUSED(baseName)
    QFile f(jsonPath);
    if (!f.open(QFile::ReadOnly)) return;

    const auto doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray arr = doc.object()[QStringLiteral("layers")].toArray();
    if (arr.isEmpty()) return;

    m_layers.clear();
    for (const auto& v : arr) {
        const QJsonObject obj = v.toObject();
        NormalLayer layer;
        layer.name    = obj[QStringLiteral("name")].toString(tr("Layer"));
        layer.visible = obj[QStringLiteral("visible")].toBool(true);
        layer.opacity = float(obj[QStringLiteral("opacity")].toDouble(1.0));

        const QString file     = obj[QStringLiteral("file")].toString();
        const QString fullPath = dir.filePath(file);
        const QImage  img(fullPath);
        if (!img.isNull()) {
            layer.image = (img.size() == m_sprite.size())
                ? img.convertToFormat(QImage::Format_ARGB32)
                : img.scaled(m_sprite.size()).convertToFormat(QImage::Format_ARGB32);
        } else {
            layer.image = QImage(m_sprite.size(), QImage::Format_ARGB32);
            layer.image.fill(Qt::transparent);
        }
        m_layers.append(layer);
    }

    if (m_layers.isEmpty())
        m_activeLayerIdx = 0;
    else
        m_activeLayerIdx = qBound(0, m_activeLayerIdx, m_layers.size() - 1);
}
