#include "EditorOverlayItem.h"
#include <QPainter>
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
            drawPivot(painter, sprite->pivotX, sprite->pivotY);
        }
    }

    // 3. Selected Marker
    if (!pivotSelected) {
        drawMarkers(painter, true);
    }

    // 4. Pivot (if selected)
    if (pivotSelected) {
        for (const auto& sprite : m_sprites) {
            drawPivot(painter, sprite->pivotX, sprite->pivotY);
        }
    }
}

void EditorOverlayItem::drawPivot(QPainter* painter, int x, int y) {
    double scale = getScale();
    double len = 10 * scale;

    QPen outlinePen(Qt::black, 5);
    outlinePen.setCosmetic(true);
    painter->setPen(outlinePen);
    painter->drawLine(QPointF(x - len, y), QPointF(x + len, y));
    painter->drawLine(QPointF(x, y - len), QPointF(x, y + len));

    QPen pen(QColor(255, 215, 0), 2); // Gold
    pen.setCosmetic(true);
    painter->setPen(pen);

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
    if (ab_len2 <= 0.00001) return QLineF(p, a).length();
    double t = qMax(0.0, qMin(1.0, (ap.x() * ab.x() + ap.y() * ab.y()) / ab_len2));
    QPointF closest = a + ab * t;
    return QLineF(p, closest).length();
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
    if (m_sprites.isEmpty()) return;
    double scale = getScale();

    for (const auto& sprite : m_sprites) {
        for (int i = 0; i < sprite->points.size(); ++i) {
            const auto& p = sprite->points[i];
            bool selected = (p.name == m_selectedMarkerName);
            if (selected != drawSelected) continue;
            
            QColor color = selected ? Qt::green : Qt::cyan;
            QPainterPath path;

            if (p.kind == MarkerKind::Point) {
                double len = 6 * scale;
                path.moveTo(p.x - len, p.y - len); path.lineTo(p.x + len, p.y + len);
                path.moveTo(p.x + len, p.y - len); path.lineTo(p.x - len, p.y + len);
            } else if (p.kind == MarkerKind::Circle) {
                path.addEllipse(QPointF(p.x, p.y), p.radius, p.radius);
            } else if (p.kind == MarkerKind::Rectangle) {
                path.addRect(p.x, p.y, p.w, p.h);
            } else if (p.kind == MarkerKind::Polygon) {
                if (p.polygonPoints.size() > 1) {
                    QPolygonF poly;
                    for(const auto& pt : p.polygonPoints) poly << pt;
                    path.addPolygon(poly);
                    path.closeSubpath();
                }
            }

            QPen colorPen(color, 2);
            colorPen.setCosmetic(true);
            painter->setPen(colorPen);
            
            if (p.kind != MarkerKind::Point) {
                QColor fillColor = color;
                fillColor.setAlpha(selected ? 60 : 30);
                painter->setBrush(fillColor);
            } else {
                painter->setBrush(Qt::NoBrush);
            }
            painter->drawPath(path);

            if (selected) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(Qt::white);
                
                if (p.kind == MarkerKind::Circle) {
                    painter->drawEllipse(QPointF(p.x + p.radius, p.y), 4 * scale, 4 * scale);
                } else if (p.kind == MarkerKind::Rectangle) {
                    // Cleaner look like frame detection: no handle boxes, just cursor change.
                } else if (p.kind == MarkerKind::Polygon) {
                    for (int j = 0; j < p.polygonPoints.size(); ++j) {
                        const auto& pt = p.polygonPoints[j];
                        if (j == m_selectedVertexIndex) {
                            painter->setBrush(QColor(255, 50, 50));
                            painter->drawRect(QRectF(pt.x() - 4*scale, pt.y() - 4*scale, 8*scale, 8*scale));
                        } else {
                            painter->setBrush(Qt::white);
                            painter->drawRect(QRectF(pt.x() - 3*scale, pt.y() - 3*scale, 6*scale, 6*scale));
                        }
                    }
                }
                
                // Draw Label with dark chip background
                QFont font = painter->font();
                font.setPixelSize(11);
                painter->setFont(font);
                QFontMetrics fm(font);
                
                int padding = 4;
                int textWidth = fm.horizontalAdvance(p.name);
                int textHeight = fm.height();
                
                // Position label relative to marker
                QPointF textBasePos(p.x + 10*scale, p.y - 10*scale);
                
                // Correct for scale to keep chip at constant screen size
                painter->save();
                painter->translate(textBasePos);
                painter->scale(scale, scale);
                
                QRectF chipRect(0, -textHeight, textWidth + padding * 2, textHeight + padding);
                
                painter->setPen(Qt::NoPen);
                painter->setBrush(QColor(0, 0, 0, 160));
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

    if (event->button() == Qt::RightButton) {
        if (!m_selectedMarkerName.isEmpty()) {
            NamedPoint* p = getNamedPoint(m_selectedMarkerName);
            if (p && p->kind == MarkerKind::Polygon) {
                for (int i = 0; i < p->polygonPoints.size(); ++i) {
                    if (QLineF(pos, p->polygonPoints[i]).length() < hitThreshold) {
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
        if (QLineF(pos, QPointF(activeSprite->pivotX, activeSprite->pivotY)).length() < hitThreshold) {
            QMenu menu;
            m_suppressNextViewContextMenu = true;
            QAction* left = menu.addAction(trOverlay("Left"));
            QAction* right = menu.addAction(trOverlay("Right"));
            QAction* hcenter = menu.addAction(trOverlay("H-Center"));
            QAction* top = menu.addAction(trOverlay("Top"));
            QAction* bottom = menu.addAction(trOverlay("Bottom"));
            QAction* vcenter = menu.addAction(trOverlay("V-Center"));
            menu.addSeparator();
            QAction* applyAction = menu.addAction(trOverlay("Apply to Selected Frames"));
            
            QAction* sel = menu.exec(event->screenPos());
            if (sel) {
                if (sel == applyAction) {
                    emit applyPivotToSelectedFramesRequested();
                } else {
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
                }
            }
            event->accept();
            return;
        }

        for (int i = activeSprite->points.size() - 1; i >= 0; --i) {
            const auto& p = activeSprite->points[i];
            bool hit = false;
            if (p.kind == MarkerKind::Point) hit = QLineF(pos, QPointF(p.x, p.y)).length() < hitThreshold;
            else if (p.kind == MarkerKind::Circle) hit = QLineF(pos, QPointF(p.x, p.y)).length() < p.radius;
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
                QMenu menu;
                m_suppressNextViewContextMenu = true;
                QAction* applyAction = menu.addAction(trOverlay("Apply to Selected Frames"));
                if (menu.exec(event->screenPos()) == applyAction) {
                    emit applyMarkerToSelectedFramesRequested(p.name);
                }
                event->accept();
                return;
            }
        }
    }

    if (event->button() != Qt::LeftButton) return;

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
                    updateResizeCursor(handle);
                    event->accept();
                    return;
                }
            } else if (p->kind == MarkerKind::Circle) {
                if (QLineF(pos, QPointF(p->x + p->radius, p->y)).length() < hitThreshold) {
                    m_dragMode = CircleRadius;
                    m_dragTargetName = p->name;
                    m_dragStartPos = pos;
                    m_dragOriginalRadius = p->radius;
                    setCursor(Qt::SizeHorCursor);
                    event->accept();
                    return;
                }
            } else if (p->kind == MarkerKind::Polygon) {
                for (int i = 0; i < p->polygonPoints.size(); ++i) {
                    if (QLineF(pos, p->polygonPoints[i]).length() < hitThreshold) {
                        m_dragMode = PolyVertex;
                        m_dragTargetName = p->name;
                        m_dragVertexIndex = i;
                        m_selectedVertexIndex = i;
                        setCursor(Qt::SizeAllCursor);
                        event->accept();
                        update();
                        return;
                    }
                }
                int edgeIdx = getPolygonHitEdge(p, pos);
                if (edgeIdx != -1) {
                    p->polygonPoints.insert(edgeIdx + 1, pos.toPoint());
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
            if (p->kind == MarkerKind::Point) hit = QLineF(pos, QPointF(p->x, p->y)).length() < hitThreshold;
            else if (p->kind == MarkerKind::Circle) hit = QLineF(pos, QPointF(p->x, p->y)).length() < p->radius;
            else if (p->kind == MarkerKind::Rectangle) hit = QRectF(p->x, p->y, p->w, p->h).contains(pos);
            else if (p->kind == MarkerKind::Polygon) {
                QPolygonF poly; for(const auto& pt : p->polygonPoints) poly << pt;
                hit = poly.containsPoint(pos, Qt::OddEvenFill);
            }

            if (hit) {
                m_dragMode = (p->kind == MarkerKind::Polygon) ? PolyMove : ((p->kind == MarkerKind::Rectangle) ? RectMove : Point);
                if (m_dragMode == PolyMove) m_dragOriginalPoly = p->polygonPoints;
                else m_dragOriginalPos = QPoint(p->x, p->y);
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
        if (p.kind == MarkerKind::Point) hit = QLineF(pos, QPointF(p.x, p.y)).length() < hitThreshold;
        else if (p.kind == MarkerKind::Circle) hit = QLineF(pos, QPointF(p.x, p.y)).length() < p.radius;
        else if (p.kind == MarkerKind::Rectangle) hit = QRectF(p.x, p.y, p.w, p.h).contains(pos);
        else if (p.kind == MarkerKind::Polygon) {
            QPolygonF poly; for(const auto& pt : p.polygonPoints) poly << pt;
            hit = poly.containsPoint(pos, Qt::OddEvenFill);
        }
        if (hit) {
            m_selectedMarkerName = p.name;
            emit markerSelected(p.name);
            m_dragMode = (p.kind == MarkerKind::Polygon) ? PolyMove : ((p.kind == MarkerKind::Rectangle) ? RectMove : Point);
            if (m_dragMode == PolyMove) m_dragOriginalPoly = p.polygonPoints;
            else m_dragOriginalPos = QPoint(p.x, p.y);
            m_dragTargetName = p.name;
            m_dragStartPos = pos;
            setCursor(Qt::SizeAllCursor);
            update();
            event->accept();
            return;
        }
    }

    m_draggingPivot = true;
    setCursor(Qt::SizeAllCursor);
    for (auto& sprite : m_sprites) { sprite->pivotX = qRound(pos.x()); sprite->pivotY = qRound(pos.y()); }
    m_selectedMarkerName.clear(); emit markerSelected(""); m_selectedVertexIndex = -1;
    emit pivotChanged(activeSprite->pivotX, activeSprite->pivotY); update(); event->accept();
}

void EditorOverlayItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    QPointF pos = event->pos();
    if (m_draggingPivot && !m_sprites.isEmpty()) {
        for (auto& sprite : m_sprites) { sprite->pivotX = qRound(pos.x()); sprite->pivotY = qRound(pos.y()); }
        emit pivotChanged(m_sprites.last()->pivotX, m_sprites.last()->pivotY); update(); return;
    }
    if (m_dragMode != None) {
        NamedPoint* p = getNamedPoint(m_dragTargetName); if (!p) return;
        prepareGeometryChange();
        QPointF delta = pos - m_dragStartPos;
        if (m_dragMode == Point || m_dragMode == RectMove) {
            p->x = m_dragOriginalPos.x() + qRound(delta.x()); p->y = m_dragOriginalPos.y() + qRound(delta.y());
        } else if (m_dragMode == CircleRadius) {
            p->radius = qMax(1, qRound(QLineF(QPointF(p->x, p->y), pos).length()));
        } else if (m_dragMode == RectResize) {
            QRect r = m_dragOriginalRect;
            switch (m_resizeHandle) {
                case TopLeft: r.setTopLeft(pos.toPoint()); break;
                case Top: r.setTop(pos.y()); break;
                case TopRight: r.setTopRight(pos.toPoint()); break;
                case Right: r.setRight(pos.x()); break;
                case BottomRight: r.setBottomRight(pos.toPoint()); break;
                case Bottom: r.setBottom(pos.y()); break;
                case BottomLeft: r.setBottomLeft(pos.toPoint()); break;
                case Left: r.setLeft(pos.x()); break;
                default: break;
            }
            r = r.normalized(); p->x = r.x(); p->y = r.y(); p->w = r.width(); p->h = r.height();
        } else if (m_dragMode == PolyVertex) {
            if (m_dragVertexIndex >= 0 && m_dragVertexIndex < p->polygonPoints.size()) {
                p->polygonPoints[m_dragVertexIndex] = pos.toPoint();
                if (m_dragVertexIndex == 0) { p->x = p->polygonPoints[0].x(); p->y = p->polygonPoints[0].y(); }
            }
        } else if (m_dragMode == PolyMove) {
            for (int i = 0; i < p->polygonPoints.size(); ++i) p->polygonPoints[i] = m_dragOriginalPoly[i] + delta.toPoint();
            if (!p->polygonPoints.isEmpty()) { p->x = p->polygonPoints[0].x(); p->y = p->polygonPoints[0].y(); }
        }
        emit markerChanged(); update();
    }
}

void EditorOverlayItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    m_draggingPivot = false; m_dragMode = None; Q_UNUSED(event); unsetCursor();
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
    double scale = getScale(); double hitThreshold = 10.0 * scale;
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
                if (QLineF(pos, QPointF(p->x + p->radius, p->y)).length() < hitThreshold) return Qt::SizeHorCursor;
            } else if (p->kind == MarkerKind::Polygon) {
                for (const auto& pt : p->polygonPoints) if (QLineF(pos, pt).length() < hitThreshold) return Qt::SizeAllCursor;
                if (getPolygonHitEdge(p, pos) != -1) return Qt::CrossCursor;
            }
            
            bool hit = false;
            if (p->kind == MarkerKind::Point) hit = QLineF(pos, QPointF(p->x, p->y)).length() < hitThreshold;
            else if (p->kind == MarkerKind::Circle) hit = QLineF(pos, QPointF(p->x, p->y)).length() < p->radius;
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
        if (p.kind == MarkerKind::Point) hit = QLineF(pos, QPointF(p.x, p.y)).length() < hitThreshold;
        else if (p.kind == MarkerKind::Circle) hit = QLineF(pos, QPointF(p.x, p.y)).length() < p.radius;
        else if (p.kind == MarkerKind::Rectangle) hit = QRectF(p.x, p.y, p.w, p.h).contains(pos);
        else if (p.kind == MarkerKind::Polygon) {
            QPolygonF poly; for(const auto& pt : p.polygonPoints) poly << pt;
            hit = poly.containsPoint(pos, Qt::OddEvenFill);
        }
        if (hit) return Qt::SizeAllCursor;
    }

    if (QLineF(pos, QPointF(activeSprite->pivotX, activeSprite->pivotY)).length() < hitThreshold) return Qt::SizeAllCursor;

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
    NamedPoint* p = getNamedPoint(m_selectedMarkerName);
    if (!p || p->kind != MarkerKind::Polygon || m_selectedVertexIndex < 0 || m_selectedVertexIndex >= p->polygonPoints.size() || p->polygonPoints.size() <= 3) return false;
    prepareGeometryChange(); p->polygonPoints.remove(m_selectedVertexIndex);
    m_selectedVertexIndex = qMin(m_selectedVertexIndex, p->polygonPoints.size() - 1);
    emit markerChanged(); update(); return true;
}

bool EditorOverlayItem::deleteSelectedMarker() {
    if (m_selectedMarkerName.isEmpty() || m_sprites.isEmpty()) return false;
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
    }
    return anyDeleted;
}

void EditorOverlayItem::updateLayout() { prepareGeometryChange(); update(); }

bool EditorOverlayItem::hasContextMenuTargetAt(const QPointF& pos) const {
    if (m_sprites.isEmpty()) return false;
    double scale = getScale(); double hitThreshold = 10.0 * scale; auto activeSprite = m_sprites.last();
    if (QLineF(pos, QPointF(activeSprite->pivotX, activeSprite->pivotY)).length() < hitThreshold) return true;
    for (const auto& p : activeSprite->points) {
        bool hit = false;
        if (p.kind == MarkerKind::Point) hit = QLineF(pos, QPointF(p.x, p.y)).length() < hitThreshold;
        else if (p.kind == MarkerKind::Circle) hit = QLineF(pos, QPointF(p.x, p.y)).length() < p.radius;
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
