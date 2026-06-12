#include "LayoutCanvas.h"
#include <QWheelEvent>
#include <QScrollBar>
#include <QApplication>
#include <QCoreApplication>
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
#include <QtConcurrent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QThreadPool>
#include <QElapsedTimer>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <limits>
#include "ViewUtils.h"
#include "SplitModeUtils.h"

#ifdef Q_OS_WASM
#include <emscripten.h>
#endif

namespace {
    const QColor kSelectionColor(10, 125, 255);
    const QColor kContextTargetColor(255, 215, 0);

    bool loadPixmapFromFile(const QString& path, QPixmap& out) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        const QByteArray data = file.readAll();
        QImage img;
        if (!img.loadFromData(data)) {
            return false;
        }
        out = QPixmap::fromImage(img);
        return !out.isNull();
    }

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
        if (urls.count() == 1) {
#ifndef Q_OS_WASM
            if (!urls.first().isLocalFile()) {
                event->acceptProposedAction();
                return;
            }
#endif
            const QString path = urls.first().toLocalFile();
            QFileInfo info(path);
            const QString ext = info.suffix().toLower();
            bool supported = info.isDir() || ext == "zip" || ext == "json";
#ifdef Q_OS_WASM
            supported = true;
#endif
            if (supported) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction(); // Accept any URL for WASM
        return;
    }
    ZoomableGraphicsView::dragEnterEvent(event);
}

void LayoutCanvas::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (urls.count() == 1) {
#ifndef Q_OS_WASM
            if (!urls.first().isLocalFile()) {
                emit externalPathDropped(urls.first().toString());
                event->acceptProposedAction();
                return;
            }
#endif
            const QString localPath = urls.first().toLocalFile();
#ifdef Q_OS_WASM
            if (localPath.isEmpty() || !QFileInfo::exists(localPath)) {
                // If it's not a local file, treat it as a URL string
                emit externalPathDropped(urls.first().toString());
                event->acceptProposedAction();
                return;
            }
#endif
            emit externalPathDropped(localPath);
            event->acceptProposedAction();
            return;
        }
    }
    ZoomableGraphicsView::dropEvent(event);
}

