#include "MapPaintCanvas.h"

#include <QContextMenuEvent>
#include <QCursor>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QShortcut>
#include <QTimer>
#include <cmath>

// ── SelectionOutlineItem ─────────────────────────────────────────────────────
// Draws an animated marching-ants outline along the boundary of the selection.
// Each border pixel is colored white or black based on (x + y + phase) % cycle,
// producing a diagonal-striped dashed line that marches on each phase increment.
class SelectionOutlineItem final : public QGraphicsItem {
public:
    explicit SelectionOutlineItem(QGraphicsItem* parent = nullptr)
        : QGraphicsItem(parent) {}

    void setMask(const QImage& mask) {
        prepareGeometryChange();
        m_border.clear();
        m_rect = QRectF();
        if (mask.isNull()) { update(); return; }
        m_rect = QRectF(0, 0, mask.width(), mask.height());
        const int w = mask.width();
        const int h = mask.height();
        for (int y = 0; y < h; ++y) {
            const uint8_t* row   = mask.constScanLine(y);
            const uint8_t* above = (y > 0)     ? mask.constScanLine(y - 1) : nullptr;
            const uint8_t* below = (y < h - 1) ? mask.constScanLine(y + 1) : nullptr;
            for (int x = 0; x < w; ++x) {
                if (row[x] < 128) continue;
                const bool onBorder =
                    (x == 0) || (x == w - 1) || (y == 0) || (y == h - 1) ||
                    row[x - 1] < 128 || row[x + 1] < 128 ||
                    (above && above[x] < 128) || (below && below[x] < 128);
                if (onBorder) m_border.append(QPoint(x, y));
            }
        }
        rebuildWhitesBlacks();
        update();
    }

    void setPhase(int phase) {
        m_phase = phase % (2 * kDashLen);
        rebuildWhitesBlacks();
        update();
    }

    QRectF boundingRect() const override { return m_rect; }

    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override {
        if (m_border.isEmpty()) return;
        p->save();
        p->setPen(QPen(Qt::white, 1));
        if (!m_whites.isEmpty()) p->drawPoints(m_whites.constData(), m_whites.size());
        p->setPen(QPen(Qt::black, 1));
        if (!m_blacks.isEmpty()) p->drawPoints(m_blacks.constData(), m_blacks.size());
        p->restore();
    }

private:
    void rebuildWhitesBlacks() {
        m_whites.clear();
        m_blacks.clear();
        for (const QPoint& pt : m_border) {
            if (((pt.x() + pt.y() + m_phase) % (2 * kDashLen)) < kDashLen)
                m_whites.append(pt);
            else
                m_blacks.append(pt);
        }
    }

    static constexpr int kDashLen = 6;
    QVector<QPoint> m_border;
    QVector<QPoint> m_whites;
    QVector<QPoint> m_blacks;
    QRectF          m_rect;
    int             m_phase = 0;
};

MapPaintCanvas::MapPaintCanvas(QWidget* parent)
    : ZoomableGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setBackgroundBrush(Qt::NoBrush);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);

    connect(this, &ZoomableGraphicsView::zoomChanged,
            this, [this](double) { updateCursor(); });

    m_selectionTimer = new QTimer(this);
    m_selectionTimer->setInterval(80);
    connect(m_selectionTimer, &QTimer::timeout, this, [this]() {
        if (m_marchingAntsItem) {
            ++m_selectionPhase;
            static_cast<SelectionOutlineItem*>(m_marchingAntsItem)->setPhase(m_selectionPhase);
        }
    });

    // Undo / redo shortcuts — WidgetWithChildrenShortcut takes priority over
    // the window-level project undo when this canvas has focus.
    auto makeShortcut = [this](QKeySequence seq) {
        return new QShortcut(seq, this, nullptr, nullptr,
                             Qt::WidgetWithChildrenShortcut);
    };
    connect(makeShortcut(QKeySequence::Undo), &QShortcut::activated,
            this, &MapPaintCanvas::undo);
    connect(makeShortcut(QKeySequence::Redo), &QShortcut::activated,
            this, &MapPaintCanvas::redo);
    // Ctrl+Shift+Z as alternate redo
    connect(makeShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)),
            &QShortcut::activated, this, &MapPaintCanvas::redo);
}

void MapPaintCanvas::loadSprite(const QString& path)
{
    m_sprite   = QImage();
    m_companion = QImage();
    m_modified  = false;
    m_painting  = false;
    m_undoHistory.clear();
    m_redoHistory.clear();

    m_scene->clear();
    m_spriteItem               = nullptr;
    m_companionItem            = nullptr;
    m_selectionOverlayItem     = nullptr;
    m_selectionInProgressItem  = nullptr;
    m_selectedShapeOverlayItem = nullptr;
    m_marchingAntsItem         = nullptr;
    m_curveState   = CurveState::Idle;
    m_curveDot1    = nullptr;
    m_curveDot2    = nullptr;
    m_curvePreview = nullptr;
    if (m_selectionTimer) m_selectionTimer->stop();

    // Reset selection state
    m_hasSelection = false;
    m_selecting    = false;
    m_selectionPoints.clear();
    m_selectionMask = QImage();
    m_shapes.clear();
    m_selectedShape = -1;
    m_hasDragged    = false;

    if (path.isEmpty()) {
        setSceneRect(QRectF());
        return;
    }

    m_sprite = QImage(path).convertToFormat(QImage::Format_ARGB32);
    if (m_sprite.isNull()) {
        setSceneRect(QRectF());
        return;
    }

    m_scene->setSceneRect(0, 0, m_sprite.width(), m_sprite.height());

    m_spriteItem = new QGraphicsPixmapItem(QPixmap::fromImage(m_sprite));
    m_spriteItem->setZValue(0);
    m_scene->addItem(m_spriteItem);

    // Start with a fully transparent companion
    m_companion = QImage(m_sprite.size(), QImage::Format_ARGB32);
    m_companion.fill(Qt::transparent);

    m_companionItem = new QGraphicsPixmapItem();
    m_companionItem->setZValue(1);
    m_companionItem->setOpacity(m_companionOpacity);
    m_scene->addItem(m_companionItem);

    // Selection overlay — dims area outside selection (z=2)
    m_selectionMask = QImage(m_sprite.size(), QImage::Format_Grayscale8);
    m_selectionMask.fill(0);
    m_selectionOverlayItem = new QGraphicsPixmapItem();
    m_selectionOverlayItem->setZValue(2);
    m_scene->addItem(m_selectionOverlayItem);

    // Marching-ants outline (z=2.5)
    auto* outline = new SelectionOutlineItem();
    outline->setZValue(2.5);
    m_scene->addItem(outline);
    m_marchingAntsItem = outline;

    // Selected shape highlight overlay (z=4)
    m_selectedShapeOverlayItem = new QGraphicsPixmapItem();
    m_selectedShapeOverlayItem->setZValue(4);
    m_scene->addItem(m_selectedShapeOverlayItem);

    updateCompanionItem();

    setZoomManual(false);
    initialFit();
    updateCursor();
}

void MapPaintCanvas::clearSprite()
{
    m_sprite    = QImage();
    m_companion = QImage();
    m_modified  = false;
    m_painting  = false;
    m_curveState   = CurveState::Idle;
    m_scene->clear();
    m_spriteItem               = nullptr;
    m_companionItem            = nullptr;
    m_selectionOverlayItem     = nullptr;
    m_selectionInProgressItem  = nullptr;
    m_selectedShapeOverlayItem = nullptr;
    m_marchingAntsItem         = nullptr;
    m_curveDot1    = nullptr;
    m_curveDot2    = nullptr;
    m_curvePreview = nullptr;
    if (m_selectionTimer) m_selectionTimer->stop();
    m_selectionMask  = QImage();
    m_hasSelection   = false;
    m_selecting      = false;
    m_selectionPoints.clear();
    m_shapes.clear();
    m_selectedShape = -1;
    setSceneRect(QRectF());
    updateCursor();
}

void MapPaintCanvas::loadCompanion(const QImage& img)
{
    if (m_sprite.isNull()) return;
    m_undoHistory.clear();
    m_redoHistory.clear();
    m_companion = img.convertToFormat(QImage::Format_ARGB32);
    if (m_companion.size() != m_sprite.size()) {
        m_companion = QImage(m_sprite.size(), QImage::Format_ARGB32);
        m_companion.fill(Qt::transparent);
    }
    m_modified = false;
    updateCompanionItem();
}

