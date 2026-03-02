#include "FrameDetectionCanvas.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QAction>
#include <QScrollBar>
#include <QApplication>
#include <QToolTip>
#include <QPainter>
#include "ViewUtils.h"

FrameDetectionCanvas::FrameDetectionCanvas(QWidget* parent) : ZoomableGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setMouseTracking(true);
    
    setBackgroundBrush(QBrush(createCheckerboardPixmap()));
    
    setZoomRange(0.1, 50.0);

    m_splitLineItem = new QGraphicsLineItem();
    QPen splitPen(Qt::red, 2, Qt::DashLine);
    m_splitLineItem->setPen(splitPen);
    m_splitLineItem->setZValue(2.0);
    m_splitLineItem->hide();
    m_scene->addItem(m_splitLineItem);
}

void FrameDetectionCanvas::setTransparentColor(const QColor& color) {
    m_transparentColor = color;
    if (!m_image.isNull()) {
        setImage(m_image);
    }
}

void FrameDetectionCanvas::setImage(const QPixmap& image) {
    m_image = image;
    if (m_imageItem) {
        m_scene->removeItem(m_imageItem);
        delete m_imageItem;
        m_imageItem = nullptr;
    }

    QPixmap pixmap = m_image;
    if (m_transparentColor.isValid()) {
        QImage img = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
        QRgb target = m_transparentColor.rgb();
        int tr = qRed(target);
        int tg = qGreen(target);
        int tb = qBlue(target);
        const int tolerance = 15; // Handle JPEG artifacts

        for (int y = 0; y < img.height(); ++y) {
            QRgb* scanLine = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                QRgb pixel = scanLine[x];
                if (qAbs(qRed(pixel) - tr) <= tolerance &&
                    qAbs(qGreen(pixel) - tg) <= tolerance &&
                    qAbs(qBlue(pixel) - tb) <= tolerance) {
                    scanLine[x] = 0;
                }
            }
        }
        pixmap = QPixmap::fromImage(img);
    }

    m_imageItem = m_scene->addPixmap(pixmap);
    m_imageItem->setZValue(0);
    m_scene->setSceneRect(m_image.rect());
}

void FrameDetectionCanvas::setFrames(const QVector<QRect>& frames) {
    m_frames = frames;
    m_selected.assign(frames.size(), false);
    m_hovered.assign(frames.size(), false);
    drawFrameRectangles();
}

void FrameDetectionCanvas::setSplitMode(bool enabled) {
    m_splitMode = enabled;
    if (!m_splitMode) {
        m_splitLineItem->hide();
        m_splitFrameIndex = -1;
        viewport()->setCursor(Qt::ArrowCursor);
    }
}

void FrameDetectionCanvas::drawFrameRectangles() {
    for (auto* item : m_frameItems) {
        m_scene->removeItem(item);
        delete item;
    }
    m_frameItems.clear();

    for (int i = 0; i < m_frames.size(); ++i) {
        auto* item = new QGraphicsRectItem(m_frames[i]);
        item->setZValue(1.0);
        item->setData(0, i);
        m_scene->addItem(item);
        m_frameItems.append(item);
    }
    updateFrameVisuals();
}

void FrameDetectionCanvas::updateFrameVisuals() {
    for (int i = 0; i < m_frameItems.size(); ++i) {
        QColor color = m_selected[i] ? m_settings.detectionSelectedColor : m_settings.borderColor;
        Qt::PenStyle style = m_settings.borderStyle;
        
        // If hovered, toggle between solid and dashed (or just use dash if default is solid)
        if (m_hovered[i]) {
            style = (style == Qt::SolidLine) ? Qt::DashLine : Qt::SolidLine;
        }
        
        QPen pen(color, 3, style);
        pen.setCosmetic(true);
        m_frameItems[i]->setPen(pen);
    }
}

void FrameDetectionCanvas::setSettings(const AppSettings& settings) {
    m_settings = settings;
    if (m_settings.showCheckerboard) {
        setBackgroundBrush(QBrush(createCheckerboardPixmap(m_settings.spriteFrameColor)));
    } else {
        setBackgroundBrush(m_settings.spriteFrameColor);
    }
    updateFrameVisuals();
}