void LayoutCanvas::setModels(const QVector<LayoutModel>& models, std::atomic<bool>* canceled) {
    clearCanvas();
    m_models = models;
    m_items.clear();
    m_baseSelectionPaths.clear();
    m_lastSelectedIndex = -1;
    m_pendingDeselect = false;
    m_borderItems.clear();
    m_modelOffsets.clear();
    m_pathToIndex.clear();

    if (models.isEmpty()) {
        m_scene->setSceneRect(0, 0, 0, 0);
        return;
    }

    int currentY = 0;
    const int margin = 100;
    int maxW = 0;

    for (const auto& model : models) {
        maxW = qMax(maxW, model.atlasWidth);
    }

    QPen borderPen(m_settings.borderColor, 2, m_settings.borderStyle);
    borderPen.setCosmetic(true);

    int totalSprites = 0;
    for (const auto& model : models) {
        totalSprites += model.sprites.size();
    }
    m_items.reserve(totalSprites);
    m_borderItems.reserve(totalSprites);
    m_modelOffsets.reserve(models.size());
    m_atlasBackgroundItems.reserve(models.size());
    m_pathToIndex.reserve(totalSprites);

    // Cache checkerboard pixmap once for all models
    QBrush bgBrush;
    if (m_settings.showCheckerboard) {
        if (m_cachedCheckerboardColor != m_settings.spriteFrameColor || m_cachedCheckerboard.isNull()) {
            m_cachedCheckerboard = createCheckerboardPixmap(m_settings.spriteFrameColor);
            m_cachedCheckerboardColor = m_settings.spriteFrameColor;
        }
        bgBrush = QBrush(m_cachedCheckerboard);
    } else {
        bgBrush = QBrush(m_settings.spriteFrameColor);
    }

    static const QTransform kRotation90 = []() {
        QTransform t;
        t.rotate(90);
        return t;
    }();

    for (int i = 0; i < models.size(); ++i) {
        if (canceled && *canceled) {
            break;
        }
        const auto& model = models[i];
        m_modelOffsets.append(QPoint(0, currentY));

        auto* bg = m_scene->addRect(0, currentY, model.atlasWidth, model.atlasHeight, Qt::NoPen, bgBrush);
        bg->setZValue(-100);
        m_atlasBackgroundItems.append(bg);

        for (const auto& sprite : model.sprites) {
            if (canceled && *canceled) {
                break;
            }

            // Build a cache key for the transformed pixmap
            const QSize targetSize = sprite->rect.size();
            const QString cacheKey = QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
                .arg(sprite->path)
                .arg(sprite->trimmed ? sprite->trimRect.x() : 0)
                .arg(sprite->trimmed ? sprite->trimRect.y() : 0)
                .arg(sprite->trimmed ? sprite->trimRect.width() : 0)
                .arg(sprite->trimmed ? sprite->trimRect.height() : 0)
                .arg(sprite->rotated ? 1 : 0)
                .arg(QStringLiteral("%1x%2").arg(targetSize.width()).arg(targetSize.height()));

            QPixmap pixmap = m_transformedPixmapCache.value(cacheKey);
            if (pixmap.isNull()) {
                pixmap = m_sourcePixmaps.value(sprite->path);
                if (pixmap.isNull()) {
                    if (loadPixmapFromFile(sprite->path, pixmap)) {
                        m_sourcePixmaps.insert(sprite->path, pixmap);
                    }
                }
                if (pixmap.isNull()) {
                    continue;
                }

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
                    pixmap = pixmap.transformed(kRotation90, Qt::SmoothTransformation);
                }

                if (targetSize.width() > 0 &&
                    targetSize.height() > 0 &&
                    (pixmap.size() != targetSize)) {
                    pixmap = pixmap.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
                }

                // Prevent unbounded cache growth: evict half the entries when at the limit
                constexpr int kMaxTransformedCache = 500;
                if (m_transformedPixmapCache.size() >= kMaxTransformedCache) {
                    auto it = m_transformedPixmapCache.begin();
                    int toRemove = kMaxTransformedCache / 2;
                    while (toRemove-- > 0 && it != m_transformedPixmapCache.end()) {
                        it = m_transformedPixmapCache.erase(it);
                    }
                }
                m_transformedPixmapCache.insert(cacheKey, pixmap);
            }

            auto* item = new SpriteItem(sprite);
            item->setPixmap(pixmap);
            item->setPos(sprite->rect.x(), currentY + sprite->rect.y());
            item->setIndex(m_items.size());
            item->setLabelMode(m_settings.layoutLabelMode);
            m_scene->addItem(item);
            m_pathToIndex.insert(sprite->path, m_items.size());
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

    if (m_displayOnly) {
        for (auto* border : m_borderItems) border->hide();
        for (auto* item : m_items) item->setLabelHidden(true);
    }

    // Re-apply dim filter if one was active before the rebuild.
    if (!m_dimFilter.isEmpty())
        setDimFilter(m_dimFilter);
}

void LayoutCanvas::setModelsAsync(const QVector<LayoutModel>& models, std::atomic<bool>* canceled, std::function<void()> onFinished) {
    QSet<QString> activePaths;
    for (const auto& model : models) {
        for (const auto& sprite : model.sprites) {
            activePaths.insert(sprite->path);
        }
    }

    auto task = [this, models, activePaths, canceled, onFinished]() {
        QElapsedTimer timer;
        timer.start();
        qInfo() << "[WASM] setModelsAsync task start"
                << "sprites=" << activePaths.size();

        // Simpler: Just ensure they are in QPixmapCache (it is thread-safe)
        for (const QString& path : activePaths) {
            if (canceled && *canceled) return;
            QPixmap pm;
            if (!QPixmapCache::find(path, &pm)) {
                if (loadPixmapFromFile(path, pm)) {
                    QPixmapCache::insert(path, pm);
                }
            }
        }
        qInfo() << "[WASM] setModelsAsync task pixmaps ready"
                << "ms=" << timer.elapsed();

        QMetaObject::invokeMethod(this, [this, models, activePaths, canceled, onFinished]() {
            if (canceled && *canceled) return;

            // Update m_sourcePixmaps cache from QPixmapCache
            for (const QString& path : activePaths) {
                QPixmap pm;
                if (QPixmapCache::find(path, &pm)) {
                    m_sourcePixmaps.insert(path, pm);
                }
            }

            // Cleanup old entries
            for (auto it = m_sourcePixmaps.begin(); it != m_sourcePixmaps.end();) {
                if (!activePaths.contains(it.key())) {
                    it = m_sourcePixmaps.erase(it);
                } else {
                    ++it;
                }
            }

            setModels(models, canceled);
            if (onFinished) onFinished();
            qInfo() << "[WASM] setModelsAsync UI apply done";
        }, Qt::AutoConnection);
        // Qt::AutoConnection: in WASM task() runs on the main thread (same as this),
        // so Qt resolves AutoConnection as DirectConnection — the callback runs
        // immediately here rather than being queued. This matters because a queued
        // QMetaCallEvent alone does not cause Qt's WASM backend to request a new
        // requestAnimationFrame, so the event loop would stall until the next user
        // input (e.g. mouse movement). Running the callback directly means hide()
        // is called inside task(), which posts a paint event that *does* trigger a
        // RAF request, keeping the loop alive. On Desktop the task runs in a thread
        // pool thread (different thread), so AutoConnection queues as before.
    };

#ifdef Q_OS_WASM
    task();
#else
    QThreadPool::globalInstance()->start(task);
#endif
}

void LayoutCanvas::ensureSplitLineItem() {
    if (!m_splitLineItem) {
        setMouseTracking(true);
        m_splitLineItem = new QGraphicsLineItem();
        QPen splitPen(Qt::red, 2, Qt::DashLine);
        splitPen.setCosmetic(true);
        m_splitLineItem->setPen(splitPen);
        m_splitLineItem->setZValue(100.0);
        m_splitLineItem->hide();
        m_scene->addItem(m_splitLineItem);
    }
}

void LayoutCanvas::setSplitMode(bool enabled) {
    ensureSplitLineItem();

    m_splitMode = enabled;
    emit splitModeChanged(m_splitMode);
    if (!m_splitMode) {
        if (m_splitLineItem) {
            m_splitLineItem->hide();
        }
        m_splitItemIndex = -1;
        if (viewport()) {
            viewport()->setCursor(Qt::ArrowCursor);
        }
    } else {
        if (viewport()) {
            viewport()->setCursor(Qt::CrossCursor);
        }
    }
}

void LayoutCanvas::clearCanvas() {
    // Remove split line from scene before clearing to avoid dangling pointer
    if (m_splitLineItem) {
        m_scene->removeItem(m_splitLineItem);
        delete m_splitLineItem;
        m_splitLineItem = nullptr;
    }

    m_borderItems.clear();
    m_atlasBackgroundItems.clear();
    m_scene->clear();
    m_items.clear();
    m_pathToIndex.clear();
    m_splitItemIndex = -1;
}

void LayoutCanvas::selectSpriteByPath(const QString& path) {
    auto it = m_pathToIndex.constFind(path);
    if (it == m_pathToIndex.constEnd()) {
        return;
    }
    const int targetIndex = it.value();
    for (auto* si : m_items) {
        if (!si->isSearchMatch()) {
            si->setSelectedState(false);
        }
    }
    m_items[targetIndex]->setSelectedState(true);
    m_lastSelectedIndex = targetIndex;
    finalizeSearchSelection();
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
    const AppSettings oldSettings = m_settings;
    m_settings = settings;
    setBackgroundBrush(settings.workspaceColor);

    if (m_models.isEmpty()) {
        return;
    }

    const bool bgChanged = oldSettings.spriteFrameColor != settings.spriteFrameColor
                        || oldSettings.showCheckerboard != settings.showCheckerboard;
    if (bgChanged) {
        // Background color or checkerboard toggle changed — full rebuild needed
        // since background rects are not tracked individually.
        setModels(m_models);
        return;
    }

    // Label mode change: update all items in-place
    if (oldSettings.layoutLabelMode != settings.layoutLabelMode) {
        for (auto* item : m_items)
            item->setLabelMode(settings.layoutLabelMode);
    }

    // Border-only changes can be applied in-place
    if (oldSettings.borderColor != settings.borderColor || oldSettings.borderStyle != settings.borderStyle) {
        updateBorderHighlights();
    }
}

void LayoutCanvas::mousePressEvent(QMouseEvent* event) {
    if (m_displayOnly) {
        ZoomableGraphicsView::mousePressEvent(event);
        return;
    }
    // Commit split on left-click in split mode
    if ((m_splitMode || (event->modifiers() & Qt::AltModifier)) && event->button() == Qt::LeftButton && m_splitLineItem) {
        if (m_splitItemIndex >= 0 && m_splitItemIndex < m_items.size() && m_splitLineItem->isVisible()) {
            SpriteItem* item = m_items[m_splitItemIndex];
            if (item) {
                QPointF local = mapToScene(event->pos()) - item->pos();
                int pos = (m_splitOrientation == Qt::Horizontal)
                              ? qRound(local.y())
                              : qRound(local.x());
                emit splitSpriteRequested(item->getData(), m_splitOrientation, pos);
                m_splitLineItem->hide();
                m_splitItemIndex = -1;
            }
        }
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
        int clickedIndex = spriteItem->index();

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
    // Split mode: show preview line over hovered sprite
    if (m_splitMode || (event->modifiers() & Qt::AltModifier)) {
        ensureSplitLineItem();
        if (m_splitLineItem) {
            QPointF scenePos = mapToScene(event->pos());
            m_splitItemIndex = -1;
            m_splitLineItem->hide();

            for (int i = 0; i < m_items.size(); ++i) {
                SpriteItem* item = m_items[i];
                if (!item) continue;

                QRectF itemRect(item->pos(), item->pixmap().size());
                if (itemRect.contains(scenePos)) {
                    m_splitItemIndex = i;
                    QPointF local = scenePos - itemRect.topLeft();
                    m_splitOrientation = SplitModeUtils::splitOrientation(
                        local, itemRect.size());
                    if (m_splitOrientation == Qt::Horizontal) {
                        double snappedY = itemRect.top() + qRound(local.y());
                        m_splitLineItem->setLine(
                            itemRect.left(), snappedY,
                            itemRect.right(), snappedY);
                    } else {
                        double snappedX = itemRect.left() + qRound(local.x());
                        m_splitLineItem->setLine(
                            snappedX, itemRect.top(),
                            snappedX, itemRect.bottom());
                    }
                    m_splitLineItem->show();
                    break;
                }
            }
        }
        event->accept();
        return;
    }

    // Hide split line when not in split mode and Alt is not pressed
    if (m_splitLineItem) {
        m_splitLineItem->hide();
    }

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
            selectedItems.reserve(m_items.size());
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
                auto it = m_pathToIndex.constFind(paths.first());
                if (it != m_pathToIndex.constEnd()) {
                    dragPixmap = m_items[it.value()]->pixmap().scaled(64, 64, Qt::KeepAspectRatio, Qt::FastTransformation);
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
            m_lastSelectedIndex = spriteItem->index();
            finalizeSearchSelection();
        }
        m_pendingDeselect = false;
    }
    ZoomableGraphicsView::mouseReleaseEvent(event);
}

void LayoutCanvas::keyPressEvent(QKeyEvent* event) {
    if (m_displayOnly) {
        ZoomableGraphicsView::keyPressEvent(event);
        return;
    }
#ifdef Q_OS_WASM
    EM_ASM({
        console.log("[LayoutCanvas] keyPressEvent - Key: " + $0 + " Text: " + UTF8ToString($1));
    }, event->key(), event->text().toStdString().c_str());
#endif

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

    // Handle Delete key to remove selected sprites
    if (event->key() == Qt::Key_Delete) {
        QList<SpriteItem*> selectedItems;
        for (auto* item : m_items) {
            if (item->isSelectedState()) {
                selectedItems.append(item);
            }
        }
        if (!selectedItems.isEmpty()) {
            QStringList paths;
            for (auto* item : selectedItems) {
                paths << item->getData()->path;
            }
            emit removeFramesRequested(paths);
            event->accept();
            return;
        }
    }

    ZoomableGraphicsView::keyPressEvent(event);
}

void LayoutCanvas::keyReleaseEvent(QKeyEvent* event) {
    ZoomableGraphicsView::keyReleaseEvent(event);
}

void LayoutCanvas::setDisplayOnly(bool displayOnly) {
    m_displayOnly = displayOnly;
    for (auto* border : m_borderItems) border->setVisible(!displayOnly);
    for (auto* item : m_items) {
        item->setLabelHidden(displayOnly);
        if (displayOnly) {
            item->setSelectedState(false);
            item->setPrimaryState(false);
        }
    }
}

void LayoutCanvas::resizeEvent(QResizeEvent* event) {
    ZoomableGraphicsView::resizeEvent(event);
    if (m_loadingBanner && m_loadingBanner->isVisible()) {
        const int bw = m_loadingBanner->width();
        const int bh = m_loadingBanner->height();
        m_loadingBanner->move((width() - bw) / 2, (height() - bh) / 2);
    }
}

void LayoutCanvas::setLoadingHint(bool loading) {
    if (loading) {
        if (!m_loadingBanner) {
            m_loadingBanner = new QLabel(tr("Loading atlas image\u2026"), this);
            m_loadingBanner->setAlignment(Qt::AlignCenter);
            m_loadingBanner->setAttribute(Qt::WA_TransparentForMouseEvents);
            m_loadingBanner->setStyleSheet(
                "background: rgba(0,0,0,160); color: white; "
                "padding: 4px 14px; border-radius: 4px;");
            m_loadingBanner->adjustSize();
        }
        const int bw = m_loadingBanner->sizeHint().width();
        const int bh = m_loadingBanner->sizeHint().height();
        m_loadingBanner->setGeometry((width() - bw) / 2, (height() - bh) / 2, bw, bh);
        m_loadingBanner->show();
        m_loadingBanner->raise();
    } else {
        if (m_loadingBanner) m_loadingBanner->hide();
    }
}

void LayoutCanvas::setSearchQuery(const QString& query) {
    if (m_searchQuery == query) return;
    m_searchQuery = query;
    updateSearch();
}

void LayoutCanvas::setDimFilter(const QString& query) {
    m_dimFilter = query;
    const bool active = !query.isEmpty();
    for (auto* item : m_items) {
        const bool matches = !active ||
            QFileInfo(item->getData()->path).baseName().contains(query, Qt::CaseInsensitive);
        item->setOpacity(matches ? 1.0 : 0.25);
    }
}

void LayoutCanvas::contextMenuEvent(QContextMenuEvent* event) {
    if (m_displayOnly) return;
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

    menu.addSeparator();

    QList<SpriteItem*> selectedItems;
    for (auto* item : m_items) {
        if (item->isSelectedState()) {
            selectedItems.append(item);
        }
    }

    QAction* removeFramesAction = menu.addAction(tr("Exclude Selected Frames"));
    removeFramesAction->setEnabled(!selectedItems.isEmpty());
    connect(removeFramesAction, &QAction::triggered, this, [this, selectedItems]() {
        QStringList paths;
        for (auto* it : selectedItems) paths << it->getData()->path;
        emit removeFramesRequested(paths);
    });

    QAction* removeSmallAction = menu.addAction(tr("Remove Small..."));
    connect(removeSmallAction, &QAction::triggered, this, &LayoutCanvas::onRemoveSmallTriggered);

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

    menu.addSeparator();
    QAction* splitAction = menu.addAction(tr("Split Mode (S)"));
    splitAction->setCheckable(true);
    splitAction->setChecked(m_splitMode);
    splitAction->setToolTip(tr("Toggle split mode (S). Hold ALT to activate temporarily. "
                                "Orientation is determined by which edge is nearest."));
    connect(splitAction, &QAction::triggered, this, &LayoutCanvas::setSplitMode);

    menu.exec(event->globalPos());
}

void LayoutCanvas::onRemoveSmallTriggered() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Remove Small Frames"));
    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* label = new QLabel(tr("Remove frames smaller than:"), &dialog);
    layout->addWidget(label);

    QHBoxLayout* inputLayout = new QHBoxLayout();

    QLabel* widthLabel = new QLabel(tr("Width:"), &dialog);
    QSpinBox* widthSpin = new QSpinBox(&dialog);
    widthSpin->setMinimum(1);
    widthSpin->setMaximum(9999);
    widthSpin->setValue(10);
    inputLayout->addWidget(widthLabel);
    inputLayout->addWidget(widthSpin);

    QLabel* heightLabel = new QLabel(tr("Height:"), &dialog);
    QSpinBox* heightSpin = new QSpinBox(&dialog);
    heightSpin->setMinimum(1);
    heightSpin->setMaximum(9999);
    heightSpin->setValue(10);
    inputLayout->addWidget(heightLabel);
    inputLayout->addWidget(heightSpin);

    layout->addLayout(inputLayout);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (dialog.exec() == QDialog::Accepted) {
        emit removeSmallFramesRequested(widthSpin->value(), heightSpin->value());
    }
}

