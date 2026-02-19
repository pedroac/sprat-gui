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
        // Include Pivot
        if (sprite->pivotX < minX) {
            minX = sprite->pivotX;
        }
        if (sprite->pivotX > maxX) {
            maxX = sprite->pivotX;
        }
        if (sprite->pivotY < minY) {
            minY = sprite->pivotY;
        }
        if (sprite->pivotY > maxY) {
            maxY = sprite->pivotY;
        }

        // Include Markers
        for (const auto& p : sprite->points) {
            if (p.x < minX) {
                minX = p.x;
            }
            if (p.x > maxX) {
                maxX = p.x;
            }
            if (p.y < minY) {
                minY = p.y;
            }
            if (p.y > maxY) {
                maxY = p.y;
            }
            
            if (p.kind == MarkerKind::Circle) {
                 if (p.x - p.radius < minX) {
                     minX = p.x - p.radius;
                 }
                 if (p.x + p.radius > maxX) {
                     maxX = p.x + p.radius;
                 }
                 if (p.y - p.radius < minY) {
                     minY = p.y - p.radius;
                 }
                 if (p.y + p.radius > maxY) {
                     maxY = p.y + p.radius;
                 }
            } else if (p.kind == MarkerKind::Rectangle) {
                 if (p.x + p.w > maxX) {
                     maxX = p.x + p.w;
                 }
                 if (p.y + p.h > maxY) {
                     maxY = p.y + p.h;
                 }
            } else if (p.kind == MarkerKind::Polygon) {
                 for (const auto& pt : p.polygonPoints) {
                     if (pt.x() < minX) {
                         minX = pt.x();
                     }
                     if (pt.x() > maxX) {
                         maxX = pt.x();
                     }
                     if (pt.y() < minY) {
                         minY = pt.y();
                     }
                     if (pt.y() > maxY) {
                         maxY = pt.y();
                     }
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
    // Only check the last sprite (active) for markers for now
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
    if (ab_len2 <= 0.00001) {
        return QLineF(p, a).length();
    }
    double t = qMax(0.0, qMin(1.0, (ap.x() * ab.x() + ap.y() * ab.y()) / ab_len2));
    QPointF closest = a + ab * t;
    return QLineF(p, closest).length();
}

int EditorOverlayItem::getPolygonHitEdge(const NamedPoint* p, const QPointF& pos) const {
    if (p->kind != MarkerKind::Polygon || p->polygonPoints.size() < 2) {
        return -1;
    }
    double scale = getScale();
    double threshold = 8.0 * scale;
    for (int i = 0; i < p->polygonPoints.size(); ++i) {
        QPointF p1 = p->polygonPoints[i];
        QPointF p2 = p->polygonPoints[(i + 1) % p->polygonPoints.size()];
        if (distancePointToSegment(pos, p1, p2) <= threshold) {
            return i;
        }
    }
    return -1;
}

void EditorOverlayItem::drawMarkers(QPainter* painter, bool drawSelected) {
    if (m_sprites.isEmpty()) {
        return;
    }
    double scale = getScale();

    static const QVector<QColor> palette = {
        QColor::fromRgbF(0.17, 0.85, 0.35),
        QColor::fromRgbF(0.95, 0.56, 0.10),
        QColor::fromRgbF(0.16, 0.75, 0.94),
        QColor::fromRgbF(0.94, 0.38, 0.74),
        QColor::fromRgbF(0.97, 0.84, 0.15),
        QColor::fromRgbF(0.44, 0.82, 0.94),
        QColor::fromRgbF(0.74, 0.94, 0.24)
    };

    // Only draw markers for the active sprite (last one) to avoid clutter
    // or draw all? The request says "having all selected frames in the editor".
    // Let's draw all.
    for (const auto& sprite : m_sprites) {
        for (int i = 0; i < sprite->points.size(); ++i) {
            const auto& p = sprite->points[i];
            bool selected = (p.name == m_selectedMarkerName);
            if (selected != drawSelected) {
                continue;
            }
            
            QColor baseColor = palette[i % palette.size()];
            QColor color;
            if (selected) {
                color = baseColor;
            } else {
                float r, g, b;
                baseColor.getRgbF(&r, &g, &b);
                float gray = (r + g + b) / 3.0f;
                // Desaturate: mix 35% color with 65% gray
                color = QColor::fromRgbF(r * 0.35 + gray * 0.65, g * 0.35 + gray * 0.65, b * 0.35 + gray * 0.65);
            }
            
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

            // Draw Outline (Contrast)
            if (p.kind != MarkerKind::Polygon) {
                QPen outlinePen(Qt::black, 5);
                outlinePen.setCosmetic(true);
                painter->setPen(outlinePen);
                painter->setBrush(Qt::NoBrush);
                painter->drawPath(path);
            }

            // Draw Color Stroke
            QPen colorPen(color, 2.5);
            colorPen.setCosmetic(true);
            painter->setPen(colorPen);
            
            // Fill
            if (p.kind != MarkerKind::Point) {
                QColor fillColor = color;
                fillColor.setAlpha(selected ? 60 : 30);
                painter->setBrush(fillColor);
            } else {
                painter->setBrush(Qt::NoBrush);
            }
            painter->drawPath(path);

            // Draw Handles
            if (selected) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(Qt::white);
                
                if (p.kind == MarkerKind::Circle) {
                    painter->drawEllipse(QPointF(p.x + p.radius, p.y), 4 * scale, 4 * scale);
                } else if (p.kind == MarkerKind::Rectangle) {
                    painter->drawRect(QRectF(p.x + p.w - 4*scale, p.y + p.h - 4*scale, 8*scale, 8*scale));
                } else if (p.kind == MarkerKind::Polygon) {
                    for (int i = 0; i < p.polygonPoints.size(); ++i) {
                        const auto& pt = p.polygonPoints[i];
                        
                        QPen vertexOutline(Qt::black, 5);
                        vertexOutline.setCosmetic(true);
                        
                        if (i == m_selectedVertexIndex) {
                            // Selected vertex outline
                            painter->setPen(vertexOutline);
                            painter->setBrush(Qt::NoBrush);
                            painter->drawRect(QRectF(pt.x() - 4*scale, pt.y() - 4*scale, 8*scale, 8*scale));
                            
                            QPen vertexFill(QColor(255, 50, 50), 2.5);
                            vertexFill.setCosmetic(true);
                            painter->setPen(vertexFill);
                            painter->setBrush(QColor(255, 50, 50));
                            painter->drawRect(QRectF(pt.x() - 4*scale, pt.y() - 4*scale, 8*scale, 8*scale));
                        } else {
                            painter->setPen(vertexOutline);
                            painter->setBrush(Qt::NoBrush);
                            painter->drawRect(QRectF(pt.x() - 3*scale, pt.y() - 3*scale, 6*scale, 6*scale));
                            
                            QPen vertexFill(color, 2.5);
                            vertexFill.setCosmetic(true);
                            painter->setPen(vertexFill);
                            painter->setBrush(Qt::white);
                            painter->drawRect(QRectF(pt.x() - 3*scale, pt.y() - 3*scale, 6*scale, 6*scale));
                        }
                    }
                }
                
                // Draw Label
                QPen textPen(Qt::black);
                textPen.setCosmetic(true);
                painter->setPen(textPen);
                painter->drawText(QPointF(p.x + 10*scale, p.y - 10*scale), p.name);
            }
        }
    }
}

void EditorOverlayItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (m_sprites.isEmpty()) {
        return;
    }
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
                        QAction* deleteAction = menu.addAction("Delete Vertex");
                        if (p->polygonPoints.size() <= 3) {
                            deleteAction->setEnabled(false);
                        }
                        if (menu.exec(event->screenPos()) == deleteAction) {
                            removeSelectedVertex();
                        }
                        event->accept();
                        return;
                    }
                }
            }
        }

        // Check pivot hit
        // Check pivot of active sprite
        auto activeSprite = m_sprites.last();
        if (QLineF(pos, QPointF(activeSprite->pivotX, activeSprite->pivotY)).length() < hitThreshold) {
            QMenu menu;
            QAction* left = menu.addAction("Left");
            QAction* right = menu.addAction("Right");
            QAction* hcenter = menu.addAction("H-Center");
            QAction* top = menu.addAction("Top");
            QAction* bottom = menu.addAction("Bottom");
            QAction* vcenter = menu.addAction("V-Center");
            
            QAction* sel = menu.exec(event->screenPos());
            if (sel) {
                for (auto& sprite : m_sprites) {
                    if (sel == left) {
                        sprite->pivotX = 0;
                    }
                    else if (sel == right) {
                        sprite->pivotX = m_sceneSize.width();
                    }
                    else if (sel == hcenter) {
                        sprite->pivotX = m_sceneSize.width() / 2;
                    }
                    else if (sel == top) {
                        sprite->pivotY = 0;
                    }
                    else if (sel == bottom) {
                        sprite->pivotY = m_sceneSize.height();
                    }
                    else if (sel == vcenter) {
                        sprite->pivotY = m_sceneSize.height() / 2;
                    }
                }
                
                emit pivotChanged(activeSprite->pivotX, activeSprite->pivotY);
                update();
            }
            event->accept();
            return;
        }
    }

    if (event->button() != Qt::LeftButton) {
        return;
    }

    // Priority 0: Pivot (if selected)
    if (m_selectedMarkerName.isEmpty()) {
        auto activeSprite = m_sprites.last();
        if (QLineF(pos, QPointF(activeSprite->pivotX, activeSprite->pivotY)).length() < hitThreshold) {
            m_draggingPivot = true;
            setCursor(Qt::SizeAllCursor);
            prepareGeometryChange();
            for (auto& sprite : m_sprites) {
                sprite->pivotX = qRound(pos.x());
                sprite->pivotY = qRound(pos.y());
            }
            m_selectedMarkerName.clear();
            emit markerSelected("");
            m_selectedVertexIndex = -1;
            emit pivotChanged(activeSprite->pivotX, activeSprite->pivotY);
            update();
            event->accept();
            return;
        }
    }

    // 1. Check handles of selected marker first
    if (!m_selectedMarkerName.isEmpty()) {
        NamedPoint* p = getNamedPoint(m_selectedMarkerName);
        if (p) {
            if (p->kind == MarkerKind::Circle) {
                QPointF handlePos(p->x + p->radius, p->y);
                if (QLineF(pos, handlePos).length() < hitThreshold) {
                    m_dragMode = CircleRadius;
                    m_dragTargetName = p->name;
                    m_dragStartPos = pos;
                    m_dragOriginalRadius = p->radius;
                    setCursor(Qt::SizeHorCursor);
                    event->accept();
                    return;
                }
            } else if (p->kind == MarkerKind::Rectangle) {
                QPointF handlePos(p->x + p->w, p->y + p->h);
                if (QLineF(pos, handlePos).length() < hitThreshold) {
                    m_dragMode = RectResize;
                    m_dragTargetName = p->name;
                    m_dragStartPos = pos;
                    m_dragOriginalPos = QPoint(p->w, p->h);
                    setCursor(Qt::SizeFDiagCursor);
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
                // Check edges for insertion
                int edgeIdx = getPolygonHitEdge(p, pos);
                if (edgeIdx != -1) {
                    prepareGeometryChange();
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
            
            // Check body of selected marker (Priority 0 for marker)
            bool hit = false;
            if (p->kind == MarkerKind::Point) {
                hit = QLineF(pos, QPointF(p->x, p->y)).length() < hitThreshold;
            } else if (p->kind == MarkerKind::Circle) {
                hit = QLineF(pos, QPointF(p->x, p->y)).length() < p->radius;
            } else if (p->kind == MarkerKind::Rectangle) {
                hit = QRectF(p->x, p->y, p->w, p->h).contains(pos);
            } else if (p->kind == MarkerKind::Polygon) {
                QPolygonF poly;
                for(const auto& pt : p->polygonPoints) poly << pt;
                hit = poly.containsPoint(pos, Qt::OddEvenFill);
            }

            if (hit) {
                // Start move drag
                if (p->kind == MarkerKind::Polygon) {
                    m_dragMode = PolyMove;
                    m_dragOriginalPoly = p->polygonPoints;
                    setCursor(Qt::SizeAllCursor);
                } else {
                    m_dragMode = (p->kind == MarkerKind::Rectangle) ? RectMove : Point;
                    m_dragOriginalPos = QPoint(p->x, p->y);
                    setCursor(Qt::SizeAllCursor);
                }
                m_dragTargetName = p->name;
                m_dragStartPos = pos;
                event->accept();
                return;
            }
        }
    }

    // 2. Hit test markers to select/move
    // Only check active sprite for new marker selection
    auto activeSprite = m_sprites.last();
    for (int i = activeSprite->points.size() - 1; i >= 0; --i) {
        const auto& p = activeSprite->points[i];
        if (p.name == m_selectedMarkerName) {
            continue; // Already checked
        }

        bool hit = false;
        if (p.kind == MarkerKind::Point) {
            hit = QLineF(pos, QPointF(p.x, p.y)).length() < hitThreshold;
        } else if (p.kind == MarkerKind::Circle) {
            hit = QLineF(pos, QPointF(p.x, p.y)).length() < p.radius;
        } else if (p.kind == MarkerKind::Rectangle) {
            hit = QRectF(p.x, p.y, p.w, p.h).contains(pos);
        } else if (p.kind == MarkerKind::Polygon) {
            QPolygonF poly;
            for(const auto& pt : p.polygonPoints) poly << pt;
            hit = poly.containsPoint(pos, Qt::OddEvenFill);
        }

        if (hit) {
            if (m_selectedMarkerName != p.name) {
                m_selectedVertexIndex = -1;
            }
            m_selectedMarkerName = p.name;
            emit markerSelected(p.name);
            update();
            
            // Start move drag
            if (p.kind == MarkerKind::Polygon) {
                m_dragMode = PolyMove;
                m_dragOriginalPoly = p.polygonPoints;
                setCursor(Qt::SizeAllCursor);
            } else {
                m_dragMode = (p.kind == MarkerKind::Rectangle) ? RectMove : Point; // Point/Circle center move
                m_dragOriginalPos = QPoint(p.x, p.y);
                setCursor(Qt::SizeAllCursor);
            }
            m_dragTargetName = p.name;
            m_dragStartPos = pos;
            event->accept();
            return;
        }
    }
    
    // 3. Fallback: Pivot
    m_draggingPivot = true;
    setCursor(Qt::SizeAllCursor);
    prepareGeometryChange();
    for (auto& sprite : m_sprites) {
        sprite->pivotX = qRound(pos.x());
        sprite->pivotY = qRound(pos.y());
    }
    m_selectedMarkerName.clear();
    emit markerSelected("");
    m_selectedVertexIndex = -1;
    emit pivotChanged(activeSprite->pivotX, activeSprite->pivotY);
    update();
    event->accept();
}

void EditorOverlayItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    QPointF pos = event->pos();

    if (m_draggingPivot && !m_sprites.isEmpty()) {
        prepareGeometryChange();
        for (auto& sprite : m_sprites) {
            sprite->pivotX = qRound(pos.x());
            sprite->pivotY = qRound(pos.y());
        }
        auto activeSprite = m_sprites.last();
        emit pivotChanged(activeSprite->pivotX, activeSprite->pivotY);
        update();
        return;
    }

    if (m_dragMode != None) {
        NamedPoint* p = getNamedPoint(m_dragTargetName);
        if (!p) {
            return;
        }
        prepareGeometryChange();

        QPointF delta = pos - m_dragStartPos;

        if (m_dragMode == Point || m_dragMode == RectMove) {
            p->x = m_dragOriginalPos.x() + qRound(delta.x());
            p->y = m_dragOriginalPos.y() + qRound(delta.y());
        } else if (m_dragMode == CircleRadius) {
            double dist = QLineF(QPointF(p->x, p->y), pos).length();
            p->radius = qMax(1, qRound(dist));
        } else if (m_dragMode == RectResize) {
            p->w = qMax(1, m_dragOriginalPos.x() + qRound(delta.x()));
            p->h = qMax(1, m_dragOriginalPos.y() + qRound(delta.y()));
        } else if (m_dragMode == PolyVertex) {
            if (m_dragVertexIndex >= 0 && m_dragVertexIndex < p->polygonPoints.size()) {
                p->polygonPoints[m_dragVertexIndex] = pos.toPoint();
                // Sync anchor to first point
                if (m_dragVertexIndex == 0) {
                    p->x = p->polygonPoints[0].x();
                    p->y = p->polygonPoints[0].y();
                }
            }
        } else if (m_dragMode == PolyMove) {
            for (int i = 0; i < p->polygonPoints.size(); ++i) {
                p->polygonPoints[i] = m_dragOriginalPoly[i] + delta.toPoint();
            }
            if (!p->polygonPoints.isEmpty()) {
                p->x = p->polygonPoints[0].x();
                p->y = p->polygonPoints[0].y();
            }
        }
        
        emit markerChanged();
        update();
    }
}

void EditorOverlayItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    m_draggingPivot = false;
    m_dragMode = None;
    Q_UNUSED(event);
    unsetCursor();
}

