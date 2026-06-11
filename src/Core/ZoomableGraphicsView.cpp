#include "ZoomableGraphicsView.h"
#include "AppConstants.h"
#include <QScrollBar>
#include <QApplication>
#include <QTimer>
#include <QtGlobal>
#include "ViewUtils.h"

#ifdef Q_OS_WASM
#include <emscripten.h>
#endif

ZoomableGraphicsView::ZoomableGraphicsView(QWidget* parent) : QGraphicsView(parent) {
    setRenderHint(QPainter::Antialiasing, false);
    setRenderHint(QPainter::SmoothPixmapTransform, false);
    setDragMode(QGraphicsView::NoDrag);
    setFocusPolicy(Qt::StrongFocus);
}

void ZoomableGraphicsView::setZoom(double zoom) {
    zoom = qBound(m_minZoom, zoom, m_maxZoom);
    if (qFuzzyCompare(zoom, this->zoom())) return;
    QPointF center = mapToScene(viewport()->rect().center());
    resetTransform();
    scale(zoom, zoom);
    centerOn(center);
    emit zoomChanged(zoom);
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
    // Inset by 1 px on each side so the computed zoom never places the scene
    // exactly at the viewport boundary — that exact-fit condition causes Qt to
    // oscillate the scrollbar on/off indefinitely.
    const QRect viewportRect = viewport()->rect().adjusted(1, 1, -1, -1);
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
        // Scene fits in the adjusted viewport: scale up as large as possible.
        // Manual calculation uses the same adjusted rect so the 1-px breathing
        // room applies here too — fitInView() uses the real viewport internally
        // and would risk the same oscillation on exact-fit aspect ratios.
        const double fw = (double)viewportRect.width()  / sceneRect.width();
        const double fh = (double)viewportRect.height() / sceneRect.height();
        newZoom = qMin(fw, fh);
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
        const double scaleFactor = 1.15;
        double newZoom = event->angleDelta().y() > 0 ? oldZoom * scaleFactor : oldZoom / scaleFactor;
        newZoom = qBound(m_minZoom, newZoom, m_maxZoom);

        if (!qFuzzyCompare(newZoom, oldZoom)) {
            scale(newZoom / oldZoom, newZoom / oldZoom);
            emit zoomChanged(newZoom);
        }
        event->accept();
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

void ZoomableGraphicsView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = true;
        if (!m_isPanning) {
            // Override cursor takes precedence over QGraphicsItem cursors,
            // ensuring the pan cursor is visible even over interactive handles.
            if (m_hasOverrideCursor)
                QApplication::changeOverrideCursor(Qt::SizeAllCursor);
            else {
                QApplication::setOverrideCursor(Qt::SizeAllCursor);
                m_hasOverrideCursor = true;
            }
        }
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void ZoomableGraphicsView::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = false;
        if (!m_isPanning && m_hasOverrideCursor) {
            QApplication::restoreOverrideCursor();
            m_hasOverrideCursor = false;
        }
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void ZoomableGraphicsView::enterEvent(QEnterEvent* event) {
    setFocus();
    QGraphicsView::enterEvent(event);
}

void ZoomableGraphicsView::focusOutEvent(QFocusEvent* event) {
    if (m_hasOverrideCursor) {
        QApplication::restoreOverrideCursor();
        m_hasOverrideCursor = false;
    }
    m_spacePressed = false;
    m_isPanning = false;
    QGraphicsView::focusOutEvent(event);
}

void ZoomableGraphicsView::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->pos();
    setFocus();  // Ensure this widget receives keyboard focus
    activateWindow();
#ifdef Q_OS_WASM
    EM_ASM({
        console.log("[Focus] ZoomableGraphicsView clicked, setting focus. Widget: " + UTF8ToString($0));
    }, metaObject()->className());
#endif
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spacePressed)) {
        m_isPanning = true;
        if (m_hasOverrideCursor)
            QApplication::changeOverrideCursor(Qt::ClosedHandCursor);
        else {
            QApplication::setOverrideCursor(Qt::ClosedHandCursor);
            m_hasOverrideCursor = true;
        }
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
        if (m_spacePressed && m_hasOverrideCursor) {
            QApplication::changeOverrideCursor(Qt::SizeAllCursor);
        } else if (m_hasOverrideCursor) {
            QApplication::restoreOverrideCursor();
            m_hasOverrideCursor = false;
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ZoomableGraphicsView::setPendingRestore(double zoom, const QPointF& sceneCenter) {
    const double clampedZoom = qBound(m_minZoom, zoom, m_maxZoom);
    m_isZoomManual = true;
    m_pendingRestoreZoom   = clampedZoom;
    m_pendingRestoreCenter = sceneCenter;

#ifndef Q_OS_WASM
    // Suppress viewport paints while the stale scroll position is in effect.
    // show()/setSizes() schedule paint events that can be dispatched before
    // this function returns, rendering the content at the wrong (old) position.
    // A 0-ms timer fires only after all queued events — including those paints
    // — have drained, at which point the viewport has its correct geometry and
    // we can safely restore zoom + scroll without any visible flash.
    viewport()->setUpdatesEnabled(false);
    QTimer::singleShot(0, this, [this]() {
        if (m_pendingRestoreZoom > 0.0) {
            if (m_resizeTimer) m_resizeTimer->stop();
            // On desktop, resizeEvent() already called QGraphicsView::resizeEvent()
            // synchronously, so scroll-bar ranges are already up to date here.
            resetTransform();
            scale(m_pendingRestoreZoom, m_pendingRestoreZoom);
            centerOn(m_pendingRestoreCenter);
            const double z = m_pendingRestoreZoom;
            m_pendingRestoreZoom = -1.0;
            emit zoomChanged(z);
        }
        viewport()->setUpdatesEnabled(true);
        viewport()->update();
    });
#endif
}

void ZoomableGraphicsView::resizeEvent(QResizeEvent* event) {
#ifdef Q_OS_WASM
    // In WASM, directly calling the base class or doing work here can cause
    // a crash if a resize event arrives while the app is suspended.
    // Instead, we do NOTHING but start a debounced timer. The timer's
    // callback will do the actual work when it's safe.
    m_pendingResizeSize = event->size();
    m_pendingResizeOldSize = event->oldSize();

    if (!m_resizeTimer) {
        m_resizeTimer = new QTimer(this);
        m_resizeTimer->setSingleShot(true);
        m_resizeTimer->setInterval(AppConstants::kResizeDebounceMs);
        connect(m_resizeTimer, &QTimer::timeout, this, [this]() {
            if (jsIsAsyncBusy() || jsHaveJspi()) {
                m_resizeTimer->start(); // Try again if busy
                return;
            }
            QResizeEvent dummyEvent(m_pendingResizeSize, m_pendingResizeOldSize);
            QGraphicsView::resizeEvent(&dummyEvent);
            if (m_pendingRestoreZoom > 0.0) {
                const double z = m_pendingRestoreZoom;
                const QPointF c = m_pendingRestoreCenter;
                m_pendingRestoreZoom = -1.0;
                resetTransform();
                scale(z, z);
                centerOn(c);
                m_isZoomManual = true;
                emit zoomChanged(z);
            } else if (!m_isZoomManual) {
                initialFit();
            }
        });
    }
    m_resizeTimer->start();
#else
    // On desktop, call the base class directly — the debounce is a WASM-only
    // concern. Calling it here keeps scroll-bar ranges immediately current so
    // that any subsequent centerOn() / setPendingRestore() is not clamped.
    QGraphicsView::resizeEvent(event);
    if (m_pendingRestoreZoom > 0.0) {
        // A restore is already queued via setPendingRestore(); leave it alone.
    } else if (!m_isZoomManual) {
        initialFit();
    }
#endif
}