void MapPaintCanvas::setBrushSize(int px)
{
    m_brushSize = qMax(1, px);
    updateCursor();
}

void MapPaintCanvas::setBrushShape(BrushShape shape)
{
    m_brushShape = shape;
    updateCursor();
}

void MapPaintCanvas::setBrushHardness(float h)
{
    m_brushHardness = qBound(0.0f, h, 1.0f);
}

void MapPaintCanvas::setBlurStrength(float s)
{
    m_blurStrength = qBound(0.0f, s, 1.0f);
}

void MapPaintCanvas::setToolMode(Tool tool)
{
    if (tool != m_tool && m_curveState != CurveState::Idle) {
        clearCurveItems();
        m_curveState = CurveState::Idle;
    }
    m_tool = tool;
    updateCursor();
}

void MapPaintCanvas::setReadOnly(bool ro)
{
    m_readOnly = ro;
    updateCursor();
    viewport()->update();
}

void MapPaintCanvas::updateCursor()
{
    if (m_readOnly) {
        viewport()->setCursor(Qt::ForbiddenCursor);
        return;
    }
    if (m_sprite.isNull() || m_canvasMode == CanvasMode::Select ||
            m_tool == Tool::Fill || m_tool == Tool::Curve) {
        viewport()->setCursor(Qt::CrossCursor);
        return;
    }

    const int screenSize = qBound(3, static_cast<int>(m_brushSize * zoom()), 120);
    const int pad = 6;
    const int pix = screenSize + pad;
    const int off = pad / 2;

    QPixmap cursor(pix, pix);
    cursor.fill(Qt::transparent);
    QPainter p(&cursor);
    p.setRenderHint(QPainter::Antialiasing, m_brushShape == BrushShape::Circle);

    const QRectF r(off, off, screenSize, screenSize);
    auto drawShape = [&](const QPen& pen) {
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        if (m_brushShape == BrushShape::Circle)
            p.drawEllipse(r);
        else
            p.drawRect(r);
    };
    drawShape(QPen(Qt::white, 3.0));
    drawShape(QPen(Qt::black, 1.0));

    // Crosshair at centre
    const int cx = pix / 2, cy = pix / 2;
    auto drawCross = [&](const QPen& pen) {
        p.setPen(pen);
        p.drawLine(cx - 3, cy, cx + 3, cy);
        p.drawLine(cx, cy - 3, cx, cy + 3);
    };
    drawCross(QPen(Qt::white, 3.0));
    drawCross(QPen(Qt::black, 1.0));
    p.end();

    viewport()->setCursor(QCursor(cursor, cx, cy));
}

void MapPaintCanvas::setPaintColor(const QColor& color)
{
    m_paintColor = color;
}

void MapPaintCanvas::clearCompanion()
{
    if (m_companion.isNull()) return;
    pushUndoSnapshot();
    m_companion.fill(Qt::transparent);
    m_modified = true;
    updateCompanionItem();
    emit companionModified();
}

void MapPaintCanvas::setCanvasMode(CanvasMode mode)
{
    m_canvasMode = mode;
    updateCursor();
    if (mode == CanvasMode::Paint) {
        // Cancel any in-progress selection when switching back to paint
        clearInProgressItem();
        m_selecting = false;
        m_selectionPoints.clear();
    }
}

void MapPaintCanvas::setSelectionTool(SelectionTool tool)
{
    m_selectionTool = tool;
}

void MapPaintCanvas::clearSelection()
{
    if (m_hasSelection) pushUndoSnapshot();
    m_shapes.clear();
    m_selectedShape = -1;
    m_hasSelection = false;
    if (!m_selectionMask.isNull())
        m_selectionMask.fill(0);
    clearInProgressItem();
    m_selecting = false;
    m_selectionPoints.clear();
    updateSelectionOverlay();
    updateSelectionOutline();
    updateSelectedShapeOverlay();
    emit selectionChanged();
}

