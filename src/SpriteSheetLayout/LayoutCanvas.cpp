#include "LayoutCanvas.h"
#include <QWheelEvent>
#include <QScrollBar>
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QGraphicsRectItem>
#include <QClipboard>
#include <QMenu>
#include <QAction>
#include <QUrl>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGraphicsPathItem>
#include <QPixmapCache>
#include <QSet>

namespace {
    const QColor kSelectionColor(10, 125, 255);
}

/**
 * @brief Constructs the LayoutCanvas.
 * 
 * Initializes the scene and sets up rendering hints.
 */
LayoutCanvas::LayoutCanvas(QWidget* parent) : QGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setAcceptDrops(true);
    setRenderHint(QPainter::Antialiasing, false);
    setRenderHint(QPainter::SmoothPixmapTransform, false);
    setDragMode(QGraphicsView::NoDrag);
    setBackgroundBrush(QColor(90, 90, 90));
}

void LayoutCanvas::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (urls.count() == 1 && urls.first().isLocalFile()) {
            const QString path = urls.first().toLocalFile();
            QFileInfo info(path);
            const QString ext = info.suffix().toLower();
            if (info.isDir() || ext == "zip" || ext == "json") {
                event->acceptProposedAction();
                return;
            }
        }
    }
    QGraphicsView::dragEnterEvent(event);
}

void LayoutCanvas::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (urls.count() == 1 && urls.first().isLocalFile()) {
            emit externalPathDropped(urls.first().toLocalFile());
            event->acceptProposedAction();
            return;
        }
    }
    QGraphicsView::dropEvent(event);
}

/**
 * @brief Sets the layout model to display.
 * 
 * Clears the current scene and populates it with sprites from the model.
 */
void LayoutCanvas::setModel(const LayoutModel& model) {
    clearCanvas();
    m_model = model;
    m_items.clear();
    m_lastSelectedIndex = -1;
    m_pendingDeselect = false;
    m_atlasBgItem = nullptr;
    m_borderItems.clear();
    m_scene->setSceneRect(0, 0, model.atlasWidth, model.atlasHeight);

    // Keep only pixmaps that are still relevant to the incoming model.
    QSet<QString> activePaths;
    activePaths.reserve(model.sprites.size());
    for (const auto& sprite : model.sprites) {
        activePaths.insert(sprite->path);
    }
    for (auto it = m_sourcePixmaps.begin(); it != m_sourcePixmaps.end();) {
        if (!activePaths.contains(it.key())) {
            it = m_sourcePixmaps.erase(it);
            continue;
        }
        ++it;
    }
    
    // Background for the atlas area
    m_atlasBgItem = m_scene->addRect(0, 0, model.atlasWidth, model.atlasHeight, Qt::NoPen, QBrush(m_settings.frameColor));
    m_atlasBgItem->setZValue(-100);

    QPen borderPen(m_settings.borderColor, 2, m_settings.borderStyle);
    borderPen.setCosmetic(true);

    // Load sprites into scene
    for (const auto& sprite : model.sprites) {
        QPixmap sourcePixmap = m_sourcePixmaps.value(sprite->path);
        if (sourcePixmap.isNull()) {
            if (!QPixmapCache::find(sprite->path, &sourcePixmap)) {
                sourcePixmap.load(sprite->path);
                if (!sourcePixmap.isNull()) {
                    QPixmapCache::insert(sprite->path, sourcePixmap);
                }
            }
            if (!sourcePixmap.isNull()) {
                m_sourcePixmaps.insert(sprite->path, sourcePixmap);
            }
        }
        if (sourcePixmap.isNull()) {
            continue;
        }
        QPixmap pixmap = sourcePixmap;

        // Handle trim logic if necessary (cropping pixmap)
        if (sprite->trimmed) {
             int l = sprite->trimRect.x();
             int t = sprite->trimRect.y();
             int r = sprite->trimRect.width();
             int b = sprite->trimRect.height();
             if (pixmap.width() > l + r && pixmap.height() > t + b) {
                 pixmap = pixmap.copy(l, t, pixmap.width() - l - r, pixmap.height() - t - b);
             }
        }

        auto* item = new SpriteItem(sprite);
        item->setPixmap(pixmap);
        item->setPos(sprite->rect.x(), sprite->rect.y());
        m_scene->addItem(item);
        m_items.append(item);

        QPainterPath path;
        QRectF r = sprite->rect;
        // Draw 4 separate lines to ensure consistent direction (Left->Right, Top->Bottom)
        // This prevents overlapping dashed lines from cancelling each other out
        path.moveTo(r.topLeft()); path.lineTo(r.topRight());       // Top
        path.moveTo(r.bottomLeft()); path.lineTo(r.bottomRight()); // Bottom
        path.moveTo(r.topLeft()); path.lineTo(r.bottomLeft());     // Left
        path.moveTo(r.topRight()); path.lineTo(r.bottomRight());   // Right

        auto* border = m_scene->addPath(path, borderPen);
        border->setZValue(item->zValue() + 0.1);
        m_borderItems.append(border);
    }
}

