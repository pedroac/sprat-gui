#include "EditorOverlayItem.h"
#include <QPainter>
#include <QFontMetrics>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneHoverEvent>
#include <QCursor>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QtMath>
#include <QMenu>
#include <QAction>
#include <QCoreApplication>

namespace {
QString trOverlay(const char* text) {
    return QCoreApplication::translate("EditorOverlayItem", text);
}

inline double distSq(const QPointF& a, const QPointF& b) {
    double dx = a.x() - b.x();
    double dy = a.y() - b.y();
    return dx * dx + dy * dy;
}

inline double dist(const QPointF& a, const QPointF& b) {
    return std::sqrt(distSq(a, b));
}
}

EditorOverlayItem::EditorOverlayItem(QGraphicsItem* parent) : QGraphicsObject(parent) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setZValue(1000);
}

void EditorOverlayItem::setSprites(const QList<SpritePtr>& sprites) {
    prepareGeometryChange();
    m_sprites = sprites;
    update();
}

void EditorOverlayItem::setSceneSize(const QSize& size) {
    prepareGeometryChange();
    m_sceneSize = size;
    update();
}

void EditorOverlayItem::setSelectedMarker(const QString& name) {
    if (m_selectedMarkerName != name) {
        m_selectedMarkerName = name;
        m_selectedVertexIndex = -1;
    }
    update();
}

QRectF EditorOverlayItem::boundingRect() const {
    if (m_sprites.isEmpty()) {
        return QRectF();
    }
    
    double minX = 0;
    double minY = 0;
    double maxX = m_sceneSize.width();
    double maxY = m_sceneSize.height();
    
    for (const auto& sprite : m_sprites) {
        minX = qMin(minX, (double)sprite->pivotX);
        maxX = qMax(maxX, (double)sprite->pivotX);
        minY = qMin(minY, (double)sprite->pivotY);
        maxY = qMax(maxY, (double)sprite->pivotY);

        for (const auto& p : sprite->points) {
            minX = qMin(minX, (double)p.x);
            maxX = qMax(maxX, (double)p.x);
            minY = qMin(minY, (double)p.y);
            maxY = qMax(maxY, (double)p.y);
            
            if (p.kind == MarkerKind::Circle) {
                 minX = qMin(minX, (double)p.x - p.radius);
                 maxX = qMax(maxX, (double)p.x + p.radius);
                 minY = qMin(minY, (double)p.y - p.radius);
                 maxY = qMax(maxY, (double)p.y + p.radius);
            } else if (p.kind == MarkerKind::Rectangle) {
                 maxX = qMax(maxX, (double)p.x + p.w);
                 maxY = qMax(maxY, (double)p.y + p.h);
            } else if (p.kind == MarkerKind::Polygon) {
                 for (const auto& pt : p.polygonPoints) {
                     minX = qMin(minX, (double)pt.x());
                     maxX = qMax(maxX, (double)pt.x());
                     minY = qMin(minY, (double)pt.y());
                     maxY = qMax(maxY, (double)pt.y());
                 }
            }
        }
    }
    
    double padding = 100.0; 
    return QRectF(minX - padding, minY - padding, maxX - minX + 2*padding, maxY - minY + 2*padding);
}

void EditorOverlayItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    if (m_sprites.isEmpty()) {
        return;
    }

    bool pivotSelected = m_selectedMarkerName.isEmpty();

    // 1. Unselected Markers
    drawMarkers(painter, false);

    // 2. Pivot (if not selected)
    if (!pivotSelected) {
        for (const auto& sprite : m_sprites) {
            double px = m_draggingPivot ? m_pivotDragFloat.x() : (double)sprite->pivotX;
            double py = m_draggingPivot ? m_pivotDragFloat.y() : (double)sprite->pivotY;
            drawPivot(painter, px, py);
        }
    }

    // 3. Selected Marker
    if (!pivotSelected) {
        drawMarkers(painter, true);
    }

    // 4. Pivot (if selected)
    if (pivotSelected) {
        for (const auto& sprite : m_sprites) {
            double px = m_draggingPivot ? m_pivotDragFloat.x() : (double)sprite->pivotX;
            double py = m_draggingPivot ? m_pivotDragFloat.y() : (double)sprite->pivotY;
            drawPivot(painter, px, py);
        }
    }
}

void EditorOverlayItem::drawPivot(QPainter* painter, double x, double y) {
    static const QPen kPivotOutlinePen = []() {
        QPen p(Qt::black, 5);
        p.setCosmetic(true);
        return p;
    }();
    static const QPen kPivotPen = []() {
        QPen p(QColor(255, 215, 0), 2);
        p.setCosmetic(true);
        return p;
    }();

    double scale = getScale();
    double len = 10 * scale;

    painter->setPen(kPivotOutlinePen);
    painter->drawLine(QPointF(x - len, y), QPointF(x + len, y));
    painter->drawLine(QPointF(x, y - len), QPointF(x, y + len));

    painter->setPen(kPivotPen);
    painter->drawLine(QPointF(x - len, y), QPointF(x + len, y));
    painter->drawLine(QPointF(x, y - len), QPointF(x, y + len));
}

