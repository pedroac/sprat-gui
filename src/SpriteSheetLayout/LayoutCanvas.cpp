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
#include <QKeySequence>
#include <limits>
#include "ViewUtils.h"

namespace {
    const QColor kSelectionColor(10, 125, 255);
    const QColor kContextTargetColor(255, 215, 0);

enum class NavigationDirection {
    Left,
    Right,
    Up,
    Down
};

int directionStep(NavigationDirection direction) {
    switch (direction) {
        case NavigationDirection::Left:
        case NavigationDirection::Up:
            return -1;
        case NavigationDirection::Right:
        case NavigationDirection::Down:
            return 1;
    }
    return 1;
}

bool isHorizontalDirection(NavigationDirection direction) {
    return direction == NavigationDirection::Left || direction == NavigationDirection::Right;
}

int directionalGuessIndex(int currentIndex, NavigationDirection direction, int size) {
    if (size <= 0) {
        return -1;
    }
    const int guess = currentIndex + directionStep(direction);
    return qBound(0, guess, size - 1);
}

int findDirectionalNeighborIndex(const QVector<SpriteItem*>& items, int currentIndex, NavigationDirection direction) {
    if (currentIndex < 0 || currentIndex >= items.size()) {
        return -1;
    }
    if (items.size() <= 1) {
        return -1;
    }

    const QRectF currentRect = items[currentIndex]->sceneBoundingRect();
    const QPointF currentCenter = currentRect.center();
    const bool horizontalMove = isHorizontalDirection(direction);
    const int directionSign = directionStep(direction);
    const double probeCoord = horizontalMove ? currentCenter.y() : currentCenter.x();
    const int guessIndex = directionalGuessIndex(currentIndex, direction, items.size());

    int bestCollisionIndex = -1;
    double bestCollisionDistance = std::numeric_limits<double>::infinity();
    for (int radius = 0; radius < items.size(); ++radius) {
        const int leftCandidate = guessIndex - radius;
        const int rightCandidate = guessIndex + radius;
        int candidateIndices[2] = { leftCandidate, rightCandidate };
        const int candidateCount = (leftCandidate == rightCandidate) ? 1 : 2;

        for (int c = 0; c < candidateCount; ++c) {
            const int i = candidateIndices[c];
            if (i < 0 || i >= items.size()) {
                continue;
            }
            if (i == currentIndex) {
                continue;
            }

            const QRectF candidateRect = items[i]->sceneBoundingRect();
            const QPointF candidateCenter = candidateRect.center();

            const double signedPrimaryDistance = horizontalMove
                ? (candidateCenter.x() - currentCenter.x())
                : (candidateCenter.y() - currentCenter.y());
            if ((directionSign < 0 && signedPrimaryDistance >= 0.0) || (directionSign > 0 && signedPrimaryDistance <= 0.0)) {
                continue;
            }

            bool collidesProbeLine = false;
            if (horizontalMove) {
                collidesProbeLine = (probeCoord >= candidateRect.top() && probeCoord <= candidateRect.bottom());
            } else {
                collidesProbeLine = (probeCoord >= candidateRect.left() && probeCoord <= candidateRect.right());
            }

            if (collidesProbeLine) {
                const double collisionDistance = horizontalMove
                    ? ((directionSign > 0) ? qAbs(candidateRect.left() - currentCenter.x()) : qAbs(currentCenter.x() - candidateRect.right()))
                    : ((directionSign > 0) ? qAbs(candidateRect.top() - currentCenter.y()) : qAbs(currentCenter.y() - candidateRect.bottom()));
                if (collisionDistance < bestCollisionDistance) {
                    bestCollisionDistance = collisionDistance;
                    bestCollisionIndex = i;
                }
                continue;
            }

        }
    }

    if (bestCollisionIndex != -1) {
        return bestCollisionIndex;
    }

    return -1;
}

int findRowEdgeIndex(const QVector<SpriteItem*>& items, int currentIndex, bool endOfRow) {
    if (currentIndex < 0 || currentIndex >= items.size()) {
        return -1;
    }
    if (items.isEmpty()) {
        return -1;
    }

    const QRectF currentRect = items[currentIndex]->sceneBoundingRect();
    const QPointF currentCenter = currentRect.center();
    int bestIndex = currentIndex;
    double bestX = currentCenter.x();
    double bestY = currentCenter.y();

    for (int i = 0; i < items.size(); ++i) {
        const QRectF rect = items[i]->sceneBoundingRect();
        const QPointF center = rect.center();
        const bool sameRow = currentCenter.y() >= rect.top() && currentCenter.y() <= rect.bottom();
        if (!sameRow) {
            continue;
        }

        const bool isBetter = endOfRow
            ? (center.x() > bestX || (qFuzzyCompare(center.x(), bestX) && center.y() < bestY))
            : (center.x() < bestX || (qFuzzyCompare(center.x(), bestX) && center.y() < bestY));
        if (isBetter) {
            bestIndex = i;
            bestX = center.x();
            bestY = center.y();
        }
    }
    return bestIndex;
}
}