/**
 * @brief Clears the canvas scene.
 */
void LayoutCanvas::clearCanvas() {
    m_scene->clear();
}

/**
 * @brief Handles mouse wheel events for zooming (Ctrl + Wheel).
 */
void LayoutCanvas::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        const double scaleFactor = 1.15;
        if (event->angleDelta().y() > 0) {
            m_zoomLevel *= scaleFactor;
        } else {
            m_zoomLevel /= scaleFactor;
        }
        if (m_zoomLevel < 0.1) {
            m_zoomLevel = 0.1;
        }
        if (m_zoomLevel > 8.0) {
            m_zoomLevel = 8.0;
        }
        setZoom(m_zoomLevel);
        emit zoomChanged(m_zoomLevel);
        event->accept();
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

/**
 * @brief Sets the zoom level of the view.
 */
void LayoutCanvas::setZoom(double zoom) {
    m_zoomLevel = zoom;
    resetTransform();
    scale(m_zoomLevel, m_zoomLevel);
}

/**
 * @brief Selects a sprite by its file path.
 */
void LayoutCanvas::selectSpriteByPath(const QString& path) {
    for (auto* item : m_items) {
        if (item->getData()->path == path) {
            for (auto* si : m_items) {
                si->setSelectedState(false);
            }
            item->setSelectedState(true);
            m_lastSelectedIndex = m_items.indexOf(item);
            emitSelectionChanged();
            return;
        }
    }
}

/**
 * @brief Selects multiple sprites by their file paths.
 */
void LayoutCanvas::selectSpritesByPaths(const QStringList& paths, const QString& primaryPath) {
    int primaryIndex = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        auto* item = m_items[i];
        bool select = paths.contains(item->getData()->path);
        item->setSelectedState(select);
        if (select && !primaryPath.isEmpty() && item->getData()->path == primaryPath) {
            primaryIndex = i;
        }
    }
    if (primaryIndex != -1) {
        m_lastSelectedIndex = primaryIndex;
    }
    emitSelectionChanged();
}

/**
 * @brief Updates the visual settings (colors, borders) of the canvas.
 */
void LayoutCanvas::setSettings(const AppSettings& settings) {
    m_settings = settings;
    setBackgroundBrush(settings.canvasColor);
    if (m_atlasBgItem) {
        m_atlasBgItem->setBrush(settings.frameColor);
    }
    
    QPen borderPen(settings.borderColor, 2, settings.borderStyle);
    borderPen.setCosmetic(true);
    
    QPen selectedPen(kSelectionColor, 2, Qt::SolidLine);
    selectedPen.setCosmetic(true);

    for (int i = 0; i < m_items.size(); ++i) {
        if (i < m_borderItems.size()) {
            if (m_items[i]->isSelectedState()) {
                m_borderItems[i]->setPen(selectedPen);
                m_borderItems[i]->setZValue(m_items[i]->zValue() + 0.2);
            } else {
                m_borderItems[i]->setPen(borderPen);
                m_borderItems[i]->setZValue(m_items[i]->zValue() + 0.1);
            }
        }
    }
    update();
}

/**
 * @brief Handles mouse press events for selection and panning.
 */
