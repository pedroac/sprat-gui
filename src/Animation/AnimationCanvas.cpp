#include "AnimationCanvas.h"
#include <QScrollBar>
#include <QApplication>

AnimationCanvas::AnimationCanvas(QWidget* parent) : QGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing, false);
    setRenderHint(QPainter::SmoothPixmapTransform, false);
    setDragMode(QGraphicsView::NoDrag);
    setBackgroundBrush(QColor(90, 90, 90));
    setFocusPolicy(Qt::StrongFocus);

    m_pixmapItem = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixmapItem);
}

void AnimationCanvas::setPixmap(const QPixmap& pixmap) {
    m_pixmapItem->setPixmap(pixmap);
    m_scene->setSceneRect(m_pixmapItem->boundingRect());
}

void AnimationCanvas::setZoom(double zoom) {
    QPointF center = mapToScene(viewport()->rect().center());
    resetTransform();
    scale(zoom, zoom);
    centerOn(center);
}

double AnimationCanvas::zoom() const {
    return transform().m11();
}

void AnimationCanvas::initialFit() {
    QRectF sceneRect = m_scene->sceneRect();
    if (sceneRect.isEmpty()) {
        return;
    }
    const QRect viewportRect = viewport()->rect();
    if (viewportRect.isEmpty()) {
        return;
    }
    
    if (sceneRect.width() > viewportRect.width() || sceneRect.height() > viewportRect.height()) {
        fitInView(sceneRect, Qt::KeepAspectRatio);
        emit zoomChanged(zoom());
    } else {
        resetTransform();
        emit zoomChanged(1.0);
        centerOn(sceneRect.center());
    }
}

void AnimationCanvas::centerContent() {
    if (m_pixmapItem) {
        centerOn(m_pixmapItem);
    }
}

void AnimationCanvas::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        m_isZoomManual = true;
        double oldZoom = zoom();
        double newZoom = oldZoom;
        const double scaleFactor = 1.15;
        if (event->angleDelta().y() > 0) {
            newZoom *= scaleFactor;
        } else {
            newZoom /= scaleFactor;
        }
        if (newZoom < 0.1) newZoom = 0.1;
        if (newZoom > 16.0) newZoom = 16.0;

        double relativeScale = newZoom / oldZoom;
        scale(relativeScale, relativeScale);
        
        emit zoomChanged(newZoom);
        event->accept();
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

void AnimationCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = true;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void AnimationCanvas::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = false;
        if (!m_isPanning) setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void AnimationCanvas::focusOutEvent(QFocusEvent* event) {
    m_spacePressed = false;
    m_isPanning = false;
    setCursor(Qt::ArrowCursor);
    QGraphicsView::focusOutEvent(event);
}

void AnimationCanvas::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->pos();
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spacePressed)) {
        m_isPanning = true;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void AnimationCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (m_isPanning) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void AnimationCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (m_isPanning) {
        m_isPanning = false;
        setCursor(m_spacePressed ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void AnimationCanvas::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (!m_isZoomManual) {
        initialFit();
    }
}