/**
 * @brief Constructs the LayoutCanvas.
 */
LayoutCanvas::LayoutCanvas(QWidget* parent) : ZoomableGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setAcceptDrops(true);
    setZoomRange(0.1, 8.0);
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
    ZoomableGraphicsView::dragEnterEvent(event);
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
    ZoomableGraphicsView::dropEvent(event);
}

void LayoutCanvas::setModels(const QVector<LayoutModel>& models) {
    clearCanvas();
    m_models = models;
    m_items.clear();
    m_baseSelectionPaths.clear();
    m_lastSelectedIndex = -1;
    m_pendingDeselect = false;
    m_borderItems.clear();
    m_modelOffsets.clear();

    if (models.isEmpty()) {
        m_scene->setSceneRect(0, 0, 0, 0);
        return;
    }

    int currentY = 0;
    const int margin = 100;
    int maxW = 0;

    QSet<QString> activePaths;
    for (const auto& model : models) {
        maxW = qMax(maxW, model.atlasWidth);
        for (const auto& sprite : model.sprites) {
            activePaths.insert(sprite->path);
        }
    }

    for (auto it = m_sourcePixmaps.begin(); it != m_sourcePixmaps.end();) {
        if (!activePaths.contains(it.key())) {
            it = m_sourcePixmaps.erase(it);
            continue;
        }
        ++it;
    }

    QPen borderPen(m_settings.borderColor, 2, m_settings.borderStyle);
    borderPen.setCosmetic(true);

    for (int i = 0; i < models.size(); ++i) {
        const auto& model = models[i];
        m_modelOffsets.append(QPoint(0, currentY));

        QGraphicsRectItem* bg;
        if (m_settings.showCheckerboard) {
            bg = m_scene->addRect(0, currentY, model.atlasWidth, model.atlasHeight, Qt::NoPen, QBrush(createCheckerboardPixmap(m_settings.spriteFrameColor)));
        } else {
            bg = m_scene->addRect(0, currentY, model.atlasWidth, model.atlasHeight, Qt::NoPen, QBrush(m_settings.spriteFrameColor));
        }
        bg->setZValue(-100);

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

            if (sprite->trimmed) {
                 int l = sprite->trimRect.x();
                 int t = sprite->trimRect.y();
                 int r = sprite->trimRect.width();
                 int b = sprite->trimRect.height();
                 if (pixmap.width() > l + r && pixmap.height() > t + b) {
                     pixmap = pixmap.copy(l, t, pixmap.width() - l - r, pixmap.height() - t - b);
                 }
            }

            if (sprite->rotated) {
                QTransform trans;
                trans.rotate(90);
                pixmap = pixmap.transformed(trans, Qt::SmoothTransformation);
            }

            const QSize targetSize = sprite->rect.size();
            if (targetSize.width() > 0 &&
                targetSize.height() > 0 &&
                (pixmap.size() != targetSize)) {
                pixmap = pixmap.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
            }

            auto* item = new SpriteItem(sprite);
            item->setPixmap(pixmap);
            item->setPos(sprite->rect.x(), currentY + sprite->rect.y());
            m_scene->addItem(item);
            m_items.append(item);

            QPainterPath path;
            QRectF r = sprite->rect;
            r.translate(0, currentY);
            path.moveTo(r.topLeft()); path.lineTo(r.topRight());
            path.moveTo(r.bottomLeft()); path.lineTo(r.bottomRight());
            path.moveTo(r.topLeft()); path.lineTo(r.bottomLeft());
            path.moveTo(r.topRight()); path.lineTo(r.bottomRight());

            auto* border = m_scene->addPath(path, borderPen);
            border->setZValue(item->zValue() + 0.1);
            m_borderItems.append(border);
        }

        currentY += model.atlasHeight + margin;
    }
    m_scene->setSceneRect(0, 0, maxW, currentY - margin);
}