void LayoutCanvas::mousePressEvent(QMouseEvent* event) {
    if (!m_searchQuery.isEmpty() && m_searchCloseRect.contains(event->pos())) {
        m_searchQuery.clear();
        updateSearch();
        event->accept();
        return;
    }
    m_lastMousePos = event->pos();
    m_pendingDeselect = false;
    // Pan logic
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spacePressed)) {
        m_isPanning = true;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
    
    // Selection logic
    SpriteItem* spriteItem = nullptr;
    QList<QGraphicsItem*> itemsAtPos = items(event->pos());
    for (auto* it : itemsAtPos) {
        if (auto* si = dynamic_cast<SpriteItem*>(it)) {
            spriteItem = si;
            break;
        }
    }

    if (spriteItem) {
        int clickedIndex = m_items.indexOf(spriteItem);

        // Shift+Click: Range Selection
        if ((event->modifiers() & Qt::ShiftModifier) && m_lastSelectedIndex != -1) {
            if (!(event->modifiers() & Qt::ControlModifier)) {
                for (auto* si : m_items) {
                    si->setSelectedState(false);
                }
            }
            int start = qMin(m_lastSelectedIndex, clickedIndex);
            int end = qMax(m_lastSelectedIndex, clickedIndex);
            for (int i = start; i <= end; ++i) {
                m_items[i]->setSelectedState(true);
            }
            emitSelectionChanged();
            return;
        }

        // Ctrl+Click: Toggle Selection
        if (event->modifiers() & Qt::ControlModifier) {
            bool newState = !spriteItem->isSelectedState();
            spriteItem->setSelectedState(newState);
            m_lastSelectedIndex = clickedIndex;
            emitSelectionChanged();
            return;
        }

        if (spriteItem->isSelectedState()) {
            m_pendingDeselect = true;
            return;
        }

        for (auto* si : m_items) {
            si->setSelectedState(false);
        }
        spriteItem->setSelectedState(true);
        m_lastSelectedIndex = clickedIndex;
        emitSelectionChanged();
    } else if (!(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
        for (auto* si : m_items) {
            si->setSelectedState(false);
        }
        m_lastSelectedIndex = -1;
        emitSelectionChanged();
    }
}

/**
 * @brief Handles mouse move events for panning and drag-and-drop initiation.
 */
void LayoutCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (m_isPanning) {
        m_pendingDeselect = false;
        QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }
    
    if (event->buttons() & Qt::LeftButton) {
        if ((event->pos() - m_lastMousePos).manhattanLength() > QApplication::startDragDistance()) {
            m_pendingDeselect = false;
            QStringList paths;
            QPixmap dragPixmap;

            QList<SpriteItem*> selectedItems;
            for (auto* item : m_items) {
                if (item->isSelectedState()) {
                    selectedItems.append(item);
                }
            }

            if (!selectedItems.isEmpty()) {
                for (auto* item : selectedItems) {
                    paths << item->getData()->path;
                }
            } else if (!m_searchQuery.isEmpty()) {
                // Drag all search matches
                for (auto* item : m_scene->items()) {
                    if (auto* si = dynamic_cast<SpriteItem*>(item)) {
                        if (si->getData()->name.contains(m_searchQuery, Qt::CaseInsensitive)) {
                            paths << si->getData()->path;
                        }
                    }
                }
            } else {
                // Drag single item under cursor
                QList<QGraphicsItem*> itemsAtPos = items(m_lastMousePos);
                for (auto* it : itemsAtPos) {
                    if (auto* si = dynamic_cast<SpriteItem*>(it)) {
                        paths << si->getData()->path;
                        break;
                    }
                }
            }

            if (paths.size() > 1) {
                dragPixmap = QPixmap(64, 64);
                dragPixmap.fill(Qt::transparent);
                QPainter p(&dragPixmap);
                p.setBrush(QColor(255, 255, 255, 200));
                p.setPen(Qt::black);
                p.drawRect(0, 0, 63, 63);
                p.drawText(dragPixmap.rect(), Qt::AlignCenter, QString::number(paths.size()));
            } else if (paths.size() == 1) {
                QString path = paths.first();
                for (auto* item : m_items) {
                    if (item->getData()->path == path) {
                        dragPixmap = item->pixmap().scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        break;
                    }
                }
            }

            if (!paths.isEmpty()) {
                QDrag* drag = new QDrag(this);
                QMimeData* mimeData = new QMimeData;
                mimeData->setData("application/x-sprat-sprite", paths.join('\n').toUtf8());
                
                QList<QUrl> urls;
                for (const QString& path : paths) {
                    urls.append(QUrl::fromLocalFile(path));
                }
                mimeData->setUrls(urls);
                if (paths.size() == 1) {
                    mimeData->setImageData(QImage(paths.first()));
                }

                drag->setMimeData(mimeData);
                drag->setPixmap(dragPixmap);
                drag->setHotSpot(QPoint(dragPixmap.width() / 2, dragPixmap.height() / 2));
                drag->exec(Qt::CopyAction);
                return;
            }
        }
    }
    QGraphicsView::mouseMoveEvent(event);
}