void LayoutCanvas::removeFramesSmallerThan(int minW, int minH) {
    QStringList removedPaths;

    int writeIdx = 0;
    for (int i = 0; i < m_items.size(); ++i) {
        auto* item = m_items[i];
        const auto& sprite = item->getData();
        if (sprite->rect.width() < minW || sprite->rect.height() < minH) {
            removedPaths.append(sprite->path);
            m_pathToIndex.remove(sprite->path);
            delete item;
        } else {
            item->setIndex(writeIdx);
            m_pathToIndex[sprite->path] = writeIdx;
            m_items[writeIdx] = item;
            ++writeIdx;
        }
    }
    m_items.resize(writeIdx);

    if (!removedPaths.isEmpty()) {
        m_baseSelectionPaths.clear();
        emit removeFramesRequested(removedPaths);
        viewport()->update();
    }
}

void LayoutCanvas::removeSprites(const QStringList& paths) {
    if (paths.isEmpty()) return;
    const QSet<QString> pathSet(paths.begin(), paths.end());
    int writeIdx = 0;
    for (int i = 0; i < m_items.size(); ++i) {
        auto* item = m_items[i];
        auto* border = (i < m_borderItems.size()) ? m_borderItems[i] : nullptr;
        if (pathSet.contains(item->getData()->path)) {
            m_pathToIndex.remove(item->getData()->path);
            delete item;
            delete border;
        } else {
            item->setIndex(writeIdx);
            m_pathToIndex[item->getData()->path] = writeIdx;
            m_items[writeIdx] = item;
            if (border) m_borderItems[writeIdx] = border;
            ++writeIdx;
        }
    }
    m_items.resize(writeIdx);
    m_borderItems.resize(writeIdx);
    m_baseSelectionPaths.clear();
    viewport()->update();
}