void LayoutCanvas::clearCanvas() {
    m_scene->clear();
    m_items.clear();
}

void LayoutCanvas::selectSpriteByPath(const QString& path) {
    for (auto* item : m_items) {
        if (item->getData()->path == path) {
            for (auto* si : m_items) {
                if (!si->isSearchMatch()) {
                    si->setSelectedState(false);
                }
            }
            item->setSelectedState(true);
            m_lastSelectedIndex = m_items.indexOf(item);
            finalizeSearchSelection();
            return;
        }
    }
}

void LayoutCanvas::selectSpritesByPaths(const QStringList& paths, const QString& primaryPath) {
    const QSet<QString> pathSet(paths.begin(), paths.end());
    int primaryIndex = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        auto* item = m_items[i];
        bool select = pathSet.contains(item->getData()->path);
        // If search is active, we should probably merge? 
        // But usually these calls come from clicking a timeline or loading a project.
        // Let's keep matches if search is active.
        if (item->isSearchMatch()) select = true;

        item->setSelectedState(select);
        if (select && !primaryPath.isEmpty() && item->getData()->path == primaryPath) {
            primaryIndex = i;
        }
    }
    if (primaryIndex != -1) {
        m_lastSelectedIndex = primaryIndex;
    }
    finalizeSearchSelection();
}

void LayoutCanvas::setSettings(const AppSettings& settings) {
    m_settings = settings;
    setBackgroundBrush(settings.workspaceColor);
    
    // Refresh the models to redraw everything with new settings (backgrounds, borders)
    if (!m_models.isEmpty()) {
        setModels(m_models);
    }
}

void LayoutCanvas::mousePressEvent(QMouseEvent* event) {
    if (!m_searchQuery.isEmpty() && m_searchCloseRect.contains(event->pos())) {
        m_searchQuery.clear();
        updateSearch();
        event->accept();
        return;
    }

    ZoomableGraphicsView::mousePressEvent(event);
    if (m_isPanning) {
        m_pendingDeselect = false;
        return;
    }

    if (event->button() != Qt::LeftButton) {
        return;
    }
    
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

        if ((event->modifiers() & Qt::ShiftModifier) && m_lastSelectedIndex != -1) {
            if (!(event->modifiers() & Qt::ControlModifier)) {
                for (auto* si : m_items) {
                    if (!si->isSearchMatch()) {
                        si->setSelectedState(false);
                    }
                }
            }
            int start = qMin(m_lastSelectedIndex, clickedIndex);
            int end = qMax(m_lastSelectedIndex, clickedIndex);
            for (int i = start; i <= end; ++i) {
                m_items[i]->setSelectedState(true);
            }

            finalizeSearchSelection();
            return;
        }

        if (event->modifiers() & Qt::ControlModifier) {
            bool newState = !spriteItem->isSelectedState();
            spriteItem->setSelectedState(newState);
            m_lastSelectedIndex = clickedIndex;

            finalizeSearchSelection();
            return;
        }

        if (spriteItem->isSelectedState()) {
            m_pendingDeselect = true;
            return;
        }

        for (auto* si : m_items) {
            if (!si->isSearchMatch()) {
                si->setSelectedState(false);
            }
        }
        spriteItem->setSelectedState(true);
        m_lastSelectedIndex = clickedIndex;

        finalizeSearchSelection();
    } else if (!(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
        for (auto* si : m_items) {
            si->setSelectedState(false);
        }
        m_lastSelectedIndex = -1;

        finalizeSearchSelection();
    }
}