void FrameDetectionCanvas::mousePressEvent(QMouseEvent* event) {
    ZoomableGraphicsView::mousePressEvent(event);
    if (m_isPanning) return;

    if (event->button() == Qt::LeftButton) {
        QPointF scenePos = mapToScene(event->pos());

        if (m_splitLineItem->isVisible() && m_splitFrameIndex != -1) {
            splitFrame(m_splitFrameIndex, m_splitOrientation, m_splitPos);
            m_splitLineItem->hide();
            m_splitFrameIndex = -1;
            return;
        }

        for (int i = 0; i < m_frames.size(); ++i) {
            if (m_selected[i]) {
                ResizeHandle handle = getResizeHandle(event->pos(), m_frames[i]);
                if (handle != NoHandle) {
                    m_dragging = true;
                    m_isResizing = true;
                    m_resizeHandle = handle;
                    m_draggedFrameIndex = i;
                    m_dragOriginalRect = m_frames[i];
                    m_dragStartPos = event->pos();
                    updateResizeCursor(handle);
                    return;
                }
            }
        }

        QGraphicsItem* item = m_scene->itemAt(scenePos, transform());
        if (auto* rectItem = dynamic_cast<QGraphicsRectItem*>(item)) {
            int idx = rectItem->data(0).toInt();
            if (event->modifiers() & Qt::ControlModifier) {
                m_selected[idx] = !m_selected[idx];
            } else {
                m_selected.fill(false);
                m_selected[idx] = true;
            }
            updateFrameVisuals();
            m_draggedFrameIndex = idx;
            m_dragOriginalRect = m_frames[idx];
            m_dragStartScenePos = scenePos;
            m_dragging = true;
            m_isResizing = false;
            viewport()->setCursor(Qt::SizeAllCursor);
            return;
        }

        createFrame(scenePos);
        return;
    }
}

void FrameDetectionCanvas::mouseMoveEvent(QMouseEvent* event) {
    ZoomableGraphicsView::mouseMoveEvent(event);
    if (m_isPanning) return;

    QPointF scenePos = mapToScene(event->pos());

    if (m_dragging && m_draggedFrameIndex >= 0) {
        if (m_isResizing) resizeFrame(m_draggedFrameIndex, m_resizeHandle, scenePos);
        else moveFrame(m_draggedFrameIndex, scenePos - m_dragStartScenePos);
        return;
    }

    if (m_splitMode || (event->modifiers() & Qt::ShiftModifier)) {
        for (int i = 0; i < m_frames.size(); ++i) {
            ResizeHandle handle = getResizeHandle(event->pos(), m_frames[i]);
            if (handle == Left || handle == Right || handle == Top || handle == Bottom) {
                m_splitFrameIndex = i;
                const QRect& r = m_frames[i];
                if (handle == Left || handle == Right) {
                    m_splitOrientation = Qt::Horizontal;
                    m_splitPos = qRound(scenePos.y());
                    m_splitLineItem->setLine(r.left(), m_splitPos, r.right(), m_splitPos);
                } else {
                    m_splitOrientation = Qt::Vertical;
                    m_splitPos = qRound(scenePos.x());
                    m_splitLineItem->setLine(m_splitPos, r.top(), m_splitPos, r.bottom());
                }
                m_splitLineItem->show();
                viewport()->setCursor(Qt::CrossCursor);
                return;
            }
        }
    }
    m_splitLineItem->hide();

    for (int i = 0; i < m_frames.size(); ++i) {
        if (m_selected[i]) {
            ResizeHandle h = getResizeHandle(event->pos(), m_frames[i]);
            if (h != NoHandle) {
                updateResizeCursor(h);
                return;
            }
        }
    }

    QGraphicsItem* item = m_scene->itemAt(scenePos, transform());
    bool changed = false;
    for (int i = 0; i < m_frameItems.size(); ++i) {
        bool h = (item == m_frameItems[i]);
        if (m_hovered[i] != h) { m_hovered[i] = h; changed = true; }
    }
    if (changed) updateFrameVisuals();

    if (findFrameAt(scenePos.toPoint()) >= 0) viewport()->setCursor(Qt::PointingHandCursor);
    else viewport()->setCursor(Qt::ArrowCursor);
}

void FrameDetectionCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (m_dragging) {
        m_dragging = false;
        m_draggedFrameIndex = -1;
        viewport()->setCursor(Qt::ArrowCursor);
        return;
    }
    ZoomableGraphicsView::mouseReleaseEvent(event);
}

void FrameDetectionCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelectedFrames();
        return;
    }
    ZoomableGraphicsView::keyPressEvent(event);
}

