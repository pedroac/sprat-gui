#include "PreviewCanvas.h"
#include <QGraphicsPixmapItem>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QGraphicsRectItem>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QApplication>

PreviewCanvas::PreviewCanvas(QWidget* parent) : QGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing, false);
    setRenderHint(QPainter::SmoothPixmapTransform, false);
    setDragMode(QGraphicsView::NoDrag);
    setBackgroundBrush(QColor(90, 90, 90));

    m_overlay = new EditorOverlayItem();
    m_scene->addItem(m_overlay);
    
    connect(m_overlay, &EditorOverlayItem::pivotChanged, this, &PreviewCanvas::pivotChanged);
}

void PreviewCanvas::setSprites(const QList<SpritePtr>& sprites) {
    m_sprites = sprites;
    m_overlay->setSprites(sprites);
    
    // Clear old items
    for (auto* item : m_imageItems) delete item;
    m_imageItems.clear();
    for (auto* item : m_borderItems) delete item;
    m_borderItems.clear();

    if (!sprites.isEmpty()) {
        QPen borderPen(m_settings.borderColor, 2, m_settings.borderStyle);
        borderPen.setCosmetic(true);

        for (const auto& sprite : sprites) {
            QPixmap pix(sprite->path);
            
            auto* bgItem = new QGraphicsRectItem(pix.rect());
            bgItem->setBrush(m_settings.frameColor);
            bgItem->setPen(Qt::NoPen);
            bgItem->setZValue(-1);
            m_scene->addItem(bgItem);
            m_borderItems.append(bgItem);

            auto* imageItem = new QGraphicsPixmapItem(pix);
            m_scene->addItem(imageItem);
            m_imageItems.append(imageItem);

            auto* borderItem = new QGraphicsRectItem(pix.rect());
            borderItem->setPen(borderPen);
            m_scene->addItem(borderItem);
            m_borderItems.append(borderItem);
        }
        // Expand scene rect to allow markers outside image are visible/reachable
        m_scene->setSceneRect(QRectF()); // Reset to auto-calculate based on items
        // Use size of the first sprite for overlay scene size reference if needed, 
        // but EditorOverlayItem now calculates bounds dynamically.
        if (!sprites.isEmpty()) {
             QPixmap pix(sprites.first()->path);
             m_overlay->setSceneSize(pix.size());
        }
    } else {
        m_scene->setSceneRect(QRectF());
    }
}

void PreviewCanvas::setZoom(double zoom) {
    QPointF center = mapToScene(viewport()->rect().center());
    resetTransform();
    scale(zoom, zoom);
    centerOn(center);
    // Trigger redraw of overlay to adjust cosmetic pens if needed
    m_overlay->update(); 
}

void PreviewCanvas::centerContent() {
    if (!m_imageItems.isEmpty()) {
        centerOn(m_imageItems.first());
    }
}

void PreviewCanvas::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        double zoom = transform().m11();
        const double scaleFactor = 1.15;
        if (event->angleDelta().y() > 0) {
            zoom *= scaleFactor;
        } else {
            zoom /= scaleFactor;
        }
        if (zoom < 0.1) zoom = 0.1;
        if (zoom > 16.0) zoom = 16.0;
        setZoom(zoom);
        emit zoomChanged(zoom);
        event->accept();
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

void PreviewCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = true;
        setCursor(Qt::OpenHandCursor);
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (m_overlay->removeSelectedVertex()) {
            event->accept();
            return;
        }
    }
    QGraphicsView::keyPressEvent(event);
}

void PreviewCanvas::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = false;
        if (!m_isPanning) setCursor(Qt::ArrowCursor);
    }
    QGraphicsView::keyReleaseEvent(event);
}

void PreviewCanvas::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->pos();
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spacePressed)) {
        m_isPanning = true;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void PreviewCanvas::mouseMoveEvent(QMouseEvent* event) {
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

void PreviewCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (m_isPanning) {
        m_isPanning = false;
        setCursor(m_spacePressed ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void PreviewCanvas::contextMenuEvent(QContextMenuEvent* event) {
    if (m_sprites.isEmpty()) {
        return;
    }
    
    QMenu menu(this);
    QAction* copyAction = menu.addAction("Copy Image");
    QAction* selected = menu.exec(event->globalPos());
    if (selected == copyAction) {
        QApplication::clipboard()->setImage(QImage(m_sprites.first()->path));
    }
}

void PreviewCanvas::setSettings(const AppSettings& settings) {
    m_settings = settings;
    setBackgroundBrush(settings.canvasColor);
    
    QPen borderPen(settings.borderColor, 2, settings.borderStyle);
    borderPen.setCosmetic(true);

    for (auto* item : m_borderItems) {
        if (item->zValue() == -1) item->setBrush(settings.frameColor); // bg item
        else item->setPen(borderPen); // border item
    }
}