void LayoutCanvas::mouseMoveEvent(QMouseEvent* event) {
    ZoomableGraphicsView::mouseMoveEvent(event);
    if (m_isPanning) {
        m_pendingDeselect = false;
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
                for (auto* item : m_scene->items()) {
                    if (auto* si = dynamic_cast<SpriteItem*>(item)) {
                        if (si->getData()->name.contains(m_searchQuery, Qt::CaseInsensitive)) {
                            paths << si->getData()->path;
                        }
                    }
                }
            } else {
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
}

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
                if (!si->isSearchMatch()) {
                    si->setSelectedState(false);
                }
            }
            spriteItem->setSelectedState(true);
            m_lastSelectedIndex = m_items.indexOf(spriteItem);
            finalizeSearchSelection();
        }
        m_pendingDeselect = false;
    }
    ZoomableGraphicsView::mouseReleaseEvent(event);
}

void LayoutCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->matches(QKeySequence::SelectAll)) {
        if (!m_items.isEmpty()) {
            for (auto* item : m_items) {
                item->setSelectedState(true);
            }
            if (m_lastSelectedIndex < 0 || m_lastSelectedIndex >= m_items.size()) {
                m_lastSelectedIndex = 0;
            }

            finalizeSearchSelection();
        }
        event->accept();
        return;
    }

    int selectionTarget = -1;
    if (event->key() == Qt::Key_Left) {
        if (m_lastSelectedIndex < 0 || m_lastSelectedIndex >= m_items.size()) {
            selectionTarget = m_items.isEmpty() ? -1 : 0;
        } else {
            selectionTarget = findDirectionalNeighborIndex(m_items, m_lastSelectedIndex, NavigationDirection::Left);
        }
    } else if (event->key() == Qt::Key_Right) {
        if (m_lastSelectedIndex < 0 || m_lastSelectedIndex >= m_items.size()) {
            selectionTarget = m_items.isEmpty() ? -1 : 0;
        } else {
            selectionTarget = findDirectionalNeighborIndex(m_items, m_lastSelectedIndex, NavigationDirection::Right);
        }
    } else if (event->key() == Qt::Key_Up) {
        if (m_lastSelectedIndex < 0 || m_lastSelectedIndex >= m_items.size()) {
            selectionTarget = m_items.isEmpty() ? -1 : (m_items.size() - 1);
        } else {
            selectionTarget = findDirectionalNeighborIndex(m_items, m_lastSelectedIndex, NavigationDirection::Up);
        }
    } else if (event->key() == Qt::Key_Down) {
        if (m_lastSelectedIndex < 0 || m_lastSelectedIndex >= m_items.size()) {
            selectionTarget = m_items.isEmpty() ? -1 : 0;
        } else {
            selectionTarget = findDirectionalNeighborIndex(m_items, m_lastSelectedIndex, NavigationDirection::Down);
        }
    } else if (event->key() == Qt::Key_Home) {
        if (m_lastSelectedIndex < 0 || m_lastSelectedIndex >= m_items.size()) {
            selectionTarget = m_items.isEmpty() ? -1 : 0;
        } else {
            selectionTarget = findRowEdgeIndex(m_items, m_lastSelectedIndex, false);
        }
    } else if (event->key() == Qt::Key_End) {
        if (m_lastSelectedIndex < 0 || m_lastSelectedIndex >= m_items.size()) {
            selectionTarget = m_items.isEmpty() ? -1 : (m_items.size() - 1);
        } else {
            selectionTarget = findRowEdgeIndex(m_items, m_lastSelectedIndex, true);
        }
    }

    if (selectionTarget != -1 && !m_items.isEmpty()) {
        int currentIndex = m_lastSelectedIndex;
        if (currentIndex < 0 || currentIndex >= m_items.size()) {
            currentIndex = 0;
        }
        const int nextIndex = selectionTarget;

        if (!(event->modifiers() & Qt::ShiftModifier)) {
            for (auto* item : m_items) {
                if (!item->isSearchMatch()) {
                    item->setSelectedState(false);
                }
            }
        }
        m_items[nextIndex]->setSelectedState(true);
        m_lastSelectedIndex = nextIndex;
        ensureVisible(m_items[nextIndex]);
        
        finalizeSearchSelection();
        event->accept();
        return;
    }

    switch (event->key()) {
        case Qt::Key_Space:
            if (!m_searchQuery.isEmpty()) {
                m_searchQuery += QLatin1Char(' ');
                updateSearch();
                event->accept();
                return;
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
                event->accept();
                return;
            }
            break;
    }

    if (!event->text().isEmpty() && event->text().at(0).isPrint()) {
        m_searchQuery += event->text();
        updateSearch();
        event->accept();
        return;
    }

    ZoomableGraphicsView::keyPressEvent(event);
}