void FrameDetectionCanvas::contextMenuEvent(QContextMenuEvent* event) {
    QPointF scenePos = mapToScene(mapFromGlobal(event->globalPos()));
    
    QMenu menu(this);
    QAction* deleteAction = menu.addAction(tr("Delete"));
    bool hasSelection = m_selected.contains(true);
    deleteAction->setEnabled(hasSelection);
    connect(deleteAction, &QAction::triggered, this, &FrameDetectionCanvas::deleteSelectedFrames);

    menu.addSeparator();
    QAction* splitAction = menu.addAction(tr("Split Mode"));
    splitAction->setCheckable(true);
    splitAction->setChecked(m_splitMode);
    connect(splitAction, &QAction::triggered, this, &FrameDetectionCanvas::setSplitMode);

    QAction* createAction = menu.addAction(tr("Create New Frame"));
    connect(createAction, &QAction::triggered, this, [this, scenePos](){ createDefaultFrame(scenePos); });

    menu.exec(event->globalPos());
}

void FrameDetectionCanvas::splitFrame(int index, Qt::Orientation orientation, int pos) {
    if (index < 0 || index >= m_frames.size()) return;
    QRect original = m_frames[index];
    QRect first, second;
    if (orientation == Qt::Horizontal) {
        first = original; first.setBottom(pos - 1);
        second = original; second.setTop(pos);
    } else {
        first = original; first.setRight(pos - 1);
        second = original; second.setLeft(pos);
    }
    if (!first.isValid() || !second.isValid() || first.isEmpty() || second.isEmpty()) return;
    m_frames.removeAt(index);
    m_selected.removeAt(index);
    m_hovered.removeAt(index);
    m_frames.append(first); m_selected.append(true); m_hovered.append(false);
    m_frames.append(second); m_selected.append(true); m_hovered.append(false);
    drawFrameRectangles();
}

void FrameDetectionCanvas::createFrame(const QPointF& scenePos) {
    m_selected.fill(false);
    QRect r(scenePos.toPoint(), QSize(0, 0));
    m_frames.append(r);
    m_selected.append(true);
    m_hovered.append(false);
    drawFrameRectangles();
    m_draggedFrameIndex = m_frames.size() - 1;
    m_dragOriginalRect = r;
    m_dragStartScenePos = scenePos;
    m_dragging = true;
    m_isResizing = true;
    m_resizeHandle = BottomRight;
    viewport()->setCursor(Qt::SizeFDiagCursor);
}

void FrameDetectionCanvas::createDefaultFrame(const QPointF& center) {
    m_selected.fill(false);
    QRect r(0, 0, 32, 32);
    r.moveCenter(center.toPoint());
    r = r.intersected(m_image.rect());
    if (r.isEmpty()) return;
    m_frames.append(r);
    m_selected.append(true);
    m_hovered.append(false);
    drawFrameRectangles();
}

void FrameDetectionCanvas::deleteSelectedFrames() {
    for (int i = m_frames.size() - 1; i >= 0; --i) {
        if (m_selected[i]) {
            m_frames.removeAt(i);
            m_selected.removeAt(i);
            m_hovered.removeAt(i);
        }
    }
    drawFrameRectangles();
}

void FrameDetectionCanvas::moveFrame(int idx, const QPointF& delta) {
    m_frames[idx] = m_dragOriginalRect.translated(delta.toPoint());
    m_frames[idx] = m_frames[idx].intersected(m_image.rect());
    m_frameItems[idx]->setRect(m_frames[idx]);
}

void FrameDetectionCanvas::resizeFrame(int idx, ResizeHandle h, const QPointF& pos) {
    QRect& r = m_frames[idx];
    r = m_dragOriginalRect;
    switch (h) {
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
    r = r.normalized().intersected(m_image.rect());
    m_frameItems[idx]->setRect(r);
}

FrameDetectionCanvas::ResizeHandle FrameDetectionCanvas::getResizeHandle(const QPoint& pos, const QRect& r) const {
    const double s = 8.0 / transform().m11();
    QPointF p = mapToScene(pos);
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

void FrameDetectionCanvas::updateResizeCursor(ResizeHandle h) {
    switch (h) {
        case TopLeft: case BottomRight: viewport()->setCursor(Qt::SizeFDiagCursor); break;
        case TopRight: case BottomLeft: viewport()->setCursor(Qt::SizeBDiagCursor); break;
        case Top: case Bottom: viewport()->setCursor(Qt::SizeVerCursor); break;
        case Left: case Right: viewport()->setCursor(Qt::SizeHorCursor); break;
        default: viewport()->setCursor(Qt::ArrowCursor); break;
    }
}

int FrameDetectionCanvas::findFrameAt(const QPoint& p) {
    for (int i = 0; i < m_frames.size(); ++i) if (m_frames[i].contains(p)) return i;
    return -1;
}