/**
 * @brief Handles mouse release events to finalize selection logic.
 */
void LayoutCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (m_pendingDeselect) {
        SpriteItem* spriteItem = nullptr;
        QList<QGraphicsItem*> itemsAtPos = items(event->pos());
        for (auto* it : itemsAtPos) {
            if (auto* si = dynamic_cast<SpriteItem*>(it)) {
                spriteItem = si;
                break;
            }
        }
        if (spriteItem) {
            for (auto* si : m_items) {
                si->setSelectedState(false);
            }
            spriteItem->setSelectedState(true);
            m_lastSelectedIndex = m_items.indexOf(spriteItem);
            emitSelectionChanged();
        }
        m_pendingDeselect = false;
    }
    if (m_isPanning) {
        m_isPanning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

/**
 * @brief Handles key press events for search typing and panning (Space).
 */
void LayoutCanvas::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Space:
            if (!event->isAutoRepeat()) {
                m_spacePressed = true;
            }
            break;
        case Qt::Key_Escape:
            m_searchQuery.clear();
            updateSearch();
            event->accept();
            return;
        case Qt::Key_Backspace:
            if (!m_searchQuery.isEmpty()) {
                m_searchQuery.chop(1);
                updateSearch();
            }
            event->accept();
            return;
    }
    if (!event->text().isEmpty() && event->text()[0].isPrint()) {
        m_searchQuery += event->text();
        updateSearch();
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

/**
 * @brief Handles key release events.
 */
void LayoutCanvas::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = false;
    }
    QGraphicsView::keyReleaseEvent(event);
}

/**
 * @brief Updates the visibility and selection state of sprites based on the search query.
 */
void LayoutCanvas::updateSearch() {
    m_lastSelectedIndex = -1;
    bool foundFirst = false;
    for (auto* si : m_items) {
        if (m_searchQuery.isEmpty()) {
            si->setSearchMatch(false);
            si->setOpacity(1.0);
            si->setSelectedState(false);
        } else {
            bool match = si->getData()->name.contains(m_searchQuery, Qt::CaseInsensitive);
            si->setSearchMatch(match);
            si->setSelectedState(match);
            si->setOpacity(match ? 1.0 : 0.3);
            if (match && !foundFirst) {
                m_lastSelectedIndex = m_items.indexOf(si);
                foundFirst = true;
            }
        }
    }

    setViewportUpdateMode(m_searchQuery.isEmpty() ? QGraphicsView::MinimalViewportUpdate : QGraphicsView::FullViewportUpdate);

    m_scene->update();
    viewport()->update();

    emitSelectionChanged();
}

/**
 * @brief Draws the search bar overlay in the foreground.
 */
void LayoutCanvas::drawForeground(QPainter* painter, const QRectF& rect) {
    if (m_searchQuery.isEmpty()) {
        m_searchCloseRect = QRect();
        return;
    }
    
    painter->save();
    painter->resetTransform(); // Draw in window coordinates
    
    QString text = "Search: " + m_searchQuery;
    QFont font = painter->font();
    font.setPixelSize(14);
    font.setBold(true);
    QFontMetrics fm(font);
    
    int textW = fm.horizontalAdvance(text);
    int closeBtnW = 24;
    int padding = 8;
    int h = fm.height() + 12;
    int w = textW + closeBtnW + (padding * 3);
    
    QRect box(10, 10, w, h);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 200));
    painter->drawRoundedRect(box, 6, 6);
    
    painter->setPen(Qt::white);
    painter->setFont(font);
    QRect textRect = box.adjusted(padding, 0, -closeBtnW - padding, 0);
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text);
    
    // Close Button Area
    QRect closeRect(box.right() - closeBtnW - padding, box.top(), closeBtnW + padding, box.height());
    m_searchCloseRect = closeRect;
    
    // Draw X
    QPen pen(Qt::white, 2);
    painter->setPen(pen);
    int cx = closeRect.center().x() - (padding / 2);
    int cy = closeRect.center().y();
    int r = 4;
    painter->drawLine(cx - r, cy - r, cx + r, cy + r);
    painter->drawLine(cx + r, cy - r, cx - r, cy + r);
    
    painter->restore();
}