void LayoutCanvas::keyReleaseEvent(QKeyEvent* event) {
    ZoomableGraphicsView::keyReleaseEvent(event);
}

void LayoutCanvas::resizeEvent(QResizeEvent* event) {
    ZoomableGraphicsView::resizeEvent(event);
}

void LayoutCanvas::drawForeground(QPainter* painter, const QRectF& rect) {
    if (m_searchQuery.isEmpty()) {
        return;
    }

    painter->save();
    painter->setWorldMatrixEnabled(false);

    const int padding = 10;
    const int margin = 20;
    const QString text = tr("Search: ") + m_searchQuery;
    const QFont font("Monospace", 12, QFont::Bold);
    painter->setFont(font);
    const QFontMetrics fm(font);
    const int textWidth = fm.horizontalAdvance(text);
    const int textHeight = fm.height();

    const int closeSize = 16;
    const int boxWidth = textWidth + (padding * 5) + closeSize + 20;
    const int boxHeight = textHeight + padding * 2;
    const QRect boxRect(viewport()->width() - boxWidth - margin, margin, boxWidth, boxHeight);

    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 180));
    painter->drawRoundedRect(boxRect, 5, 5);

    painter->setPen(Qt::white);
    QRect textRect = boxRect.adjusted(padding * 2, 0, -(padding * 2 + closeSize), 0);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);

    m_searchCloseRect = QRect(boxRect.right() - padding - closeSize, boxRect.center().y() - closeSize / 2, closeSize, closeSize);
    painter->setPen(QPen(Qt::white, 2));
    painter->drawLine(m_searchCloseRect.topLeft(), m_searchCloseRect.bottomRight());
    painter->drawLine(m_searchCloseRect.topRight(), m_searchCloseRect.bottomLeft());

    painter->restore();
    
    // Ensure viewport-level update for immediate visual feedback of the search text
    viewport()->update();
}