QRect MapPaintCanvas::selectionBoundingRect() const
{
    if (!m_hasSelection || m_selectionMask.isNull()) return {};
    const int w = m_selectionMask.width();
    const int h = m_selectionMask.height();
    int minX = w, maxX = -1, minY = h, maxY = -1;
    for (int y = 0; y < h; ++y) {
        const uchar* row = m_selectionMask.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            if (row[x] >= 128) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    if (maxX < 0) return {};
    return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

void MapPaintCanvas::setCompanionOpacity(qreal opacity)
{
    m_companionOpacity = opacity;
    if (m_companionItem)
        m_companionItem->setOpacity(opacity);
}

// ---------------------------------------------------------------------------
// Undo / Redo
// ---------------------------------------------------------------------------

void MapPaintCanvas::pushUndoSnapshot()
{
    UndoState s;
    s.companion     = m_companion.copy();
    s.selectionMask = m_selectionMask.copy();
    s.hasSelection  = m_hasSelection;
    s.shapes        = m_shapes;
    s.selectedShape = m_selectedShape;
    m_undoHistory.append(std::move(s));
    if (m_undoHistory.size() > kMaxUndoSteps)
        m_undoHistory.removeFirst();
    m_redoHistory.clear();
}

void MapPaintCanvas::undo()
{
    if (m_undoHistory.isEmpty()) return;

    UndoState cur;
    cur.companion     = m_companion.copy();
    cur.selectionMask = m_selectionMask.copy();
    cur.hasSelection  = m_hasSelection;
    cur.shapes        = m_shapes;
    cur.selectedShape = m_selectedShape;
    m_redoHistory.append(std::move(cur));

    UndoState s = m_undoHistory.takeLast();
    m_companion     = std::move(s.companion);
    m_selectionMask = std::move(s.selectionMask);
    m_hasSelection  = s.hasSelection;
    m_shapes        = std::move(s.shapes);
    m_selectedShape = s.selectedShape;

    m_modified = true;
    updateCompanionItem();
    updateSelectionOverlay();
    updateSelectionOutline();
    updateSelectedShapeOverlay();
    emit companionModified();
    emit selectionChanged();
}

void MapPaintCanvas::redo()
{
    if (m_redoHistory.isEmpty()) return;

    UndoState cur;
    cur.companion     = m_companion.copy();
    cur.selectionMask = m_selectionMask.copy();
    cur.hasSelection  = m_hasSelection;
    cur.shapes        = m_shapes;
    cur.selectedShape = m_selectedShape;
    m_undoHistory.append(std::move(cur));

    UndoState s = m_redoHistory.takeLast();
    m_companion     = std::move(s.companion);
    m_selectionMask = std::move(s.selectionMask);
    m_hasSelection  = s.hasSelection;
    m_shapes        = std::move(s.shapes);
    m_selectedShape = s.selectedShape;

    m_modified = true;
    updateCompanionItem();
    updateSelectionOverlay();
    updateSelectionOutline();
    updateSelectedShapeOverlay();
    emit companionModified();
    emit selectionChanged();
}

void MapPaintCanvas::performFill()
{
    if (m_companion.isNull() || m_sprite.isNull()) return;
    pushUndoSnapshot();

    const int w = m_companion.width();
    const int h = m_companion.height();

    for (int y = 0; y < h; ++y) {
        const QRgb* spriteLine    = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(y));
        QRgb*       companionLine = reinterpret_cast<QRgb*>(m_companion.scanLine(y));
        const uchar* maskLine     = m_hasSelection ? m_selectionMask.constScanLine(y) : nullptr;

        for (int x = 0; x < w; ++x) {
            if (qAlpha(spriteLine[x]) == 0) continue;
            float selWeight = 1.0f;
            if (maskLine) {
                const int mv = maskLine[x];
                if (mv == 0) continue;
                selWeight = mv / 255.0f;
            }
            if (selWeight >= 1.0f) {
                companionLine[x] = m_paintColor.rgba();
            } else {
                const QRgb ex = companionLine[x];
                const float w0 = 1.0f - selWeight;
                companionLine[x] = qRgba(
                    qBound(0, int(qRed(ex)  *w0 + m_paintColor.red()  *selWeight), 255),
                    qBound(0, int(qGreen(ex)*w0 + m_paintColor.green()*selWeight), 255),
                    qBound(0, int(qBlue(ex) *w0 + m_paintColor.blue() *selWeight), 255),
                    qBound(0, int(qAlpha(ex)*w0 + m_paintColor.alpha()*selWeight), 255));
            }
        }
    }

    m_modified = true;
    updateCompanionItem();
    emit companionModified();
}

void MapPaintCanvas::setRectSelection(const QRect& rect)
{
    if (m_selectionMask.isNull() || !rect.isValid()) return;
    commitRectangleSelection(rect, Qt::NoModifier);
}

void MapPaintCanvas::setCompanionVisible(bool visible)
{
    if (m_companionItem)
        m_companionItem->setVisible(visible);
}

void MapPaintCanvas::updateCompanionItem()
{
    if (!m_companionItem || m_companion.isNull()) return;
    m_companionItem->setPixmap(QPixmap::fromImage(m_companion));
}

// ---------------------------------------------------------------------------
// Background: checkerboard
// ---------------------------------------------------------------------------

void MapPaintCanvas::drawBackground(QPainter* painter, const QRectF& rect)
{
    const int  cs = 8;
    const QColor c1(200, 200, 200);
    const QColor c2(255, 255, 255);

    const int left   = static_cast<int>(std::floor(rect.left()))   - cs;
    const int top    = static_cast<int>(std::floor(rect.top()))    - cs;
    const int right  = static_cast<int>(std::ceil(rect.right()))   + cs;
    const int bottom = static_cast<int>(std::ceil(rect.bottom()))  + cs;

    // Snap to grid
    const int startX = left  - ((left  % cs + cs) % cs);
    const int startY = top   - ((top   % cs + cs) % cs);

    for (int y = startY; y < bottom; y += cs) {
        for (int x = startX; x < right; x += cs) {
            const int gx = (x / cs + 256) % 2;
            const int gy = (y / cs + 256) % 2;
            painter->fillRect(QRect(x, y, cs, cs), ((gx + gy) % 2 == 0) ? c1 : c2);
        }
    }
}

void MapPaintCanvas::drawForeground(QPainter* painter, const QRectF& rect)
{
    if (m_readOnly)
        painter->fillRect(rect, QColor(0, 0, 0, 70));
}

// ---------------------------------------------------------------------------
// Mouse events — painting + selection
// ---------------------------------------------------------------------------

void MapPaintCanvas::mousePressEvent(QMouseEvent* event)
{
    if (m_readOnly) { ZoomableGraphicsView::mousePressEvent(event); return; }

    // --- Select mode ---
    if (m_canvasMode == CanvasMode::Select && !m_spacePressed && !m_sprite.isNull()) {
        if (event->button() == Qt::LeftButton) {
            const QPoint scenePos = mapToScene(event->pos()).toPoint();

            if (m_selectionTool == SelectionTool::MagicWand) {
                magicWandFill(scenePos, event->modifiers());
                event->accept();
                return;
            }

            // Polygon: subsequent clicks add vertices while already selecting
            if (m_selectionTool == SelectionTool::Polygon && m_selecting) {
                m_selectionPoints.append(scenePos);
                updateInProgressItem(scenePos);
                event->accept();
                return;
            }

            // Start a new selection
            m_selecting  = true;
            m_hasDragged = false;
            m_selectionStart = scenePos;
            m_selectionPoints.clear();
            if (m_selectionTool == SelectionTool::Polygon ||
                m_selectionTool == SelectionTool::Freehand) {
                m_selectionPoints.append(scenePos);
            }
            createInProgressItem();
            event->accept();
            return;
        }

        // RMB with ≥3 polygon vertices: commit
        if (event->button() == Qt::RightButton &&
            m_selectionTool == SelectionTool::Polygon &&
            m_selecting && m_selectionPoints.size() >= 3) {
            commitPolygonSelection(event->modifiers());
            clearInProgressItem();
            m_selecting = false;
            event->accept();
            return;
        }
    }

    // --- Fill tool ---
    if (m_canvasMode == CanvasMode::Paint &&
        m_tool == Tool::Fill &&
        event->button() == Qt::LeftButton &&
        !m_spacePressed &&
        !m_sprite.isNull()) {
        setFocus(Qt::MouseFocusReason);
        performFill();
        event->accept();
        return;
    }

    // --- Curve tool ---
    if (m_canvasMode == CanvasMode::Paint &&
        m_tool == Tool::Curve &&
        !m_spacePressed &&
        !m_sprite.isNull()) {

        const QPointF sp = mapToScene(event->pos());
        setFocus(Qt::MouseFocusReason);

        if (event->button() == Qt::LeftButton) {
            if (m_curveState == CurveState::Idle) {
                m_curveP1    = sp;
                m_curveState = CurveState::AwaitingP2;
                if (!m_curveDot1 && m_scene) {
                    m_curveDot1 = new QGraphicsEllipseItem(-3, -3, 6, 6);
                    m_curveDot1->setBrush(Qt::white);
                    m_curveDot1->setPen(QPen(Qt::black, 1));
                    m_curveDot1->setZValue(11);
                    m_scene->addItem(m_curveDot1);
                }
                if (m_curveDot1) m_curveDot1->setPos(m_curveP1);
                if (!m_curvePreview && m_scene) {
                    m_curvePreview = new QGraphicsPathItem();
                    m_curvePreview->setPen(QPen(Qt::white, 1, Qt::DashLine));
                    m_curvePreview->setZValue(11);
                    m_scene->addItem(m_curvePreview);
                }
            } else if (m_curveState == CurveState::AwaitingP2) {
                m_curveP2    = sp;
                m_curveState = CurveState::Curving;
                if (!m_curveDot2 && m_scene) {
                    m_curveDot2 = new QGraphicsEllipseItem(-3, -3, 6, 6);
                    m_curveDot2->setBrush(Qt::white);
                    m_curveDot2->setPen(QPen(Qt::black, 1));
                    m_curveDot2->setZValue(11);
                    m_scene->addItem(m_curveDot2);
                }
                if (m_curveDot2) m_curveDot2->setPos(m_curveP2);
                updateCurvePreview(sp);
            } else { // Curving: commit with cursor as control point
                pushUndoSnapshot();
                paintCurveStroke(m_curveP1, m_curveP2, sp);
                m_modified = true;
                updateCompanionItem();
                emit companionModified();
                clearCurveItems();
                m_curveState = CurveState::Idle;
            }
            event->accept();
            return;
        }

        if (event->button() == Qt::RightButton) {
            if (m_curveState == CurveState::Curving) {
                // RMB commits a straight line (control = midpoint)
                const QPointF mid = (m_curveP1 + m_curveP2) * 0.5;
                pushUndoSnapshot();
                paintCurveStroke(m_curveP1, m_curveP2, mid);
                m_modified = true;
                updateCompanionItem();
                emit companionModified();
            }
            clearCurveItems();
            m_curveState = CurveState::Idle;
            event->accept();
            return;
        }
    }

    // --- Paint mode (pencil / eraser) ---
    if (m_canvasMode == CanvasMode::Paint &&
        m_tool != Tool::Fill &&
        m_tool != Tool::Curve &&
        event->button() == Qt::LeftButton &&
        !m_spacePressed &&
        !(event->modifiers() & Qt::ShiftModifier) &&
        !m_sprite.isNull()) {
        setFocus(Qt::MouseFocusReason);
        m_painting = true;
        pushUndoSnapshot();
        const QPoint scenePos = mapToScene(event->pos()).toPoint();
        m_lastPaintPos = scenePos;
        paintBrushAt(scenePos);
        m_modified = true;
        updateCompanionItem();
        emit companionModified();
        event->accept();
        return;
    }

    ZoomableGraphicsView::mousePressEvent(event);
}

void MapPaintCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_readOnly) { ZoomableGraphicsView::mouseMoveEvent(event); return; }

    if (m_canvasMode == CanvasMode::Select && m_selecting) {
        const QPoint scenePos = mapToScene(event->pos()).toPoint();
        if (!m_hasDragged && (scenePos - m_selectionStart).manhattanLength() >= 5)
            m_hasDragged = true;
        if (m_selectionTool == SelectionTool::Freehand) {
            const QPoint& last = m_selectionPoints.last();
            if ((scenePos - last).manhattanLength() >= 2)
                m_selectionPoints.append(scenePos);
        }
        updateInProgressItem(scenePos);
        event->accept();
        return;
    }

    if (m_painting) {
        const QPoint scenePos = mapToScene(event->pos()).toPoint();
        drawStroke(m_lastPaintPos, scenePos);
        m_lastPaintPos = scenePos;
        event->accept();
        return;
    }
    if (m_tool == Tool::Curve && m_curveState != CurveState::Idle)
        updateCurvePreview(mapToScene(event->pos()));
    ZoomableGraphicsView::mouseMoveEvent(event);
}

void MapPaintCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_readOnly) { ZoomableGraphicsView::mouseReleaseEvent(event); return; }

    if (m_canvasMode == CanvasMode::Select && m_selecting &&
        event->button() == Qt::LeftButton) {
        const QPoint scenePos = mapToScene(event->pos()).toPoint();
        switch (m_selectionTool) {
            case SelectionTool::Rectangle:
                if (m_hasDragged)
                    commitRectangleSelection(QRect(m_selectionStart, scenePos).normalized(),
                                             event->modifiers());
                else
                    trySelectShapeAt(scenePos);
                clearInProgressItem();
                m_selecting = false;
                break;
            case SelectionTool::Ellipse:
                if (m_hasDragged)
                    commitEllipseSelection(QRect(m_selectionStart, scenePos).normalized(),
                                            event->modifiers());
                else
                    trySelectShapeAt(scenePos);
                clearInProgressItem();
                m_selecting = false;
                break;
            case SelectionTool::Freehand:
                if (m_hasDragged && !m_selectionPoints.isEmpty())
                    commitFreehandSelection(event->modifiers());
                else if (!m_hasDragged)
                    trySelectShapeAt(scenePos);
                clearInProgressItem();
                m_selecting = false;
                break;
            case SelectionTool::Polygon:
                // Committed by double-click or RMB; stay in selecting state
                break;
            default:
                m_selecting = false;
                break;
        }
        event->accept();
        return;
    }

    if (m_painting && event->button() == Qt::LeftButton) {
        m_painting = false;
        event->accept();
        return;
    }
    ZoomableGraphicsView::mouseReleaseEvent(event);
}

void MapPaintCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_canvasMode == CanvasMode::Select &&
        event->button() == Qt::LeftButton &&
        m_selectionTool == SelectionTool::Polygon &&
        m_selecting) {
        // The second press of the double-click already appended a point via
        // mousePressEvent; pop it before committing.
        if (!m_selectionPoints.isEmpty())
            m_selectionPoints.removeLast();
        if (m_selectionPoints.size() >= 3)
            commitPolygonSelection(event->modifiers());
        clearInProgressItem();
        m_selecting = false;
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void MapPaintCanvas::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
        m_canvasMode == CanvasMode::Select && m_selectedShape >= 0) {
        removeSelectedShape();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (m_curveState != CurveState::Idle) {
            clearCurveItems();
            m_curveState = CurveState::Idle;
            event->accept();
            return;
        }
        if (m_selecting) {
            clearInProgressItem();
            m_selectionPoints.clear();
            m_selecting = false;
        } else if (m_hasSelection) {
            clearSelection();
        }
        event->accept();
        return;
    }
    ZoomableGraphicsView::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// Curve tool helpers
// ---------------------------------------------------------------------------

void MapPaintCanvas::clearCurveItems()
{
    if (m_curveDot1 && m_scene) {
        m_scene->removeItem(m_curveDot1); delete m_curveDot1; m_curveDot1 = nullptr;
    }
    if (m_curveDot2 && m_scene) {
        m_scene->removeItem(m_curveDot2); delete m_curveDot2; m_curveDot2 = nullptr;
    }
    if (m_curvePreview && m_scene) {
        m_scene->removeItem(m_curvePreview); delete m_curvePreview; m_curvePreview = nullptr;
    }
}

void MapPaintCanvas::updateCurvePreview(const QPointF& cursor)
{
    if (!m_curvePreview) return;
    QPainterPath path;
    if (m_curveState == CurveState::AwaitingP2) {
        path.moveTo(m_curveP1);
        path.lineTo(cursor);
    } else if (m_curveState == CurveState::Curving) {
        path.moveTo(m_curveP1);
        path.quadTo(cursor, m_curveP2);
    }
    m_curvePreview->setPath(path);
}

void MapPaintCanvas::paintCurveStroke(
    const QPointF& p1, const QPointF& p2, const QPointF& ctrl)
{
    // Estimate the curve length to determine sampling density.
    const float dx       = float(p2.x() - p1.x());
    const float dy       = float(p2.y() - p1.y());
    const float chord    = std::sqrt(dx * dx + dy * dy);
    const float midX     = (p1.x() + p2.x()) * 0.5f;
    const float midY     = (p1.y() + p2.y()) * 0.5f;
    const float devX     = float(ctrl.x()) - midX;
    const float devY     = float(ctrl.y()) - midY;
    const float deviation = std::sqrt(devX * devX + devY * devY);
    const int   steps    = qMax(1, int(chord + deviation));

    paintBrushAt(p1.toPoint());
    for (int i = 1; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        const float u = 1.0f - t;
        const QPoint pt(
            int(std::round(u*u * float(p1.x()) + 2.0f*u*t * float(ctrl.x()) + t*t * float(p2.x()))),
            int(std::round(u*u * float(p1.y()) + 2.0f*u*t * float(ctrl.y()) + t*t * float(p2.y()))));
        paintBrushAt(pt);
    }
}

// ---------------------------------------------------------------------------
// Stroke drawing
// ---------------------------------------------------------------------------

void MapPaintCanvas::drawStroke(QPoint from, QPoint to)
{
    // Bresenham's line
    int dx = std::abs(to.x() - from.x());
    int dy = std::abs(to.y() - from.y());
    int x  = from.x();
    int y  = from.y();
    const int sx = (to.x() > from.x()) ? 1 : -1;
    const int sy = (to.y() > from.y()) ? 1 : -1;

    if (dx >= dy) {
        int err = dx / 2;
        while (x != to.x()) {
            paintBrushAt(QPoint(x, y));
            err -= dy;
            if (err < 0) { y += sy; err += dx; }
            x += sx;
        }
    } else {
        int err = dy / 2;
        while (y != to.y()) {
            paintBrushAt(QPoint(x, y));
            err -= dx;
            if (err < 0) { x += sx; err += dy; }
            y += sy;
        }
    }
    paintBrushAt(to);

    m_modified = true;
    updateCompanionItem();
    emit companionModified();
}