double EditorOverlayItem::getScale() const {
    if (scene() && !scene()->views().isEmpty()) {
        return 1.0 / scene()->views().first()->transform().m11();
    }
    return 1.0;
}

NamedPoint* EditorOverlayItem::getNamedPoint(const QString& name) {
    if (m_sprites.isEmpty()) {
        return nullptr;
    }
    for (auto& p : m_sprites.last()->points) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

double EditorOverlayItem::distancePointToSegment(const QPointF& p, const QPointF& a, const QPointF& b) const {
    QPointF ab = b - a;
    QPointF ap = p - a;
    double ab_len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (ab_len2 <= 0.00001) return dist(p, a);
    double t = qMax(0.0, qMin(1.0, (ap.x() * ab.x() + ap.y() * ab.y()) / ab_len2));
    QPointF closest = a + ab * t;
    return dist(p, closest);
}

int EditorOverlayItem::getPolygonHitEdge(const NamedPoint* p, const QPointF& pos) const {
    if (p->kind != MarkerKind::Polygon || p->polygonPoints.size() < 2) return -1;
    double scale = getScale();
    double threshold = 8.0 * scale;
    for (int i = 0; i < p->polygonPoints.size(); ++i) {
        QPointF p1 = p->polygonPoints[i];
        QPointF p2 = p->polygonPoints[(i + 1) % p->polygonPoints.size()];
        if (distancePointToSegment(pos, p1, p2) <= threshold) return i;
    }
    return -1;
}

void EditorOverlayItem::drawMarkers(QPainter* painter, bool drawSelected) {
    static const QPen kSelectedPen = []() {
        QPen p(Qt::green, 2);
        p.setCosmetic(true);
        return p;
    }();
    static const QPen kUnselectedPen = []() {
        QPen p(Qt::cyan, 2);
        p.setCosmetic(true);
        return p;
    }();
    static const QColor kSelectedFill(0, 128, 0, 60);   // green alpha 60
    static const QColor kUnselectedFill(0, 255, 255, 30); // cyan alpha 30
    static const QColor kVertexHighlight(255, 50, 50);
    static const QColor kChipBg(0, 0, 0, 160);
    static const QFont kLabelFont = []() {
        QFont f;
        f.setPixelSize(11);
        return f;
    }();
    static const QFontMetrics kLabelFm(kLabelFont);

    if (m_sprites.isEmpty()) return;
    double scale = getScale();

    QPainterPath markerPath; // reused across iterations

    for (const auto& sprite : m_sprites) {
        for (int i = 0; i < sprite->points.size(); ++i) {
            const auto& p = sprite->points[i];
            bool selected = (p.name == m_selectedMarkerName);
            if (selected != drawSelected) continue;

            markerPath.clear();

            // Compute effective float coordinates for smooth drag rendering.
            // During drag the float members track sub-pixel position; we draw
            // from them so the visual stays smooth and only snaps on release.
            bool useFloat = selected && m_dragMode != None && m_dragTargetName == p.name;
            double ex = p.x, ey = p.y;
            double eRadius = p.radius;
            double ew = p.w, eh = p.h;
            bool useFloatPoly = false;

            if (useFloat) {
                if (m_dragMode == Point || m_dragMode == RectMove) {
                    ex = m_markerDragFloatDelta.x(); ey = m_markerDragFloatDelta.y();
                } else if (m_dragMode == CircleRadius) {
                    eRadius = m_markerDragFloatRadius;
                } else if (m_dragMode == RectResize) {
                    ex = m_markerDragFloatRect.x(); ey = m_markerDragFloatRect.y();
                    ew = m_markerDragFloatRect.width(); eh = m_markerDragFloatRect.height();
                } else if ((m_dragMode == PolyVertex || m_dragMode == PolyMove)
                           && !m_markerDragFloatPoly.isEmpty()) {
                    useFloatPoly = true;
                }
            }

            if (p.kind == MarkerKind::Point) {
                double len = 6 * scale;
                markerPath.moveTo(ex - len, ey - len); markerPath.lineTo(ex + len, ey + len);
                markerPath.moveTo(ex + len, ey - len); markerPath.lineTo(ex - len, ey + len);
            } else if (p.kind == MarkerKind::Circle) {
                markerPath.addEllipse(QPointF(ex, ey), eRadius, eRadius);
            } else if (p.kind == MarkerKind::Rectangle) {
                markerPath.addRect(ex, ey, ew, eh);
            } else if (p.kind == MarkerKind::Polygon) {
                if (p.polygonPoints.size() > 1) {
                    QPolygonF poly;
                    if (useFloatPoly) {
                        for (const auto& pt : m_markerDragFloatPoly) poly << pt;
                    } else {
                        for (const auto& pt : p.polygonPoints) poly << pt;
                    }
                    markerPath.addPolygon(poly);
                    markerPath.closeSubpath();
                }
            }

            painter->setPen(selected ? kSelectedPen : kUnselectedPen);

            if (p.kind != MarkerKind::Point) {
                painter->setBrush(selected ? kSelectedFill : kUnselectedFill);
            } else {
                painter->setBrush(Qt::NoBrush);
            }
            painter->drawPath(markerPath);

            if (selected) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(Qt::white);

                if (p.kind == MarkerKind::Circle) {
                    painter->drawEllipse(QPointF(ex + eRadius, ey), 4 * scale, 4 * scale);
                } else if (p.kind == MarkerKind::Rectangle) {
                    // Cleaner look like frame detection: no handle boxes, just cursor change.
                } else if (p.kind == MarkerKind::Polygon) {
                    int polySize = useFloatPoly ? m_markerDragFloatPoly.size() : p.polygonPoints.size();
                    for (int j = 0; j < polySize; ++j) {
                        QPointF pt = useFloatPoly ? m_markerDragFloatPoly[j] : QPointF(p.polygonPoints[j]);
                        if (j == m_selectedVertexIndex) {
                            painter->setBrush(kVertexHighlight);
                            painter->drawRect(QRectF(pt.x() - 4*scale, pt.y() - 4*scale, 8*scale, 8*scale));
                        } else {
                            painter->setBrush(Qt::white);
                            painter->drawRect(QRectF(pt.x() - 3*scale, pt.y() - 3*scale, 6*scale, 6*scale));
                        }
                    }
                }

                // Draw label with dark chip background
                painter->setFont(kLabelFont);

                int padding = 4;
                int textWidth = kLabelFm.horizontalAdvance(p.name);
                int textHeight = kLabelFm.height();

                // Position label relative to effective marker position
                QPointF textBasePos(ex + 10*scale, ey - 10*scale);

                // Correct for scale to keep chip at constant screen size
                painter->save();
                painter->translate(textBasePos);
                painter->scale(scale, scale);

                QRectF chipRect(0, -textHeight, textWidth + padding * 2, textHeight + padding);

                painter->setPen(Qt::NoPen);
                painter->setBrush(kChipBg);
                painter->drawRoundedRect(chipRect, 4, 4);

                painter->setPen(Qt::white);
                painter->drawText(chipRect.adjusted(padding, 0, -padding, 0), Qt::AlignLeft | Qt::AlignVCenter, p.name);

                painter->restore();
            }
        }
    }
}

void EditorOverlayItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (m_sprites.isEmpty()) return;
    QPointF pos = event->pos();
    double scale = getScale();
    double hitThreshold = 10.0 * scale;
    double hitThresholdSq = hitThreshold * hitThreshold;

    if (event->button() == Qt::RightButton) {
        if (!m_selectedMarkerName.isEmpty()) {
            NamedPoint* p = getNamedPoint(m_selectedMarkerName);
            if (p && p->kind == MarkerKind::Polygon) {
                for (int i = 0; i < p->polygonPoints.size(); ++i) {
                    if (distSq(pos, p->polygonPoints[i]) < hitThresholdSq) {
                        m_selectedVertexIndex = i;
                        update();
                        QMenu menu;
                        m_suppressNextViewContextMenu = true;
                        QAction* deleteAction = menu.addAction(trOverlay("Delete Vertex"));
                        if (p->polygonPoints.size() <= 3) deleteAction->setEnabled(false);
                        if (menu.exec(event->screenPos()) == deleteAction) removeSelectedVertex();
                        event->accept();
                        return;
                    }
                }
            }
        }

        auto activeSprite = m_sprites.last();
        if (distSq(pos, QPointF(activeSprite->pivotX, activeSprite->pivotY)) < hitThresholdSq) {
            QMenu menu;
            m_suppressNextViewContextMenu = true;
            QAction* left = menu.addAction(trOverlay("Left"));
            QAction* right = menu.addAction(trOverlay("Right"));
            QAction* hcenter = menu.addAction(trOverlay("H-Center"));
            QAction* top = menu.addAction(trOverlay("Top"));
            QAction* bottom = menu.addAction(trOverlay("Bottom"));
            QAction* vcenter = menu.addAction(trOverlay("V-Center"));

            QAction* sel = menu.exec(event->screenPos());
            if (sel) {
                const int oldPivotX = activeSprite->pivotX;
                const int oldPivotY = activeSprite->pivotY;
                for (auto& sprite : m_sprites) {
                    if (sel == left) sprite->pivotX = 0;
                    else if (sel == right) sprite->pivotX = m_sceneSize.width();
                    else if (sel == hcenter) sprite->pivotX = m_sceneSize.width() / 2;
                    else if (sel == top) sprite->pivotY = 0;
                    else if (sel == bottom) sprite->pivotY = m_sceneSize.height();
                    else if (sel == vcenter) sprite->pivotY = m_sceneSize.height() / 2;
                }
                emit pivotChanged(activeSprite->pivotX, activeSprite->pivotY);
                update();
                if (activeSprite->pivotX != oldPivotX || activeSprite->pivotY != oldPivotY)
                    emit pivotDragFinished(oldPivotX, oldPivotY,
                                           activeSprite->pivotX, activeSprite->pivotY);
            }
            event->accept();
            return;
        }

        for (int i = activeSprite->points.size() - 1; i >= 0; --i) {
            const auto& p = activeSprite->points[i];
            bool hit = false;
            if (p.kind == MarkerKind::Point) hit = distSq(pos, QPointF(p.x, p.y)) < hitThresholdSq;
            else if (p.kind == MarkerKind::Circle) hit = dist(pos, QPointF(p.x, p.y)) < p.radius;
            else if (p.kind == MarkerKind::Rectangle) hit = QRectF(p.x, p.y, p.w, p.h).contains(pos);
            else if (p.kind == MarkerKind::Polygon) {
                QPolygonF poly; for(const auto& pt : p.polygonPoints) poly << pt;
                hit = poly.containsPoint(pos, Qt::OddEvenFill);
            }
            if (hit) {
                if (m_selectedMarkerName != p.name) {
                    m_selectedMarkerName = p.name;
                    m_selectedVertexIndex = -1;
                    emit markerSelected(p.name);
                    update();
                }
                event->accept();
                return;
            }
        }
    }

    if (event->button() != Qt::LeftButton) return;

    // Save marker state before any drag so we can push an undo command on release.
    if (!m_sprites.isEmpty())
        m_pointsBeforeDrag = m_sprites.last()->points;

    if (!m_selectedMarkerName.isEmpty()) {
        NamedPoint* p = getNamedPoint(m_selectedMarkerName);
        if (p) {
            if (p->kind == MarkerKind::Rectangle) {
                ResizeHandle handle = getResizeHandle(pos, QRectF(p->x, p->y, p->w, p->h));
                if (handle != NoHandle) {
                    m_dragMode = RectResize;
                    m_resizeHandle = handle;
                    m_dragTargetName = p->name;
                    m_dragStartPos = pos;
                    m_dragOriginalRect = QRect(p->x, p->y, p->w, p->h);
                    m_markerDragFloatRect = QRectF(p->x, p->y, p->w, p->h);
                    updateResizeCursor(handle);
                    event->accept();
                    return;
                }
            } else if (p->kind == MarkerKind::Circle) {
                if (distSq(pos, QPointF(p->x + p->radius, p->y)) < hitThresholdSq) {
                    m_dragMode = CircleRadius;
                    m_dragTargetName = p->name;
                    m_dragStartPos = pos;
                    m_dragOriginalRadius = p->radius;
                    m_markerDragFloatRadius = (double)p->radius;
                    setCursor(Qt::SizeHorCursor);
                    event->accept();
                    return;
                }
            } else if (p->kind == MarkerKind::Polygon) {
                for (int i = 0; i < p->polygonPoints.size(); ++i) {
                    if (distSq(pos, p->polygonPoints[i]) < hitThresholdSq) {
                        m_dragMode = PolyVertex;
                        m_dragTargetName = p->name;
                        m_dragVertexIndex = i;
                        m_selectedVertexIndex = i;
                        m_markerDragFloatPoly.resize(p->polygonPoints.size());
                        for (int j = 0; j < p->polygonPoints.size(); ++j) m_markerDragFloatPoly[j] = p->polygonPoints[j];
                        setCursor(Qt::SizeAllCursor);
                        event->accept();
                        update();
                        return;
                    }
                }
                int edgeIdx = getPolygonHitEdge(p, pos);
                if (edgeIdx != -1) {
                    p->polygonPoints.insert(edgeIdx + 1, pos.toPoint());
                    m_markerDragFloatPoly.resize(p->polygonPoints.size());
                    for (int j = 0; j < p->polygonPoints.size(); ++j) m_markerDragFloatPoly[j] = p->polygonPoints[j];
                    m_markerDragFloatPoly[edgeIdx + 1] = pos;
                    m_dragMode = PolyVertex;
                    m_dragTargetName = p->name;
                    m_dragVertexIndex = edgeIdx + 1;
                    m_selectedVertexIndex = edgeIdx + 1;
                    setCursor(Qt::SizeAllCursor);
                    emit markerChanged();
                    update();
                    event->accept();
                    return;
                }
            }

            bool hit = false;
            if (p->kind == MarkerKind::Point) hit = distSq(pos, QPointF(p->x, p->y)) < hitThresholdSq;
            else if (p->kind == MarkerKind::Circle) hit = dist(pos, QPointF(p->x, p->y)) < p->radius;
            else if (p->kind == MarkerKind::Rectangle) hit = QRectF(p->x, p->y, p->w, p->h).contains(pos);
            else if (p->kind == MarkerKind::Polygon) {
                QPolygonF poly; for(const auto& pt : p->polygonPoints) poly << pt;
                hit = poly.containsPoint(pos, Qt::OddEvenFill);
            }

            if (hit) {
                m_dragMode = (p->kind == MarkerKind::Polygon) ? PolyMove : ((p->kind == MarkerKind::Rectangle) ? RectMove : Point);
                if (m_dragMode == PolyMove) {
                    m_dragOriginalPoly = p->polygonPoints;
                    m_markerDragFloatPoly.resize(p->polygonPoints.size());
                    for (int j = 0; j < p->polygonPoints.size(); ++j) m_markerDragFloatPoly[j] = p->polygonPoints[j];
                } else if (m_dragMode == RectMove) {
                    m_dragOriginalPos = QPoint(p->x, p->y);
                    m_markerDragFloatRect = QRectF(p->x, p->y, p->w, p->h);
                    m_markerDragFloatDelta = QPointF(p->x, p->y);
                } else {
                    m_dragOriginalPos = QPoint(p->x, p->y);
                    m_markerDragFloatDelta = QPointF(p->x, p->y);
                }
                m_dragTargetName = p->name;
                m_dragStartPos = pos;
                setCursor(Qt::SizeAllCursor);
                event->accept();
                return;
            }
        }
    }

    auto activeSprite = m_sprites.last();
    for (int i = activeSprite->points.size() - 1; i >= 0; --i) {
        const auto& p = activeSprite->points[i];
        bool hit = false;
        if (p.kind == MarkerKind::Point) hit = distSq(pos, QPointF(p.x, p.y)) < hitThresholdSq;
        else if (p.kind == MarkerKind::Circle) hit = dist(pos, QPointF(p.x, p.y)) < p.radius;
        else if (p.kind == MarkerKind::Rectangle) hit = QRectF(p.x, p.y, p.w, p.h).contains(pos);
        else if (p.kind == MarkerKind::Polygon) {
            QPolygonF poly; for(const auto& pt : p.polygonPoints) poly << pt;
            hit = poly.containsPoint(pos, Qt::OddEvenFill);
        }
        if (hit) {
            m_selectedMarkerName = p.name;
            emit markerSelected(p.name);
            m_dragMode = (p.kind == MarkerKind::Polygon) ? PolyMove : ((p.kind == MarkerKind::Rectangle) ? RectMove : Point);
            if (m_dragMode == PolyMove) {
                m_dragOriginalPoly = p.polygonPoints;
                m_markerDragFloatPoly.resize(p.polygonPoints.size());
                for (int j = 0; j < p.polygonPoints.size(); ++j) m_markerDragFloatPoly[j] = p.polygonPoints[j];
            } else if (m_dragMode == RectMove) {
                m_dragOriginalPos = QPoint(p.x, p.y);
                m_markerDragFloatRect = QRectF(p.x, p.y, p.w, p.h);
                m_markerDragFloatDelta = QPointF(p.x, p.y);
            } else {
                m_dragOriginalPos = QPoint(p.x, p.y);
                m_markerDragFloatDelta = QPointF(p.x, p.y);
            }
            m_dragTargetName = p.name;
            m_dragStartPos = pos;
            setCursor(Qt::SizeAllCursor);
            update();
            event->accept();
            return;
        }
    }

    if (!m_sprites.isEmpty())
        m_pivotBeforeDrag = QPoint(m_sprites.last()->pivotX, m_sprites.last()->pivotY);
    m_pivotDragFloat = pos;
    m_draggingPivot = true;
    setCursor(Qt::SizeAllCursor);
    for (auto& sprite : m_sprites) { sprite->pivotX = (int)pos.x(); sprite->pivotY = (int)pos.y(); }
    m_selectedMarkerName.clear(); emit markerSelected(""); m_selectedVertexIndex = -1;
    emit pivotChanged(activeSprite->pivotX, activeSprite->pivotY); update(); event->accept();
}

void EditorOverlayItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    QPointF pos = event->pos();
    if (m_draggingPivot && !m_sprites.isEmpty()) {
        // Track sub-pixel position; write truncated int to model for dialog feedback only.
        // The actual rounded commit happens in mouseReleaseEvent.
        m_pivotDragFloat = pos;
        for (auto& sprite : m_sprites) { sprite->pivotX = (int)pos.x(); sprite->pivotY = (int)pos.y(); }
        emit pivotChanged(m_sprites.last()->pivotX, m_sprites.last()->pivotY); update(); return;
    }
    if (m_dragMode != None) {
        NamedPoint* p = getNamedPoint(m_dragTargetName); if (!p) return;
        prepareGeometryChange();
        QPointF delta = pos - m_dragStartPos;
        if (m_dragMode == Point || m_dragMode == RectMove) {
            // Update float position; write truncated int for dialog feedback.
            m_markerDragFloatDelta = QPointF(m_dragOriginalPos) + delta;
            p->x = (int)m_markerDragFloatDelta.x(); p->y = (int)m_markerDragFloatDelta.y();
        } else if (m_dragMode == CircleRadius) {
            m_markerDragFloatRadius = qMax(1.0, dist(QPointF(p->x, p->y), pos));
            p->radius = qMax(1, (int)m_markerDragFloatRadius);
        } else if (m_dragMode == RectResize) {
            QRectF r(m_dragOriginalRect);
            switch (m_resizeHandle) {
                case TopLeft:     r.setTopLeft(pos); break;
                case Top:         r.setTop(pos.y()); break;
                case TopRight:    r.setTopRight(pos); break;
                case Right:       r.setRight(pos.x()); break;
                case BottomRight: r.setBottomRight(pos); break;
                case Bottom:      r.setBottom(pos.y()); break;
                case BottomLeft:  r.setBottomLeft(pos); break;
                case Left:        r.setLeft(pos.x()); break;
                default: break;
            }
            r = r.normalized();
            m_markerDragFloatRect = r;
            p->x = (int)r.x(); p->y = (int)r.y(); p->w = (int)r.width(); p->h = (int)r.height();
        } else if (m_dragMode == PolyVertex) {
            if (m_dragVertexIndex >= 0 && m_dragVertexIndex < p->polygonPoints.size()
                    && m_dragVertexIndex < m_markerDragFloatPoly.size()) {
                m_markerDragFloatPoly[m_dragVertexIndex] = pos;
                p->polygonPoints[m_dragVertexIndex] = QPoint((int)pos.x(), (int)pos.y());
                if (m_dragVertexIndex == 0) { p->x = p->polygonPoints[0].x(); p->y = p->polygonPoints[0].y(); }
            }
        } else if (m_dragMode == PolyMove) {
            for (int i = 0; i < p->polygonPoints.size() && i < m_markerDragFloatPoly.size(); ++i) {
                m_markerDragFloatPoly[i] = QPointF(m_dragOriginalPoly[i]) + delta;
                p->polygonPoints[i] = QPoint((int)m_markerDragFloatPoly[i].x(), (int)m_markerDragFloatPoly[i].y());
            }
            if (!p->polygonPoints.isEmpty()) { p->x = p->polygonPoints[0].x(); p->y = p->polygonPoints[0].y(); }
        }
        emit markerChanged(); update();
    }
}

void EditorOverlayItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    Q_UNUSED(event);

    // Commit rounded (pixel-snapped) final positions before capturing pointsAfterDrag.
    // During drag we tracked sub-pixel floats and wrote only truncated ints for dialog
    // feedback; here we apply qRound() once, so the snap occurs only on release.
    if (m_draggingPivot && !m_sprites.isEmpty()) {
        for (auto& sprite : m_sprites) {
            sprite->pivotX = qRound(m_pivotDragFloat.x());
            sprite->pivotY = qRound(m_pivotDragFloat.y());
        }
        emit pivotChanged(m_sprites.last()->pivotX, m_sprites.last()->pivotY);
    }
    if (m_dragMode != None) {
        NamedPoint* p = getNamedPoint(m_dragTargetName);
        if (p) {
            if (m_dragMode == Point || m_dragMode == RectMove) {
                p->x = qRound(m_markerDragFloatDelta.x());
                p->y = qRound(m_markerDragFloatDelta.y());
            } else if (m_dragMode == CircleRadius) {
                p->radius = qMax(1, qRound(m_markerDragFloatRadius));
            } else if (m_dragMode == RectResize) {
                p->x = qRound(m_markerDragFloatRect.x());
                p->y = qRound(m_markerDragFloatRect.y());
                p->w = qRound(m_markerDragFloatRect.width());
                p->h = qRound(m_markerDragFloatRect.height());
            } else if (m_dragMode == PolyVertex) {
                if (m_dragVertexIndex >= 0 && m_dragVertexIndex < p->polygonPoints.size()
                        && m_dragVertexIndex < m_markerDragFloatPoly.size()) {
                    p->polygonPoints[m_dragVertexIndex] = QPoint(
                        qRound(m_markerDragFloatPoly[m_dragVertexIndex].x()),
                        qRound(m_markerDragFloatPoly[m_dragVertexIndex].y()));
                    if (m_dragVertexIndex == 0) {
                        p->x = p->polygonPoints[0].x();
                        p->y = p->polygonPoints[0].y();
                    }
                }
            } else if (m_dragMode == PolyMove) {
                for (int i = 0; i < p->polygonPoints.size() && i < m_markerDragFloatPoly.size(); ++i) {
                    p->polygonPoints[i] = QPoint(
                        qRound(m_markerDragFloatPoly[i].x()),
                        qRound(m_markerDragFloatPoly[i].y()));
                }
                if (!p->polygonPoints.isEmpty()) {
                    p->x = p->polygonPoints[0].x();
                    p->y = p->polygonPoints[0].y();
                }
            }
            emit markerChanged();
        }
    }

    const bool wasDraggingPivot = m_draggingPivot;
    const bool wasDraggingMarker = (m_dragMode != None);
    QVector<NamedPoint> pointsAfterDrag;
    if (wasDraggingMarker && !m_sprites.isEmpty())
        pointsAfterDrag = m_sprites.last()->points;

    m_draggingPivot = false;
    m_dragMode = None;
    unsetCursor();
    update(); // Repaint to show the snapped (rounded) final position

    if (wasDraggingPivot && !m_sprites.isEmpty()) {
        auto s = m_sprites.last();
        if (s->pivotX != m_pivotBeforeDrag.x() || s->pivotY != m_pivotBeforeDrag.y())
            emit pivotDragFinished(m_pivotBeforeDrag.x(), m_pivotBeforeDrag.y(),
                                   s->pivotX, s->pivotY);
    }
    if (wasDraggingMarker && pointsAfterDrag != m_pointsBeforeDrag)
        emit markerDragFinished(m_pointsBeforeDrag, pointsAfterDrag);
}