/**
 * @brief Emits signals when the selection changes and updates visual highlights.
 */
void LayoutCanvas::emitSelectionChanged() {
    QList<SpritePtr> selection;
    
    QPen borderPen(m_settings.borderColor, 2, m_settings.borderStyle);
    borderPen.setCosmetic(true);

    QPen selectedPen(kSelectionColor, 2, Qt::SolidLine);
    selectedPen.setCosmetic(true);

    for (int i = 0; i < m_items.size(); ++i) {
        auto* item = m_items[i];
        bool selected = item->isSelectedState();
        if (selected) {
            selection.append(item->getData());
        }
        
        if (i < m_borderItems.size()) {
            if (selected) {
                m_borderItems[i]->setPen(selectedPen);
                m_borderItems[i]->setZValue(item->zValue() + 0.2);
            } else {
                m_borderItems[i]->setPen(borderPen);
                m_borderItems[i]->setZValue(item->zValue() + 0.1);
            }
        }
    }
    emit selectionChanged(selection);

    SpritePtr primary = nullptr;
    if (!selection.isEmpty()) {
        primary = (m_lastSelectedIndex >= 0 && m_lastSelectedIndex < m_items.size() && m_items[m_lastSelectedIndex]->isSelectedState())
            ? m_items[m_lastSelectedIndex]->getData()
            : selection.last();
    }
    emit spriteSelected(primary);
}

/**
 * @brief Shows a context menu (e.g., to copy the spritesheet image).
 */
void LayoutCanvas::contextMenuEvent(QContextMenuEvent* event) {
    QStringList selectedPaths;
    for (auto* item : m_items) {
        if (item->isSelectedState()) {
            selectedPaths.append(item->getData()->path);
        }
    }
    SpriteItem* spriteUnderCursor = nullptr;
    for (auto* it : items(event->pos())) {
        if (auto* si = dynamic_cast<SpriteItem*>(it)) {
            spriteUnderCursor = si;
            break;
        }
    }
    if (selectedPaths.isEmpty() && spriteUnderCursor) {
        selectedPaths.append(spriteUnderCursor->getData()->path);
    }

    QMenu menu(this);
    QAction* addFramesAction = menu.addAction("Add Frames...");
    QAction* removeFramesAction = nullptr;
    if (!selectedPaths.isEmpty()) {
        QString label = selectedPaths.size() > 1 ? "Remove Frames" : "Remove Frame";
        removeFramesAction = menu.addAction(label);
    }
    menu.addSeparator();
    QAction* autoTimelineAction = menu.addAction("Auto-create Timelines");
    QAction* copyLayoutAction = menu.addAction("Copy Spritesheet");

    QAction* selectedAction = menu.exec(event->globalPos());
    if (selectedAction == addFramesAction) {
        emit addFramesRequested();
        return;
    }
    if (selectedAction == removeFramesAction) {
        emit removeFramesRequested(selectedPaths);
        return;
    }
    if (selectedAction == autoTimelineAction) {
        emit requestTimelineGeneration();
        return;
    }

    if (selectedAction == copyLayoutAction) {
        if (m_model.atlasWidth <= 0 || m_model.atlasHeight <= 0) {
            return;
        }
        
        QImage image(m_model.atlasWidth, m_model.atlasHeight, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        
        for (auto* item : m_items) {
            // Use the already loaded and trimmed pixmaps from the scene items
            painter.drawPixmap(item->pos(), item->pixmap());
        }
        painter.end();
        QApplication::clipboard()->setImage(image);
    }
}