Qt::CursorShape EditorOverlayItem::cursorForPosition(const QPointF& pos) const {
    if (m_sprites.isEmpty()) {
        return Qt::ArrowCursor;
    }
    const double scale = getScale();
    const double hitThreshold = 10.0 * scale;

    if (m_selectedMarkerName.isEmpty()) {
        const auto activeSprite = m_sprites.last();
        if (QLineF(pos, QPointF(activeSprite->pivotX, activeSprite->pivotY)).length() < hitThreshold) {
            return Qt::SizeAllCursor;
        }
        return Qt::CrossCursor;
    }

    const auto activeSprite = m_sprites.last();
    const NamedPoint* selectedPoint = nullptr;
    for (const auto& p : activeSprite->points) {
        if (p.name == m_selectedMarkerName) {
            selectedPoint = &p;
            break;
        }
    }
    if (selectedPoint) {
        if (selectedPoint->kind == MarkerKind::Circle) {
            const QPointF handlePos(selectedPoint->x + selectedPoint->radius, selectedPoint->y);
            if (QLineF(pos, handlePos).length() < hitThreshold) {
                return Qt::SizeHorCursor;
            }
        } else if (selectedPoint->kind == MarkerKind::Rectangle) {
            const QPointF handlePos(selectedPoint->x + selectedPoint->w, selectedPoint->y + selectedPoint->h);
            if (QLineF(pos, handlePos).length() < hitThreshold) {
                return Qt::SizeFDiagCursor;
            }
        } else if (selectedPoint->kind == MarkerKind::Polygon) {
            for (const auto& pt : selectedPoint->polygonPoints) {
                if (QLineF(pos, pt).length() < hitThreshold) {
                    return Qt::SizeAllCursor; // Move vertex
                }
            }
            if (getPolygonHitEdge(selectedPoint, pos) != -1) {
                return Qt::CrossCursor; // Insert vertex
            }
        }

        bool hitBody = false;
        if (selectedPoint->kind == MarkerKind::Point) {
            hitBody = QLineF(pos, QPointF(selectedPoint->x, selectedPoint->y)).length() < hitThreshold;
        } else if (selectedPoint->kind == MarkerKind::Circle) {
            hitBody = QLineF(pos, QPointF(selectedPoint->x, selectedPoint->y)).length() < selectedPoint->radius;
        } else if (selectedPoint->kind == MarkerKind::Rectangle) {
            hitBody = QRectF(selectedPoint->x, selectedPoint->y, selectedPoint->w, selectedPoint->h).contains(pos);
        } else if (selectedPoint->kind == MarkerKind::Polygon) {
            QPolygonF poly;
            for (const auto& pt : selectedPoint->polygonPoints) {
                poly << pt;
            }
            hitBody = poly.containsPoint(pos, Qt::OddEvenFill);
        }
        if (hitBody) {
            return Qt::SizeAllCursor; // Move marker or polygon
        }
    }

    for (int i = activeSprite->points.size() - 1; i >= 0; --i) {
        const auto& p = activeSprite->points[i];
        bool hit = false;
        if (p.kind == MarkerKind::Point) {
            hit = QLineF(pos, QPointF(p.x, p.y)).length() < hitThreshold;
        } else if (p.kind == MarkerKind::Circle) {
            hit = QLineF(pos, QPointF(p.x, p.y)).length() < p.radius;
        } else if (p.kind == MarkerKind::Rectangle) {
            hit = QRectF(p.x, p.y, p.w, p.h).contains(pos);
        } else if (p.kind == MarkerKind::Polygon) {
            QPolygonF poly;
            for (const auto& pt : p.polygonPoints) {
                poly << pt;
            }
            hit = poly.containsPoint(pos, Qt::OddEvenFill);
        }
        if (hit) {
            return Qt::SizeAllCursor;
        }
    }
    return Qt::CrossCursor;
}