EditorOverlayItem::ResizeHandle EditorOverlayItem::getResizeHandle(const QPointF& p, const QRectF& r) const {
    const double s = 8.0 * getScale();
    if (qAbs(p.x()-r.left())<=s && qAbs(p.y()-r.top())<=s) return TopLeft;
    if (qAbs(p.x()-r.right())<=s && qAbs(p.y()-r.top())<=s) return TopRight;
    if (qAbs(p.x()-r.left())<=s && qAbs(p.y()-r.bottom())<=s) return BottomLeft;
    if (qAbs(p.x()-r.right())<=s && qAbs(p.y()-r.bottom())<=s) return BottomRight;
    if (qAbs(p.x()-r.left())<=s/2 && p.y()>=r.top() && p.y()<=r.bottom()) return Left;
    if (qAbs(p.x()-r.right())<=s/2 && p.y()>=r.top() && p.y()<=r.bottom()) return Right;
    if (qAbs(p.y()-r.top())<=s/2 && p.x()>=r.left() && p.x()<=r.right()) return Top;
    if (qAbs(p.y()-r.bottom())<=s/2 && p.x()>=r.left() && p.x()<=r.right()) return Bottom;
    return NoHandle;
}

void EditorOverlayItem::updateResizeCursor(ResizeHandle h) {
    switch (h) {
        case TopLeft: case BottomRight: setCursor(Qt::SizeFDiagCursor); break;
        case TopRight: case BottomLeft: setCursor(Qt::SizeBDiagCursor); break;
        case Top: case Bottom: setCursor(Qt::SizeVerCursor); break;
        case Left: case Right: setCursor(Qt::SizeHorCursor); break;
        default: setCursor(Qt::ArrowCursor); break;
    }
}