void LayoutCanvas::contextMenuEvent(QContextMenuEvent* event) {
    SpriteItem* target = nullptr;
    QList<QGraphicsItem*> itemsAtPos = items(event->pos());
    for (auto* it : itemsAtPos) {
        if (auto* si = dynamic_cast<SpriteItem*>(it)) {
            target = si;
            break;
        }
    }

    QMenu menu(this);
    QAction* addFramesAction = menu.addAction(tr("Add Frames..."));
    connect(addFramesAction, &QAction::triggered, this, &LayoutCanvas::addFramesRequested);

    QAction* genTimelineAction = menu.addAction(tr("Auto-create Timelines"));
    connect(genTimelineAction, &QAction::triggered, this, &LayoutCanvas::requestTimelineGeneration);

    menu.addSeparator();

    QList<SpriteItem*> selectedItems;
    for (auto* item : m_items) {
        if (item->isSelectedState()) {
            selectedItems.append(item);
        }
    }

    QAction* removeFramesAction = menu.addAction(tr("Remove Selected Frames"));
    removeFramesAction->setEnabled(!selectedItems.isEmpty());
    connect(removeFramesAction, &QAction::triggered, this, [this, selectedItems]() {
        QStringList paths;
        for (auto* it : selectedItems) paths << it->getData()->path;
        emit removeFramesRequested(paths);
    });

    if (target) {
        menu.addSeparator();
        m_contextMenuTargetPath = target->getData()->path;
        QAction* copyPathAction = menu.addAction(tr("Copy Path"));
        connect(copyPathAction, &QAction::triggered, this, [this]() {
            QApplication::clipboard()->setText(m_contextMenuTargetPath);
        });
        QAction* copyImageAction = menu.addAction(tr("Copy Image"));
        connect(copyImageAction, &QAction::triggered, this, [this]() {
            QApplication::clipboard()->setImage(QImage(m_contextMenuTargetPath));
        });
    }

    menu.exec(event->globalPos());
}

void LayoutCanvas::updateSearch() {
    const bool hasQuery = !m_searchQuery.isEmpty();
    bool selectionChangedOccurred = false;
    for (auto* item : m_items) {
        const bool match = hasQuery && item->getData()->name.contains(m_searchQuery, Qt::CaseInsensitive);
        item->setSearchMatch(match);
        
        const bool shouldBeSelected = match || m_baseSelectionPaths.contains(item->getData()->path);
        if (item->isSelectedState() != shouldBeSelected) {
            item->setSelectedState(shouldBeSelected);
            selectionChangedOccurred = true;
        }
    }
    if (selectionChangedOccurred) {
        emitSelectionChanged();
    }
    viewport()->update();
    update();
}

void LayoutCanvas::finalizeSearchSelection() {
    if (!m_searchQuery.isEmpty()) {
        for (auto* item : m_items) {
            if (item->isSearchMatch()) {
                m_baseSelectionPaths.insert(item->getData()->path);
            }
        }
        m_searchQuery.clear();
        updateSearch();
    } else {
        // When no search is active, ensure our manual selection is reflected in m_baseSelectionPaths
        m_baseSelectionPaths.clear();
        for (auto* item : m_items) {
            if (item->isSelectedState()) {
                m_baseSelectionPaths.insert(item->getData()->path);
            }
        }
        emitSelectionChanged();
    }
}

void LayoutCanvas::emitSelectionChanged() {
    QList<SpritePtr> selection;
    SpritePtr primary;
    for (int i = 0; i < m_items.size(); ++i) {
        bool isPrimary = (i == m_lastSelectedIndex && m_items[i]->isSelectedState());
        m_items[i]->setPrimaryState(isPrimary);

        if (m_items[i]->isSelectedState()) {
            selection.append(m_items[i]->getData());
            if (isPrimary) {
                primary = m_items[i]->getData();
            }
        }
    }

    emit selectionChanged(selection);
    if (primary) {
        emit spriteSelected(primary);
    } else if (!selection.isEmpty()) {
        emit spriteSelected(selection.first());
    } else {
        emit spriteSelected(SpritePtr());
    }
}

void LayoutCanvas::updateBorderHighlights() {
    QPen borderPen(m_settings.borderColor, 2, m_settings.borderStyle);
    borderPen.setCosmetic(true);
    for (auto* border : m_borderItems) {
        border->setPen(borderPen);
    }
}