void EditorOverlayItem::updateHoverCursor(const QPointF& pos) {
    if (m_draggingPivot || m_dragMode != None) {
        return;
    }
    setCursor(cursorForPosition(pos));
}

void EditorOverlayItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event) {
    updateHoverCursor(event->pos());
    QGraphicsObject::hoverMoveEvent(event);
}

void EditorOverlayItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    if (!m_draggingPivot && m_dragMode == None) {
        unsetCursor();
    }
    QGraphicsObject::hoverLeaveEvent(event);
}

bool EditorOverlayItem::removeSelectedVertex() {
    if (m_selectedMarkerName.isEmpty()) {
        return false;
    }
    NamedPoint* p = getNamedPoint(m_selectedMarkerName);
    if (!p || p->kind != MarkerKind::Polygon) {
        return false;
    }
    
    if (m_selectedVertexIndex >= 0 && m_selectedVertexIndex < p->polygonPoints.size()) {
        if (p->polygonPoints.size() <= 3) {
            return false; // Keep minimum 3 vertices
        }
        prepareGeometryChange();
        p->polygonPoints.remove(m_selectedVertexIndex);
        if (m_selectedVertexIndex >= p->polygonPoints.size()) {
            m_selectedVertexIndex = p->polygonPoints.size() - 1;
        }
        emit markerChanged();
        update();
        return true;
    }
    return false;
}

void EditorOverlayItem::updateLayout() {
    prepareGeometryChange();
    update();
}
