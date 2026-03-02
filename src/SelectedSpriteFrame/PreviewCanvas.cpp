#include "PreviewCanvas.h"
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QApplication>

PreviewCanvas::PreviewCanvas(QWidget* parent) : ZoomableGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setBackgroundBrush(QColor(90, 90, 90));

    m_overlay = new EditorOverlayItem();
    m_scene->addItem(m_overlay);
    
    connect(m_overlay, &EditorOverlayItem::pivotChanged, this, &PreviewCanvas::pivotChanged);
}

void PreviewCanvas::setSprites(const QList<SpritePtr>& sprites) {
    m_sprites = sprites;
    m_overlay->setSprites(sprites);
    
    for (auto* item : m_imageItems) delete item;
    m_imageItems.clear();
    for (auto* item : m_borderItems) delete item;
    m_borderItems.clear();

    if (!sprites.isEmpty()) {
        QPen borderPen(m_settings.borderColor, 2, m_settings.borderStyle);
        borderPen.setCosmetic(true);

        QRectF totalRect;
        for (const auto& sprite : sprites) {
            QPixmap pix(sprite->path);
            if (pix.isNull()) continue;

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

            totalRect = totalRect.united(pix.rect());
        }
        
        m_scene->setSceneRect(totalRect);
        
        if (!sprites.isEmpty()) {
             QPixmap pix(sprites.first()->path);
             m_overlay->setSceneSize(pix.size());
        }
    } else {
        m_scene->setSceneRect(QRectF());
    }
}

void PreviewCanvas::setZoom(double zoom) {
    ZoomableGraphicsView::setZoom(zoom);
    m_overlay->update(); 
}

void PreviewCanvas::centerContent() {
    if (!m_imageItems.isEmpty()) {
        centerOn(m_imageItems.first());
    }
}

void PreviewCanvas::initialFit() {
    ZoomableGraphicsView::initialFit();
    m_overlay->update();
}

QPointF PreviewCanvas::viewportCenterInScene() const {
    return mapToScene(viewport()->rect().center());
}

void PreviewCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (m_overlay->removeSelectedVertex()) {
            event->accept();
            return;
        }
        if (m_overlay->deleteSelectedMarker()) {
            event->accept();
            return;
        }
    }
    ZoomableGraphicsView::keyPressEvent(event);
}

void PreviewCanvas::contextMenuEvent(QContextMenuEvent* event) {
    if (m_sprites.isEmpty()) return;
    if (m_overlay && m_overlay->consumeSuppressedViewContextMenu()) {
        event->accept();
        return;
    }
    if (m_overlay && m_overlay->hasContextMenuTargetAt(mapToScene(event->pos()))) {
        event->accept();
        return;
    }
    
    QMenu menu(this);
    QAction* copyAction = menu.addAction(tr("Copy Image"));
    menu.addSeparator();
    QAction* applyPivotAction = menu.addAction(tr("Apply Pivot to Selected Frames"));
    const QString markerName = m_overlay ? m_overlay->selectedMarkerName().trimmed() : QString();
    QAction* applyMarkerAction = menu.addAction(
        markerName.isEmpty()
            ? tr("Apply Marker to Selected Frames")
            : tr("Apply Marker '%1' to Selected Frames").arg(markerName));
    applyMarkerAction->setEnabled(!markerName.isEmpty());
    QAction* selected = menu.exec(event->globalPos());
    if (selected == copyAction) {
        QApplication::clipboard()->setImage(QImage(m_sprites.first()->path));
    } else if (selected == applyPivotAction) {
        emit applyPivotToSelectedFramesRequested();
    } else if (selected == applyMarkerAction && !markerName.isEmpty()) {
        emit applyMarkerToSelectedFramesRequested(markerName);
    }
}

void PreviewCanvas::setSettings(const AppSettings& settings) {
    m_settings = settings;
    setBackgroundBrush(settings.canvasColor);
    QPen borderPen(settings.borderColor, 2, settings.borderStyle);
    borderPen.setCosmetic(true);
    for (auto* item : m_borderItems) {
        if (item->zValue() == -1) item->setBrush(settings.frameColor); 
        else item->setPen(borderPen); 
    }
}