std::optional<QPointF> LayoutCanvas::spriteItemScenePos(const QString& spritePath) const {
    auto it = m_pathToIndex.find(spritePath);
    if (it == m_pathToIndex.end()) return std::nullopt;
    int index = it.value();
    if (index < 0 || index >= m_items.size()) return std::nullopt;
    return m_items[index]->pos();
}

void LayoutCanvas::setSpriteItemScenePos(const QString& spritePath, const QPointF& pos) {
    auto it = m_pathToIndex.find(spritePath);
    if (it == m_pathToIndex.end()) return;
    int index = it.value();
    if (index < 0 || index >= m_items.size()) return;
    const QPointF delta = pos - m_items[index]->pos();
    m_items[index]->setPos(pos);
    // Move the border outline by the same delta (it uses absolute path coordinates).
    if (index < m_borderItems.size() && m_borderItems[index])
        m_borderItems[index]->setPos(m_borderItems[index]->pos() + delta);
}

QMap<QString, QPointF> LayoutCanvas::computeItemScenePositions(const QVector<LayoutModel>& models) {
    QMap<QString, QPointF> positions;
    int currentY = 0;
    constexpr int margin = 100;
    for (const auto& model : models) {
        for (const auto& sprite : model.sprites) {
            if (sprite) {
                positions[sprite->path] = QPointF(sprite->rect.x(), currentY + sprite->rect.y());
            }
        }
        currentY += model.atlasHeight + margin;
    }
    return positions;
}