Qt::CursorShape EditorOverlayItem::cursorForPosition(const QPointF& pos) const {
    if (m_sprites.isEmpty()) return Qt::ArrowCursor;
    double scale = getScale(); double hitThreshold = 10.0 * scale; double hitThresholdSq = hitThreshold * hitThreshold;
    auto activeSprite = m_sprites.last();
    if (!m_selectedMarkerName.isEmpty()) {
        NamedPoint* p = const_cast<EditorOverlayItem*>(this)->getNamedPoint(m_selectedMarkerName);
        if (p) {
            if (p->kind == MarkerKind::Rectangle) {
                ResizeHandle h = getResizeHandle(pos, QRectF(p->x, p->y, p->w, p->h));
                if (h != NoHandle) {
                    switch (h) {
                        case TopLeft: case BottomRight: return Qt::SizeFDiagCursor;
                        case TopRight: case BottomLeft: return Qt::SizeBDiagCursor;
                        case Top: case Bottom: return Qt::SizeVerCursor;
                        case Left: case Right: return Qt::SizeHorCursor;
                        default: break;
                    }
                }
            } else if (p->kind == MarkerKind::Circle) {
                if (distSq(pos, QPointF(p->x + p->radius, p->y)) < hitThresholdSq) return Qt::SizeHorCursor;
            } else if (p->kind == MarkerKind::Polygon) {
                for (const auto& pt : p->polygonPoints) if (distSq(pos, pt) < hitThresholdSq) return Qt::SizeAllCursor;
                if (getPolygonHitEdge(p, pos) != -1) return Qt::CrossCursor;
            }
            
            bool hit = false;
            if (p->kind == MarkerKind::Point) hit = distSq(pos, QPointF(p->x, p->y)) < hitThresholdSq;
            else if (p->kind == MarkerKind::Circle) hit = dist(pos, QPointF(p->x, p->y)) < p->radius;
            else if (p->kind == MarkerKind::Rectangle) hit = QRectF(p->x, p->y, p->w, p->h).contains(pos);
            else if (p->kind == MarkerKind::Polygon) {
                QPolygonF poly; for(const auto& pt : p->polygonPoints) poly << pt;
                hit = poly.containsPoint(pos, Qt::OddEvenFill);
            }
            if (hit) return Qt::SizeAllCursor;
        }
    }

    for (int i = activeSprite->points.size() - 1; i >= 0; --i) {
        const auto& p = activeSprite->points[i];
        bool hit = false;
        if (p.kind == MarkerKind::Point) hit = distSq(pos, QPointF(p.x, p.y)) < hitThresholdSq;
        else if (p.kind == MarkerKind::Circle) hit = dist(pos, QPointF(p.x, p.y)) < p.radius;
        else if (p.kind == MarkerKind::Rectangle) hit = QRectF(p.x, p.y, p.w, p.h).contains(pos);
        else if (p.kind == MarkerKind::Polygon) {
            QPolygonF poly; for(const auto& pt : p.polygonPoints) poly << pt;
            hit = poly.containsPoint(pos, Qt::OddEvenFill);
        }
        if (hit) return Qt::SizeAllCursor;
    }

    if (distSq(pos, QPointF(activeSprite->pivotX, activeSprite->pivotY)) < hitThresholdSq) return Qt::SizeAllCursor;

    return Qt::CrossCursor;
}