void MapPaintCanvas::paintBrushAt(QPoint center)
{
    if (m_companion.isNull() || m_sprite.isNull()) return;

    if (m_tool == Tool::Blend) { paintBlendAt(center); return; }

    const int   half = m_brushSize / 2;
    const int   w    = m_companion.width();
    const int   h    = m_companion.height();
    const bool  hard = (m_brushHardness >= 1.0f) || (half == 0);
    const float fhalf = (half > 0) ? float(half) : 1.0f;

    for (int dy = -half; dy <= half; ++dy) {
        const int py = center.y() + dy;
        if (py < 0 || py >= h) continue;

        const QRgb* spriteLine    = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(py));
        QRgb*       companionLine = reinterpret_cast<QRgb*>(m_companion.scanLine(py));

        for (int dx = -half; dx <= half; ++dx) {
            const int px = center.x() + dx;
            if (px < 0 || px >= w) continue;
            if (qAlpha(spriteLine[px]) == 0) continue;
            float selWeight = 1.0f;
            if (m_hasSelection) {
                const int mv = m_selectionMask.constScanLine(py)[px];
                if (mv == 0) continue;
                selWeight = mv / 255.0f;
            }

            // Compute per-pixel weight based on brush hardness
            float weight;
            if (hard) {
                if (m_brushShape == BrushShape::Circle) {
                    const int r2 = half * half;
                    if (dx * dx + dy * dy > r2 + half) continue;
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
                // Linear falloff: solid core [0, hardness], fade to 0 at rim [hardness, 1]
                weight = (t <= m_brushHardness)
                    ? 1.0f
                    : (1.0f - t) / (1.0f - m_brushHardness);
                weight = qBound(0.0f, weight, 1.0f);
            }

            weight *= selWeight;
            if (weight <= 0.0f) continue;

            const QRgb existing = companionLine[px];

            if (m_tool == Tool::Eraser) {
                if (weight >= 1.0f)
                    companionLine[px] = qRgba(0, 0, 0, 0);
                else
                    companionLine[px] = qRgba(qRed(existing), qGreen(existing), qBlue(existing),
                                               int(qAlpha(existing) * (1.0f - weight)));
            } else if (m_tool == Tool::Smudge) {
                // Sample from the same (dx,dy) offset at the previous brush centre.
                // m_lastPaintPos holds that position: set on press and updated after
                // each drawStroke call, so it stays stable for the whole stroke segment.
                const int srcX = m_lastPaintPos.x() + dx;
                const int srcY = m_lastPaintPos.y() + dy;
                if (srcX < 0 || srcX >= w || srcY < 0 || srcY >= h) continue;
                const QRgb* srcSLine = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(srcY));
                if (qAlpha(srcSLine[srcX]) == 0) continue;
                const QRgb src = reinterpret_cast<const QRgb*>(m_companion.constScanLine(srcY))[srcX];
                const float sw = weight * m_blurStrength;
                const float w0 = 1.0f - sw;
                companionLine[px] = qRgba(
                    qBound(0, int(qRed(existing)   * w0 + qRed(src)   * sw), 255),
                    qBound(0, int(qGreen(existing) * w0 + qGreen(src) * sw), 255),
                    qBound(0, int(qBlue(existing)  * w0 + qBlue(src)  * sw), 255),
                    qBound(0, int(qAlpha(existing) * w0 + qAlpha(src) * sw), 255));
            } else if (m_tool == Tool::Blur) {
                const int kr = qMax(1, m_brushSize / 4);
                float tr = 0, tg = 0, tb = 0, ta = 0; int cnt = 0;
                for (int ky = -kr; ky <= kr; ++ky) {
                    const int ry = py + ky;
                    if (ry < 0 || ry >= h) continue;
                    const QRgb* kSLine = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(ry));
                    const QRgb* kCLine = reinterpret_cast<const QRgb*>(m_companion.constScanLine(ry));
                    for (int kx = -kr; kx <= kr; ++kx) {
                        const int rx = px + kx;
                        if (rx < 0 || rx >= w) continue;
                        if (qAlpha(kSLine[rx]) == 0) continue;
                        tr += qRed(kCLine[rx]); tg += qGreen(kCLine[rx]);
                        tb += qBlue(kCLine[rx]); ta += qAlpha(kCLine[rx]);
                        ++cnt;
                    }
                }
                if (cnt > 0) {
                    const float bw = weight * m_blurStrength;
                    const float w0 = 1.0f - bw;
                    companionLine[px] = qRgba(
                        qBound(0, int(qRed(existing)  *w0 + (tr/cnt)*bw), 255),
                        qBound(0, int(qGreen(existing)*w0 + (tg/cnt)*bw), 255),
                        qBound(0, int(qBlue(existing) *w0 + (tb/cnt)*bw), 255),
                        qBound(0, int(qAlpha(existing)*w0 + (ta/cnt)*bw), 255));
                }
            } else {
                // Pencil
                if (weight >= 1.0f)
                    companionLine[px] = m_paintColor.rgba();
                else {
                    const float w0 = 1.0f - weight;
                    companionLine[px] = qRgba(
                        qBound(0, int(qRed(existing)  *w0 + m_paintColor.red()  *weight), 255),
                        qBound(0, int(qGreen(existing)*w0 + m_paintColor.green()*weight), 255),
                        qBound(0, int(qBlue(existing) *w0 + m_paintColor.blue() *weight), 255),
                        qBound(0, int(qAlpha(existing)*w0 + m_paintColor.alpha()*weight), 255));
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Selection — Gaussian feather (3-pass box blur)
// ---------------------------------------------------------------------------

void MapPaintCanvas::featherMask(QImage& mask, int radius)
{
    if (radius <= 0 || mask.isNull()) return;
    const int w = mask.width();
    const int h = mask.height();
    if (w == 0 || h == 0) return;

    const int r = qMax(1, radius / 3);
    const float inv = 1.0f / float(2 * r + 1);

    QVector<float> buf(w * h);
    QVector<float> tmp(w * h);

    for (int y = 0; y < h; ++y) {
        const uchar* row = mask.constScanLine(y);
        for (int x = 0; x < w; ++x)
            buf[y * w + x] = float(row[x]);
    }

    for (int pass = 0; pass < 3; ++pass) {
        // Horizontal
        for (int y = 0; y < h; ++y) {
            const float* s = buf.data() + y * w;
            float*       d = tmp.data() + y * w;
            float sum = 0;
            for (int x = -r; x <= r; ++x)
                sum += s[qBound(0, x, w - 1)];
            for (int x = 0; x < w; ++x) {
                d[x] = sum * inv;
                sum -= s[qBound(0, x - r,     w - 1)];
                sum += s[qBound(0, x + r + 1, w - 1)];
            }
        }
        // Vertical
        for (int x = 0; x < w; ++x) {
            float sum = 0;
            for (int y = -r; y <= r; ++y)
                sum += tmp[qBound(0, y, h - 1) * w + x];
            for (int y = 0; y < h; ++y) {
                buf[y * w + x] = sum * inv;
                sum -= tmp[qBound(0, y - r,     h - 1) * w + x];
                sum += tmp[qBound(0, y + r + 1, h - 1) * w + x];
            }
        }
    }

    for (int y = 0; y < h; ++y) {
        uchar* row = mask.scanLine(y);
        for (int x = 0; x < w; ++x)
            row[x] = uchar(qBound(0.0f, buf[y * w + x], 255.0f));
    }
}

// ---------------------------------------------------------------------------
// Selection — mask commit helpers
// ---------------------------------------------------------------------------

void MapPaintCanvas::applyMaskWithModifier(QImage newMask, Qt::KeyboardModifiers mods)
{
    if (m_selectionMask.isNull()) return;
    pushUndoSnapshot();

    if (m_selectionFeather > 0)
        featherMask(newMask, m_selectionFeather);

    SelectionOp effectiveOp;
    if (m_selectionOp == SelectionOp::Add) {
        effectiveOp = SelectionOp::Add;
    } else if (m_selectionOp == SelectionOp::Subtract) {
        effectiveOp = SelectionOp::Subtract;
    } else {
        // Replace mode: honour keyboard modifiers
        if (mods & Qt::ShiftModifier)
            effectiveOp = SelectionOp::Add;
        else if ((mods & Qt::ControlModifier) || (mods & Qt::AltModifier))
            effectiveOp = SelectionOp::Subtract;
        else {
            m_shapes.clear();  // Replace clears prior shapes
            effectiveOp = SelectionOp::Add;
        }
    }

    m_shapes.append(ShapeRecord{newMask, effectiveOp});
    m_selectedShape = m_shapes.size() - 1;
    recomputeSelectionFromShapes();
    updateSelectedShapeOverlay();
}

void MapPaintCanvas::commitRectangleSelection(QRect rect, Qt::KeyboardModifiers mods)
{
    if (m_selectionMask.isNull()) return;
    QImage newMask(m_sprite.size(), QImage::Format_Grayscale8);
    newMask.fill(0);
    QPainter p(&newMask);
    p.setBrush(Qt::white);
    p.setPen(Qt::NoPen);
    p.drawRect(rect);
    p.end();
    applyMaskWithModifier(newMask, mods);
}

void MapPaintCanvas::commitEllipseSelection(QRect rect, Qt::KeyboardModifiers mods)
{
    if (m_selectionMask.isNull()) return;
    QImage newMask(m_sprite.size(), QImage::Format_Grayscale8);
    newMask.fill(0);
    QPainter p(&newMask);
    p.setBrush(Qt::white);
    p.setPen(Qt::NoPen);
    p.drawEllipse(rect);
    p.end();
    applyMaskWithModifier(newMask, mods);
}

void MapPaintCanvas::commitPolygonSelection(Qt::KeyboardModifiers mods)
{
    if (m_selectionMask.isNull() || m_selectionPoints.size() < 3) return;
    QImage newMask(m_sprite.size(), QImage::Format_Grayscale8);
    newMask.fill(0);
    QVector<QPoint> pts = m_selectionPoints;
    QPainter p(&newMask);
    p.setBrush(Qt::white);
    p.setPen(Qt::NoPen);
    p.drawPolygon(pts.data(), pts.size());
    p.end();
    applyMaskWithModifier(newMask, mods);
}

void MapPaintCanvas::commitFreehandSelection(Qt::KeyboardModifiers mods)
{
    if (m_selectionMask.isNull() || m_selectionPoints.size() < 3) return;
    QImage newMask(m_sprite.size(), QImage::Format_Grayscale8);
    newMask.fill(0);
    QPainterPath path;
    path.moveTo(m_selectionPoints.first());
    for (int i = 1; i < m_selectionPoints.size(); ++i)
        path.lineTo(m_selectionPoints[i]);
    path.closeSubpath();
    QPainter p(&newMask);
    p.setBrush(Qt::white);
    p.setPen(Qt::NoPen);
    p.drawPath(path);
    p.end();

    // Intersect with sprite alpha — only select opaque sprite pixels
    const int w = m_sprite.width();
    const int h = m_sprite.height();
    for (int y = 0; y < h; ++y) {
        const QRgb* spriteLine = reinterpret_cast<const QRgb*>(m_sprite.constScanLine(y));
        uchar* maskLine = newMask.scanLine(y);
        for (int x = 0; x < w; ++x)
            if (qAlpha(spriteLine[x]) == 0) maskLine[x] = 0;
    }

    applyMaskWithModifier(newMask, mods);
}

void MapPaintCanvas::magicWandFill(QPoint seed, Qt::KeyboardModifiers mods)
{
    if (m_companion.isNull() || m_sprite.isNull()) return;

    const int w = m_sprite.width();
    const int h = m_sprite.height();
    if (seed.x() < 0 || seed.x() >= w || seed.y() < 0 || seed.y() >= h) return;

    QImage newMask(m_sprite.size(), QImage::Format_Grayscale8);
    newMask.fill(0);

    // Sample from the sprite so the wand selects by sprite colour, not companion
    QImage comp = m_sprite.convertToFormat(QImage::Format_ARGB32);
    const QRgb seedPix = reinterpret_cast<const QRgb*>(comp.constScanLine(seed.y()))[seed.x()];
    if (qAlpha(seedPix) == 0) return;   // don't start from a transparent pixel
    const int sr = qRed(seedPix);
    const int sg = qGreen(seedPix);
    const int sb = qBlue(seedPix);
    const int sa = qAlpha(seedPix);

    QVector<bool> visited(w * h, false);
    QVector<QPoint> queue;
    queue.reserve(w * h / 4);
    queue.append(seed);
    visited[seed.y() * w + seed.x()] = true;

    const int dx4[] = {-1, 1, 0,  0};
    const int dy4[] = { 0, 0, -1, 1};

    while (!queue.isEmpty()) {
        const QPoint cur = queue.takeLast();
        const int cx = cur.x();
        const int cy = cur.y();
        newMask.scanLine(cy)[cx] = 255;

        for (int i = 0; i < 4; ++i) {
            const int nx = cx + dx4[i];
            const int ny = cy + dy4[i];
            if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
            if (visited[ny * w + nx]) continue;
            visited[ny * w + nx] = true;

            const QRgb pix = reinterpret_cast<const QRgb*>(comp.constScanLine(ny))[nx];
            if (qAlpha(pix) == 0) continue;  // never select transparent sprite pixels
            const int maxDiff = std::max({qAbs(qRed(pix) - sr),
                                          qAbs(qGreen(pix) - sg),
                                          qAbs(qBlue(pix) - sb),
                                          qAbs(qAlpha(pix) - sa)});
            if (maxDiff <= m_magicWandTolerance) {
                queue.append(QPoint(nx, ny));
            }
        }
    }

    applyMaskWithModifier(newMask, mods);
}

void MapPaintCanvas::setSelectionOverlayColor(const QColor& color)
{
    m_selectionOverlayColor = color;
    updateSelectionOverlay();
}

// ---------------------------------------------------------------------------
// Selection — overlay rendering
// ---------------------------------------------------------------------------

void MapPaintCanvas::updateSelectionOverlay()
{
    if (!m_selectionOverlayItem || m_selectionMask.isNull()) return;

    if (!m_hasSelection) {
        m_selectionOverlayItem->setPixmap(QPixmap());
        return;
    }

    const int w = m_selectionMask.width();
    const int h = m_selectionMask.height();

    // Dim the area OUTSIDE the selection; leave the selected area clear.
    QImage overlay(w, h, QImage::Format_ARGB32);
    overlay.fill(Qt::transparent);

    for (int y = 0; y < h; ++y) {
        const uint8_t* maskLine    = m_selectionMask.constScanLine(y);
        QRgb*          overlayLine = reinterpret_cast<QRgb*>(overlay.scanLine(y));
        for (int x = 0; x < w; ++x) {
            if (maskLine[x] < 128)
                overlayLine[x] = qRgba(0, 0, 0, 100);
        }
    }

    m_selectionOverlayItem->setPixmap(QPixmap::fromImage(overlay));
}

void MapPaintCanvas::updateSelectionOutline()
{
    if (!m_marchingAntsItem) return;
    auto* outline = static_cast<SelectionOutlineItem*>(m_marchingAntsItem);
    outline->setMask(m_hasSelection ? m_selectionMask : QImage());
    if (m_hasSelection) {
        if (m_selectionTimer && !m_selectionTimer->isActive())
            m_selectionTimer->start();
    } else {
        if (m_selectionTimer) m_selectionTimer->stop();
    }
}

// ---------------------------------------------------------------------------
// Selection — in-progress item management
// ---------------------------------------------------------------------------

void MapPaintCanvas::createInProgressItem()
{
    clearInProgressItem();

    const QPen dashed = (m_selectionOp == SelectionOp::Subtract)
        ? QPen(Qt::red, 1, Qt::DashLine)
        : QPen(Qt::white, 1, Qt::DashLine);

    switch (m_selectionTool) {
        case SelectionTool::Rectangle: {
            auto* item = new QGraphicsRectItem(QRectF(m_selectionStart, m_selectionStart));
            item->setPen(dashed);
            item->setBrush(Qt::NoBrush);
            item->setZValue(3);
            m_scene->addItem(item);
            m_selectionInProgressItem = item;
            break;
        }
        case SelectionTool::Ellipse: {
            auto* item = new QGraphicsEllipseItem(QRectF(m_selectionStart, m_selectionStart));
            item->setPen(dashed);
            item->setBrush(Qt::NoBrush);
            item->setZValue(3);
            m_scene->addItem(item);
            m_selectionInProgressItem = item;
            break;
        }
        case SelectionTool::Polygon:
        case SelectionTool::Freehand: {
            auto* item = new QGraphicsPathItem();
            item->setPen(dashed);
            item->setBrush(Qt::NoBrush);
            item->setZValue(3);
            m_scene->addItem(item);
            m_selectionInProgressItem = item;
            break;
        }
        default:
            break;
    }
}

void MapPaintCanvas::clearInProgressItem()
{
    if (m_selectionInProgressItem) {
        m_scene->removeItem(m_selectionInProgressItem);
        delete m_selectionInProgressItem;
        m_selectionInProgressItem = nullptr;
    }
}

void MapPaintCanvas::updateInProgressItem(QPoint cursorPos)
{
    if (!m_selectionInProgressItem) return;

    switch (m_selectionTool) {
        case SelectionTool::Rectangle: {
            auto* item = static_cast<QGraphicsRectItem*>(m_selectionInProgressItem);
            item->setRect(QRectF(QRect(m_selectionStart, cursorPos).normalized()));
            break;
        }
        case SelectionTool::Ellipse: {
            auto* item = static_cast<QGraphicsEllipseItem*>(m_selectionInProgressItem);
            item->setRect(QRectF(QRect(m_selectionStart, cursorPos).normalized()));
            break;
        }
        case SelectionTool::Polygon:
        case SelectionTool::Freehand: {
            auto* item = static_cast<QGraphicsPathItem*>(m_selectionInProgressItem);
            QPainterPath path;
            if (!m_selectionPoints.isEmpty()) {
                path.moveTo(m_selectionPoints.first());
                for (int i = 1; i < m_selectionPoints.size(); ++i)
                    path.lineTo(m_selectionPoints[i]);
                path.lineTo(cursorPos);
            }
            item->setPath(path);
            break;
        }
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Shape collection
// ---------------------------------------------------------------------------

void MapPaintCanvas::recomputeSelectionFromShapes()
{
    if (m_selectionMask.isNull()) return;

    m_selectionMask.fill(0);
    const int w = m_selectionMask.width();
    const int h = m_selectionMask.height();

    // Pass 1: Add shapes — union of all positive regions.
    for (const ShapeRecord& rec : m_shapes) {
        if (rec.op == SelectionOp::Subtract) continue;
        for (int y = 0; y < h; ++y) {
            uint8_t*       dst = m_selectionMask.scanLine(y);
            const uint8_t* src = rec.mask.constScanLine(y);
            for (int x = 0; x < w; ++x)
                dst[x] = qMax(dst[x], src[x]);
        }
    }

    // Pass 2: Subtract shapes — always override Add regions regardless of draw order.
    for (const ShapeRecord& rec : m_shapes) {
        if (rec.op != SelectionOp::Subtract) continue;
        for (int y = 0; y < h; ++y) {
            uint8_t*       dst = m_selectionMask.scanLine(y);
            const uint8_t* src = rec.mask.constScanLine(y);
            for (int x = 0; x < w; ++x)
                if (src[x] >= 128) dst[x] = 0;
        }
    }

    m_hasSelection = !m_shapes.isEmpty();
    updateSelectionOverlay();
    updateSelectionOutline();
    emit selectionChanged();
}

int MapPaintCanvas::hitTestShapes(QPoint scenePos) const
{
    // Test from top (last added) to bottom so topmost shape wins.
    // Mode filter: Add op → only Add shapes are pickable; Subtract op → only Subtract shapes.
    for (int i = m_shapes.size() - 1; i >= 0; --i) {
        if (m_selectionOp == SelectionOp::Add      && m_shapes[i].op == SelectionOp::Subtract) continue;
        if (m_selectionOp == SelectionOp::Subtract && m_shapes[i].op != SelectionOp::Subtract) continue;
        const QImage& mask = m_shapes[i].mask;
        if (scenePos.x() < 0 || scenePos.y() < 0 ||
            scenePos.x() >= mask.width() || scenePos.y() >= mask.height())
            continue;
        if (mask.constScanLine(scenePos.y())[scenePos.x()] >= 128)
            return i;
    }
    return -1;
}

void MapPaintCanvas::updateSelectedShapeOverlay()
{
    if (!m_selectedShapeOverlayItem || m_selectionMask.isNull()) return;

    if (m_shapes.isEmpty()) {
        m_selectedShapeOverlayItem->setPixmap(QPixmap());
        return;
    }

    const int w = m_selectionMask.width();
    const int h = m_selectionMask.height();
    QImage overlay(w, h, QImage::Format_ARGB32);
    overlay.fill(Qt::transparent);

    // Pass 1: all non-selected subtract shapes — always visible in dim red
    for (int i = 0; i < m_shapes.size(); ++i) {
        if (m_shapes[i].op != SelectionOp::Subtract || i == m_selectedShape) continue;
        const QImage& mask = m_shapes[i].mask;
        for (int y = 0; y < h; ++y) {
            const uint8_t* ml = mask.constScanLine(y);
            QRgb*          ol = reinterpret_cast<QRgb*>(overlay.scanLine(y));
            for (int x = 0; x < w; ++x)
                if (ml[x] > 0)
                    ol[x] = qRgba(220, 55, 55, qBound(0, int(60 * ml[x] / 255), 255));
        }
    }

    m_selectedShapeOverlayItem->setPixmap(QPixmap::fromImage(overlay));
}

bool MapPaintCanvas::trySelectShapeAt(QPoint scenePos)
{
    const int hit = hitTestShapes(scenePos);
    if (hit < 0) return false;
    m_selectedShape = hit;
    updateSelectedShapeOverlay();
    emit selectionChanged();
    return true;
}

void MapPaintCanvas::removeSelectedShape()
{
    if (m_selectedShape < 0 || m_selectedShape >= m_shapes.size()) return;
    pushUndoSnapshot();
    m_shapes.removeAt(m_selectedShape);
    if (m_shapes.isEmpty())
        m_selectedShape = -1;
    else
        m_selectedShape = qMin(m_selectedShape, m_shapes.size() - 1);
    recomputeSelectionFromShapes();
    updateSelectedShapeOverlay();
}

void MapPaintCanvas::setShapes(const QVector<ShapeRecord>& shapes)
{
    m_shapes        = shapes;
    m_selectedShape = m_shapes.isEmpty() ? -1 : m_shapes.size() - 1;
    recomputeSelectionFromShapes();
    updateSelectedShapeOverlay();
}

// ---------------------------------------------------------------------------
// Selection context menu
// ---------------------------------------------------------------------------

void MapPaintCanvas::contextMenuEvent(QContextMenuEvent* event)
{
    if (!m_hasSelection || m_selecting) {
        QGraphicsView::contextMenuEvent(event);
        return;
    }

    QMenu menu(this);
    menu.addAction("Deselect",  this, &MapPaintCanvas::clearSelection);
    menu.addAction("Invert",    this, &MapPaintCanvas::invertSelection);
    menu.addSeparator();
    QAction* shrinkAct = menu.addAction("Shrink...");
    QAction* growAct   = menu.addAction("Grow...");

    connect(shrinkAct, &QAction::triggered, this, [this]() {
        bool ok = false;
        const int px = QInputDialog::getInt(this, "Shrink Selection",
                                             "Pixels:", 1, 1, 500, 1, &ok);
        if (ok) shrinkSelection(px);
    });
    connect(growAct, &QAction::triggered, this, [this]() {
        bool ok = false;
        const int px = QInputDialog::getInt(this, "Grow Selection",
                                             "Pixels:", 1, 1, 500, 1, &ok);
        if (ok) growSelection(px);
    });

    menu.exec(event->globalPos());
    event->accept();
}

// ---------------------------------------------------------------------------
// Selection: invert / grow / shrink helpers
// ---------------------------------------------------------------------------

// Replaces the shape list with a single flat mask and re-checks hasSelection.
static void replaceWithFlatMask(QImage mask,
                                QVector<MapPaintCanvas::ShapeRecord>& shapes,
                                int& selectedShape, bool& hasSelection,
                                QImage& selectionMask)
{
    bool any = false;
    const int w = mask.width(), h = mask.height();
    for (int y = 0; y < h && !any; ++y) {
        const uint8_t* row = mask.constScanLine(y);
        for (int x = 0; x < w && !any; ++x)
            if (row[x] >= 128) any = true;
    }
    selectionMask = mask;
    shapes.clear();
    hasSelection  = any;
    selectedShape = -1;
    if (any) {
        shapes.append(MapPaintCanvas::ShapeRecord{std::move(mask),
                                                   MapPaintCanvas::SelectionOp::Add});
        selectedShape = 0;
    }
}

void MapPaintCanvas::invertSelection()
{
    if (m_selectionMask.isNull()) return;
    pushUndoSnapshot();

    QImage inv = m_selectionMask.copy();
    const int w = inv.width(), h = inv.height();
    for (int y = 0; y < h; ++y) {
        uint8_t* row = inv.scanLine(y);
        for (int x = 0; x < w; ++x)
            row[x] = 255 - row[x];
    }

    replaceWithFlatMask(std::move(inv), m_shapes, m_selectedShape,
                        m_hasSelection, m_selectionMask);
    updateSelectionOverlay();
    updateSelectionOutline();
    updateSelectedShapeOverlay();
    emit selectionChanged();
}

// Separable box dilation (grow).
static QImage dilateGrayscale(const QImage& src, int r)
{
    const int w = src.width(), h = src.height();
    QImage tmp(w, h, QImage::Format_Grayscale8);
    QImage dst(w, h, QImage::Format_Grayscale8);

    for (int y = 0; y < h; ++y) {
        const uint8_t* s = src.constScanLine(y);
        uint8_t* d = tmp.scanLine(y);
        for (int x = 0; x < w; ++x) {
            uint8_t v = 0;
            for (int dx = -r; dx <= r; ++dx)
                v = qMax(v, s[qBound(0, x + dx, w - 1)]);
            d[x] = v;
        }
    }
    for (int y = 0; y < h; ++y) {
        uint8_t* d = dst.scanLine(y);
        for (int x = 0; x < w; ++x) {
            uint8_t v = 0;
            for (int dy = -r; dy <= r; ++dy)
                v = qMax(v, tmp.constScanLine(qBound(0, y + dy, h - 1))[x]);
            d[x] = v;
        }
    }
    return dst;
}

// Separable box erosion (shrink).
static QImage erodeGrayscale(const QImage& src, int r)
{
    const int w = src.width(), h = src.height();
    QImage tmp(w, h, QImage::Format_Grayscale8);
    QImage dst(w, h, QImage::Format_Grayscale8);

    for (int y = 0; y < h; ++y) {
        const uint8_t* s = src.constScanLine(y);
        uint8_t* d = tmp.scanLine(y);
        for (int x = 0; x < w; ++x) {
            uint8_t v = 255;
            for (int dx = -r; dx <= r; ++dx)
                v = qMin(v, s[qBound(0, x + dx, w - 1)]);
            d[x] = v;
        }
    }
    for (int y = 0; y < h; ++y) {
        uint8_t* d = dst.scanLine(y);
        for (int x = 0; x < w; ++x) {
            uint8_t v = 255;
            for (int dy = -r; dy <= r; ++dy)
                v = qMin(v, tmp.constScanLine(qBound(0, y + dy, h - 1))[x]);
            d[x] = v;
        }
    }
    return dst;
}

void MapPaintCanvas::growSelection(int px)
{
    if (m_selectionMask.isNull() || !m_hasSelection || px <= 0) return;
    pushUndoSnapshot();
    QImage grown = dilateGrayscale(m_selectionMask, px);
    replaceWithFlatMask(std::move(grown), m_shapes, m_selectedShape,
                        m_hasSelection, m_selectionMask);
    updateSelectionOverlay();
    updateSelectionOutline();
    updateSelectedShapeOverlay();
    emit selectionChanged();
}

void MapPaintCanvas::shrinkSelection(int px)
{
    if (m_selectionMask.isNull() || !m_hasSelection || px <= 0) return;
    pushUndoSnapshot();
    QImage shrunk = erodeGrayscale(m_selectionMask, px);
    replaceWithFlatMask(std::move(shrunk), m_shapes, m_selectedShape,
                        m_hasSelection, m_selectionMask);
    updateSelectionOverlay();
    updateSelectionOutline();
    updateSelectedShapeOverlay();
    emit selectionChanged();
}

// ---------------------------------------------------------------------------
// Blend tool — smooth transition between two adjacent flat-color areas
// ---------------------------------------------------------------------------

void MapPaintCanvas::paintBlendAt(QPoint center)
{
    if (m_companion.isNull() || m_sprite.isNull()) return;

    const int   half  = m_brushSize / 2;
    const int   imgW  = m_companion.width();
    const int   imgH  = m_companion.height();
    const bool  hard  = (m_brushHardness >= 1.0f) || (half == 0);
    const float fhalf = (half > 0) ? float(half) : 1.0f;

    // Phase 1: collect valid pixels with their brush weights
    struct PxInfo { int px, py; float weight; };
    QVector<PxInfo> pixels;
    pixels.reserve((2 * half + 1) * (2 * half + 1));

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
            pixels.append({px, py, weight});
        }
    }

    const int N = pixels.size();
    if (N < 6) return;

    // Phase 2: read current colors
    QVector<float> pr(N), pg(N), pb(N);
    for (int i = 0; i < N; ++i) {
        const QRgb c = reinterpret_cast<const QRgb*>(
            m_companion.constScanLine(pixels[i].py))[pixels[i].px];
        pr[i] = float(qRed(c));
        pg[i] = float(qGreen(c));
        pb[i] = float(qBlue(c));
    }

    // Phase 3: k-means k=2 (farthest-point init: C0 = pixel[0], C1 = farthest from C0)
    float c0r = pr[0], c0g = pg[0], c0b = pb[0];
    float c1r, c1g, c1b;
    {
        float maxD2 = -1.0f; int c1Idx = 0;
        for (int i = 1; i < N; ++i) {
            const float d2 = (pr[i]-c0r)*(pr[i]-c0r)
                           + (pg[i]-c0g)*(pg[i]-c0g)
                           + (pb[i]-c0b)*(pb[i]-c0b);
            if (d2 > maxD2) { maxD2 = d2; c1Idx = i; }
        }
        if (maxD2 < 1.0f) return;  // All pixels identical — nothing to blend
        c1r = pr[c1Idx]; c1g = pg[c1Idx]; c1b = pb[c1Idx];
    }

    QVector<int> assign(N, 0);
    for (int iter = 0; iter < 10; ++iter) {
        for (int i = 0; i < N; ++i) {
            const float d0 = (pr[i]-c0r)*(pr[i]-c0r) + (pg[i]-c0g)*(pg[i]-c0g) + (pb[i]-c0b)*(pb[i]-c0b);
            const float d1 = (pr[i]-c1r)*(pr[i]-c1r) + (pg[i]-c1g)*(pg[i]-c1g) + (pb[i]-c1b)*(pb[i]-c1b);
            assign[i] = (d1 < d0) ? 1 : 0;
        }
        float s0r=0,s0g=0,s0b=0,cnt0=0;
        float s1r=0,s1g=0,s1b=0,cnt1=0;
        for (int i = 0; i < N; ++i) {
            if (assign[i] == 0) { s0r+=pr[i]; s0g+=pg[i]; s0b+=pb[i]; cnt0+=1.0f; }
            else                { s1r+=pr[i]; s1g+=pg[i]; s1b+=pb[i]; cnt1+=1.0f; }
        }
        if (cnt0 > 0.0f) { c0r=s0r/cnt0; c0g=s0g/cnt0; c0b=s0b/cnt0; }
        if (cnt1 > 0.0f) { c1r=s1r/cnt1; c1g=s1g/cnt1; c1b=s1b/cnt1; }
    }

    // Phase 4: Homogeneity checks
    const float dR = c1r - c0r, dG = c1g - c0g, dB = c1b - c0b;
    const float interDist = std::sqrt(dR*dR + dG*dG + dB*dB);
    // Bail if the two cluster centers are too similar (only one effective color)
    if (interDist < 15.0f) return;

    // Bimodality check: fraction of pixels clearly belonging to one cluster.
    // If the area already has a smooth gradient, many pixels will be
    // equidistant from both centroids (t ≈ 0.5) and this fraction will be low.
    int bimodalCount = 0;
    for (int i = 0; i < N; ++i) {
        const float d0 = std::sqrt((pr[i]-c0r)*(pr[i]-c0r) + (pg[i]-c0g)*(pg[i]-c0g) + (pb[i]-c0b)*(pb[i]-c0b));
        const float d1 = std::sqrt((pr[i]-c1r)*(pr[i]-c1r) + (pg[i]-c1g)*(pg[i]-c1g) + (pb[i]-c1b)*(pb[i]-c1b));
        const float t  = d0 / (d0 + d1 + 1e-6f);  // 0 = at C0, 1 = at C1
        if (t < 0.25f || t > 0.75f) ++bimodalCount;
    }
    if (float(bimodalCount) / float(N) < 0.6f) return;  // Already a transition

    // Phase 5: Spatial centroids to determine blend direction
    float cx0=0.0f, cy0=0.0f, spatN0=0.0f;
    float cx1=0.0f, cy1=0.0f, spatN1=0.0f;
    for (int i = 0; i < N; ++i) {
        if (assign[i] == 0) { cx0 += float(pixels[i].px); cy0 += float(pixels[i].py); spatN0 += 1.0f; }
        else                { cx1 += float(pixels[i].px); cy1 += float(pixels[i].py); spatN1 += 1.0f; }
    }
    if (spatN0 <= 0.0f || spatN1 <= 0.0f) return;
    cx0 /= spatN0; cy0 /= spatN0;
    cx1 /= spatN1; cy1 /= spatN1;

    const float bdx   = cx1 - cx0;
    const float bdy   = cy1 - cy0;
    const float bdLen = std::sqrt(bdx*bdx + bdy*bdy);
    if (bdLen < 1.0f) return;  // Spatial centroids coincide
    const float bdNx  = bdx / bdLen;
    const float bdNy  = bdy / bdLen;
    const float midX  = (cx0 + cx1) * 0.5f;
    const float midY  = (cy0 + cy1) * 0.5f;
    const float blendHW = bdLen * 0.5f;  // half-width of blend zone

    // Phase 6: Write smooth transition
    for (const PxInfo& pxInfo : pixels) {
        // Project pixel position onto blend axis, normalised to [-1, 1]
        const float projX = float(pxInfo.px) - midX;
        const float projY = float(pxInfo.py) - midY;
        float proj = projX * bdNx + projY * bdNy;
        float t = proj / (blendHW + 1e-6f);
        t = qBound(-1.0f, t, 1.0f);
        t = (t + 1.0f) * 0.5f;             // remap to [0, 1]
        t = t * t * (3.0f - 2.0f * t);     // smoothstep

        const int tR = qBound(0, int(c0r + (c1r - c0r) * t + 0.5f), 255);
        const int tG = qBound(0, int(c0g + (c1g - c0g) * t + 0.5f), 255);
        const int tB = qBound(0, int(c0b + (c1b - c0b) * t + 0.5f), 255);

        QRgb* compLine = reinterpret_cast<QRgb*>(m_companion.scanLine(pxInfo.py));
        const QRgb existing = compLine[pxInfo.px];
        const float bw  = pxInfo.weight;
        const float w0f = 1.0f - bw;
        compLine[pxInfo.px] = qRgba(
            qBound(0, int(float(qRed(existing))   * w0f + float(tR) * bw + 0.5f), 255),
            qBound(0, int(float(qGreen(existing)) * w0f + float(tG) * bw + 0.5f), 255),
            qBound(0, int(float(qBlue(existing))  * w0f + float(tB) * bw + 0.5f), 255),
            qAlpha(existing));
    }
}