void LayoutCanvas::setSpriteItemRotation(const QString& spritePath, qreal angle) {
    auto it = m_pathToIndex.find(spritePath);
    if (it == m_pathToIndex.end()) return;
    int index = it.value();
    if (index < 0 || index >= m_items.size()) return;
    m_items[index]->setRotation(angle);
}

void LayoutCanvas::setSpriteItemTransformOrigin(const QString& spritePath, const QPointF& origin) {
    auto it = m_pathToIndex.find(spritePath);
    if (it == m_pathToIndex.end()) return;
    int index = it.value();
    if (index < 0 || index >= m_items.size()) return;
    m_items[index]->setTransformOriginPoint(origin);
}

void LayoutCanvas::setSpriteItemLabelHidden(const QString& spritePath, bool hidden) {
    auto it = m_pathToIndex.find(spritePath);
    if (it == m_pathToIndex.end()) return;
    int index = it.value();
    if (index < 0 || index >= m_items.size()) return;
    m_items[index]->setLabelHidden(hidden);
}

QVector<QRectF> LayoutCanvas::computeAtlasRects(const QVector<LayoutModel>& models) {
    QVector<QRectF> rects;
    int currentY = 0;
    constexpr int margin = 100;
    for (const auto& model : models) {
        rects.append(QRectF(0, currentY, model.atlasWidth, model.atlasHeight));
        currentY += model.atlasHeight + margin;
    }
    return rects;
}

