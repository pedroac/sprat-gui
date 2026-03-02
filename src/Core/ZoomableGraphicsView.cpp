#include "ZoomableGraphicsView.h"
#include <QScrollBar>
#include <QApplication>
#include <QtGlobal>

ZoomableGraphicsView::ZoomableGraphicsView(QWidget* parent) : QGraphicsView(parent) {
    setRenderHint(QPainter::Antialiasing, false);
    setRenderHint(QPainter::SmoothPixmapTransform, false);
    setDragMode(QGraphicsView::NoDrag);
    setFocusPolicy(Qt::StrongFocus);
}

void ZoomableGraphicsView::setZoom(double zoom) {
    zoom = qBound(m_minZoom, zoom, m_maxZoom);
    QPointF center = mapToScene(viewport()->rect().center());
    resetTransform();
    scale(zoom, zoom);
    centerOn(center);
}

double ZoomableGraphicsView::zoom() const {
    return transform().m11();
}

void ZoomableGraphicsView::setZoomRange(double min, double max) {
    m_minZoom = min;
    m_maxZoom = max;
}

void ZoomableGraphicsView::initialFit() {
    if (!scene() || scene()->sceneRect().isEmpty()) {
        return;
    }
    const QRectF sceneRect = scene()->sceneRect();
    const QRect viewportRect = viewport()->rect();
    if (viewportRect.isEmpty()) {
        return;
    }
    
    double newZoom = 1.0;
    const bool isLarger = sceneRect.width() > viewportRect.width() || sceneRect.height() > viewportRect.height();
    
    if (isLarger) {
        if (sceneRect.height() > sceneRect.width()) {
            // Portrait: fit width to viewport, allow vertical scrolling
            newZoom = (double)viewportRect.width() / sceneRect.width();
        } else {
            // Landscape or Square: fit height to viewport, allow horizontal scrolling
            newZoom = (double)viewportRect.height() / sceneRect.height();
        }
    } else {
        // Scene fits in viewport: fit it as large as possible without scrolling
        fitInView(sceneRect, Qt::KeepAspectRatio);
        newZoom = zoom();
    }
    
    newZoom = qBound(m_minZoom, newZoom, m_maxZoom);
    
    // Use resetTransform logic for initial fit to avoid "anchored" behavior which might be confusing on first load
    resetTransform();
    scale(newZoom, newZoom);
    centerOn(sceneRect.center());
    
    emit zoomChanged(newZoom);
}

void ZoomableGraphicsView::wheelEvent(QWheelEvent* event) {
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
        newZoom = qBound(m_minZoom, newZoom, m_maxZoom);

        double relativeScale = newZoom / oldZoom;
        scale(relativeScale, relativeScale);
        
        emit zoomChanged(newZoom);
        event->accept();
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

void ZoomableGraphicsView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = true;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void ZoomableGraphicsView::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = false;
        if (!m_isPanning) setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void ZoomableGraphicsView::focusOutEvent(QFocusEvent* event) {
    m_spacePressed = false;
    m_isPanning = false;
    setCursor(Qt::ArrowCursor);
    QGraphicsView::focusOutEvent(event);
}

void ZoomableGraphicsView::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->pos();
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spacePressed)) {
        m_isPanning = true;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void ZoomableGraphicsView::mouseMoveEvent(QMouseEvent* event) {
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

void ZoomableGraphicsView::mouseReleaseEvent(QMouseEvent* event) {
    if (m_isPanning) {
        m_isPanning = false;
        setCursor(m_spacePressed ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ZoomableGraphicsView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (!m_isZoomManual) {
        initialFit();
    }
}