void EditorOverlayItem::updateHoverCursor(const QPointF& pos) {
    if (m_draggingPivot || m_dragMode != None) return;
    setCursor(cursorForPosition(pos));
}

void EditorOverlayItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event) {
    updateHoverCursor(event->pos()); QGraphicsObject::hoverMoveEvent(event);
}

void EditorOverlayItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    if (!m_draggingPivot && m_dragMode == None) unsetCursor();
    QGraphicsObject::hoverLeaveEvent(event);
}

bool EditorOverlayItem::removeSelectedVertex() {
    if (m_sprites.isEmpty()) return false;
    NamedPoint* p = getNamedPoint(m_selectedMarkerName);
    if (!p || p->kind != MarkerKind::Polygon || m_selectedVertexIndex < 0 || m_selectedVertexIndex >= p->polygonPoints.size() || p->polygonPoints.size() <= 3) return false;
    const QVector<NamedPoint> oldPoints = m_sprites.last()->points;
    prepareGeometryChange(); p->polygonPoints.remove(m_selectedVertexIndex);
    m_selectedVertexIndex = qMin(m_selectedVertexIndex, p->polygonPoints.size() - 1);
    emit markerChanged(); update();
    emit markerDragFinished(oldPoints, m_sprites.last()->points);
    return true;
}

bool EditorOverlayItem::deleteSelectedMarker() {
    if (m_selectedMarkerName.isEmpty() || m_sprites.isEmpty()) return false;
    const QVector<NamedPoint> oldPoints = m_sprites.last()->points;
    QString target = m_selectedMarkerName;
    bool anyDeleted = false;
    for (auto& sprite : m_sprites) {
        for (int i = 0; i < sprite->points.size(); ++i) {
            if (sprite->points[i].name == target) {
                sprite->points.removeAt(i);
                anyDeleted = true;
                break;
            }
        }
    }
    if (anyDeleted) {
        m_selectedMarkerName.clear();
        m_selectedVertexIndex = -1;
        emit markerSelected("");
        emit markerChanged();
        update();
        emit markerDragFinished(oldPoints, m_sprites.last()->points);
    }
    return anyDeleted;
}

void EditorOverlayItem::updateLayout() { prepareGeometryChange(); update(); }

bool EditorOverlayItem::hasContextMenuTargetAt(const QPointF& pos) const {
    if (m_sprites.isEmpty()) return false;
    double scale = getScale(); double hitThreshold = 10.0 * scale; double hitThresholdSq = hitThreshold * hitThreshold; auto activeSprite = m_sprites.last();
    if (distSq(pos, QPointF(activeSprite->pivotX, activeSprite->pivotY)) < hitThresholdSq) return true;
    for (const auto& p : activeSprite->points) {
        bool hit = false;
        if (p.kind == MarkerKind::Point) hit = distSq(pos, QPointF(p.x, p.y)) < hitThresholdSq;
        else if (p.kind == MarkerKind::Circle) hit = dist(pos, QPointF(p.x, p.y)) < p.radius;
        else if (p.kind == MarkerKind::Rectangle) hit = QRectF(p.x, p.y, p.w, p.h).contains(pos);
        else if (p.kind == MarkerKind::Polygon) {
            QPolygonF poly; for(const auto& pt : p.polygonPoints) poly << pt;
            hit = poly.containsPoint(pos, Qt::OddEvenFill);
        }
        if (hit) return true;
    }
    return false;
}

bool EditorOverlayItem::consumeSuppressedViewContextMenu() {
    bool suppressed = m_suppressNextViewContextMenu; m_suppressNextViewContextMenu = false; return suppressed;
}