QVector<QRectF> LayoutCanvas::currentAtlasRects() const {
    QVector<QRectF> rects;
    rects.reserve(m_atlasBackgroundItems.size());
    for (auto* item : m_atlasBackgroundItems)
        rects.append(item ? item->rect() : QRectF());
    return rects;
}

void LayoutCanvas::setAtlasRect(int index, const QRectF& rect) {
    if (index < 0 || index >= m_atlasBackgroundItems.size()) return;
    if (m_atlasBackgroundItems[index])
        m_atlasBackgroundItems[index]->setRect(rect);
}

void LayoutCanvas::scrollToAtlas(int index) {
    if (index < 0 || index >= m_atlasBackgroundItems.size()) return;
    if (const auto* item = m_atlasBackgroundItems.at(index))
        centerOn(item);
}

void LayoutCanvas::freezeSceneRect() {
    m_scene->setSceneRect(m_scene->itemsBoundingRect());
}

void LayoutCanvas::thawSceneRect() {
    m_scene->setSceneRect(QRectF());
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
    // Only update if selection actually changed - prevents continuous repaint loop
    if (selectionChangedOccurred) {
        viewport()->update();
        update();
    }
}

void LayoutCanvas::finalizeSearchSelection() {
    m_baseSelectionPaths.clear();
    for (auto* item : m_items) {
        if (item->isSelectedState()) {
            m_baseSelectionPaths.insert(item->getData()->path);
        }
    }
    emitSelectionChanged();
}

void LayoutCanvas::emitSelectionChanged() {
    QList<SpritePtr> selection;
    selection.reserve(m_items.size());
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

void LayoutCanvas::enterEvent(QEnterEvent* event) {
    ZoomableGraphicsView::enterEvent(event);
    emit userInteractionStarted();
}

void LayoutCanvas::leaveEvent(QEvent* event) {
    ZoomableGraphicsView::leaveEvent(event);
    emit userInteractionEnded();
}
