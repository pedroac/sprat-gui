#include "PreviewCanvas.h"
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QImage>
#include <QPixmapCache>
#include <QKeySequence>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QApplication>
#include <QPainter>
#include <QtConcurrent>
#include "ViewUtils.h"

namespace {
class GridOverlayItem : public QGraphicsItem {
public:
    GridOverlayItem() { setZValue(0.25); }

    void setParams(int cellW, int cellH, int offX, int offY,
                   const QColor& color, const QSizeF& size)
    {
        m_cellW = qMax(1, cellW);
        m_cellH = qMax(1, cellH);
        m_offX  = offX;
        m_offY  = offY;
        m_color = color;
        m_size  = size;
        update();
    }

    QRectF boundingRect() const override {
        return QRectF(QPointF(0, 0), m_size);
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
        if (m_size.isEmpty()) return;
        QPen pen(m_color, 0);
        pen.setCosmetic(true);
        painter->setPen(pen);

        const int w = qRound(m_size.width());
        const int h = qRound(m_size.height());

        // First column position >= 0 satisfying (x - offX) % cellW == 0
        const int startX = ((m_offX % m_cellW) + m_cellW) % m_cellW;
        for (int x = startX; x < w; x += m_cellW)
            painter->drawLine(x, 0, x, h);

        // First row position >= 0 satisfying (y - offY) % cellH == 0
        const int startY = ((m_offY % m_cellH) + m_cellH) % m_cellH;
        for (int y = startY; y < h; y += m_cellH)
            painter->drawLine(0, y, w, y);
    }

private:
    int    m_cellW = 16, m_cellH = 16;
    int    m_offX  = 0,  m_offY  = 0;
    QColor m_color = QColor(255, 255, 255, 80);
    QSizeF m_size;
};
} // namespace

PreviewCanvas::~PreviewCanvas() {
    // Ensure the background trim-rect scan (which captures 'this' raw) has
    // finished before our memory is freed.  Without this, the thread can call
    // QMetaObject::invokeMethod(this, ...) after destruction.
    m_trimWatcher.waitForFinished();
}

PreviewCanvas::PreviewCanvas(QWidget* parent) : ZoomableGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setBackgroundBrush(m_settings.workspaceColor);

    m_overlay = new EditorOverlayItem();
    m_scene->addItem(m_overlay);
    
    connect(m_overlay, &EditorOverlayItem::pivotChanged, this, &PreviewCanvas::pivotChanged);
}

// ---------------------------------------------------------------------------
// Trim rect helpers
// ---------------------------------------------------------------------------

QRect PreviewCanvas::computeTrimRect(const QImage& img) {
    if (img.isNull()) return QRect();
    const int w = img.width();
    const int h = img.height();
    int top = h, bottom = -1, left = w, right = -1;
    for (int y = 0; y < h; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if (qAlpha(line[x]) > 0) {
                if (y < top)    top    = y;
                if (y > bottom) bottom = y;
                if (x < left)   left   = x;
                if (x > right)  right  = x;
            }
        }
    }
    if (bottom < 0) return QRect(); // fully transparent
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

QRect PreviewCanvas::cachedTrimRect() {
    if (m_sprites.isEmpty()) return {};
    const QString path = m_sprites.first()->path;
    const QDateTime ts = QFileInfo(path).lastModified();

    // Cache hit — instant return.
    if (m_trimCache.path == path && m_trimCache.timestamp == ts)
        return m_trimCache.rect;

    // A computation for this same cache miss is already in flight.
    if (m_trimWatcher.isRunning())
        return m_trimCache.rect;   // return stale (or empty) rect while computing

    // Cache miss: stamp the cache, clear the stale rect, launch background compute.
    m_trimCache.path      = path;
    m_trimCache.timestamp = ts;
    m_trimCache.rect      = {};

    // The background lambda loads and scans the image (safe on any thread).
    // The result is delivered back to the main thread via invokeMethod.
    // We capture `path` by value so we can guard against sprites that changed
    // while the compute was in flight.
    m_trimWatcher.setFuture(QtConcurrent::run([this, path]() -> QRect {
        QRect rect = PreviewCanvas::computeTrimRect(
            QImage(path).convertToFormat(QImage::Format_ARGB32));
        // Qt guarantees the functor is NOT called if `this` is destroyed before
        // the queued invocation runs (context-object safety).
        QMetaObject::invokeMethod(this, [this, path, rect]() {
            // Ignore stale results for a sprite that was replaced.
            if (!m_sprites.isEmpty() && m_sprites.first()->path == path) {
                m_trimCache.rect = rect;
                updateTrimRectItem();
            }
        }, Qt::QueuedConnection);
        return rect;
    }));

    return {};
}

void PreviewCanvas::updateTrimRectItem() {
    if (m_trimRectItem) {
        m_scene->removeItem(m_trimRectItem);
        delete m_trimRectItem;
        m_trimRectItem = nullptr;
    }
    if (m_trimDimItem) {
        m_scene->removeItem(m_trimDimItem);
        delete m_trimDimItem;
        m_trimDimItem = nullptr;
    }
    if (!m_settings.showTrimRect || m_sprites.isEmpty()) {
        m_overlay->setTrimRect(QRect());
        return;
    }

    const QRect trimRect = cachedTrimRect();
    if (!trimRect.isValid()) return;

    // Dim the area outside the trim rect using an OddEven-filled path "frame"
    if (!m_imageItems.isEmpty()) {
        QPainterPath path;
        path.setFillRule(Qt::OddEvenFill);
        path.addRect(m_imageItems.first()->boundingRect());
        path.addRect(trimRect);
        m_trimDimItem = new QGraphicsPathItem(path);
        m_trimDimItem->setPen(Qt::NoPen);
        m_trimDimItem->setBrush(QColor(0, 0, 0, 90));
        m_trimDimItem->setZValue(0.4);
        m_scene->addItem(m_trimDimItem);
    }

    QPen pen(m_settings.trimRectColor, 1, m_settings.trimRectStyle);
    pen.setCosmetic(true);
    m_trimRectItem = new QGraphicsRectItem(trimRect);
    m_trimRectItem->setPen(pen);
    m_trimRectItem->setBrush(Qt::NoBrush);
    m_trimRectItem->setZValue(0.5); // above sprite (z=0), below overlay
    m_scene->addItem(m_trimRectItem);

    m_overlay->setTrimRect(trimRect);
}

void PreviewCanvas::updateGridItem() {
    if (m_gridItem) {
        m_scene->removeItem(m_gridItem);
        delete m_gridItem;
        m_gridItem = nullptr;
    }
    if (!m_settings.showGrid || m_imageItems.isEmpty()) return;

    auto* grid = new GridOverlayItem();
    grid->setParams(m_settings.gridCellWidth, m_settings.gridCellHeight,
                    m_settings.gridOffsetX,   m_settings.gridOffsetY,
                    m_settings.gridColor,
                    m_imageItems.first()->boundingRect().size());
    m_scene->addItem(grid);
    m_gridItem = grid;
}

void PreviewCanvas::setSprites(const QList<SpritePtr>& sprites) {
    // Cancel any in-flight trim-rect computation for the old sprite so it doesn't
    // waste CPU on a result we no longer need.  waitForFinished() is intentionally
    // omitted: the background lambda only reads its captured `path` copy and posts
    // a QueuedConnection — it never touches m_trimCache or m_sprites directly.
    // The callback's path guard handles any stale QueuedConnection that fires after
    // we update m_sprites below.
    if (m_trimWatcher.isRunning())
        m_trimWatcher.cancel();
    m_trimCache = {};

    m_sprites = sprites;
    m_overlay->setSprites(sprites);

    for (auto* item : m_imageItems) delete item;
    m_imageItems.clear();
    for (auto* item : m_borderItems) delete item;
    m_borderItems.clear();
    for (auto* item : m_ghostItems) { m_scene->removeItem(item); delete item; }
    m_ghostItems.clear();
    if (m_trimRectItem) { m_scene->removeItem(m_trimRectItem); delete m_trimRectItem; m_trimRectItem = nullptr; }
    if (m_trimDimItem) { m_scene->removeItem(m_trimDimItem); delete m_trimDimItem; m_trimDimItem = nullptr; }

    if (!sprites.isEmpty()) {
        QPen borderPen(m_settings.borderColor, 2, m_settings.borderStyle);
        borderPen.setCosmetic(true);

        QRectF totalRect;
        QSize firstSize;
        for (const auto& sprite : sprites) {
            QPixmap pix;
            if (!QPixmapCache::find(sprite->path, &pix)) {
                pix = QPixmap(sprite->path);
                if (!pix.isNull())
                    QPixmapCache::insert(sprite->path, pix);
            }
            if (pix.isNull()) continue;

            if (firstSize.isEmpty()) firstSize = pix.size();

            auto* bgItem = new QGraphicsRectItem(pix.rect());
            if (m_settings.showCheckerboard) {
                bgItem->setBrush(QBrush(createCheckerboardPixmap(m_settings.spriteFrameColor)));
            } else {
                bgItem->setBrush(m_settings.spriteFrameColor);
            }
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

        if (!firstSize.isEmpty())
            m_overlay->setSceneSize(firstSize);
    } else {
        m_scene->setSceneRect(QRectF());
    }

    updateTrimRectItem();
    updateGridItem();
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

void PreviewCanvas::alignPivotToScreenPos(QPoint pivot, QPoint screenPos) {
    // We want `pivot` (scene coords) to appear at `screenPos` (viewport coords).
    // centerOn(target) places `target` at the viewport centre.
    // So: target = pivot - (screenPos - viewportCentre) / scale
    //
    // IMPORTANT: Qt clamps centerOn to the sceneRect.  After setSprites the
    // sceneRect equals the image bounds, which often fits entirely inside the
    // viewport at fit-to-view zoom, leaving zero scrollable room and making
    // centerOn a no-op.  We expand the sceneRect by one viewport-size margin
    // in every direction so there is always room to reach the desired position.
    const double scale = transform().m11(); // zoom factor
    const double margin = qMax(viewport()->width(), viewport()->height()) / scale;
    m_scene->setSceneRect(m_scene->sceneRect().adjusted(-margin, -margin, margin, margin));

    const QPointF viewportCentre(viewport()->width() / 2.0, viewport()->height() / 2.0);
    const QPointF delta = QPointF(screenPos) - viewportCentre;
    centerOn(QPointF(pivot) - delta / scale);
}

void PreviewCanvas::initialFit() {
    ZoomableGraphicsView::initialFit();
    m_overlay->update();
}

QPointF PreviewCanvas::viewportCenterInScene() const {
    return mapToScene(viewport()->rect().center());
}

void PreviewCanvas::keyPressEvent(QKeyEvent* event) {
    if (!m_sprites.isEmpty() && m_overlay) {
        if (event->matches(QKeySequence::Copy))  { emit copyMarkersRequested();  event->accept(); return; }
        if (event->matches(QKeySequence::Paste)) { emit pasteMarkersRequested(); event->accept(); return; }
        const int step = (event->modifiers() & Qt::ShiftModifier) ? 10 : 1;
        switch (event->key()) {
            case Qt::Key_Left:  m_overlay->nudge(-step,     0); event->accept(); return;
            case Qt::Key_Right: m_overlay->nudge( step,     0); event->accept(); return;
            case Qt::Key_Up:    m_overlay->nudge(    0, -step); event->accept(); return;
            case Qt::Key_Down:  m_overlay->nudge(    0,  step); event->accept(); return;
            default: break;
        }
    }
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
    QAction* selected = menu.exec(event->globalPos());
    if (selected == copyAction) {
        QApplication::clipboard()->setImage(QImage(m_sprites.first()->path));
    }
}

void PreviewCanvas::setGhostSprites(const QList<SpritePtr>& ghosts, QPoint activePivot) {
    for (auto* item : m_ghostItems) { m_scene->removeItem(item); delete item; }
    m_ghostItems.clear();
    for (const auto& sprite : ghosts) {
        QPixmap pix;
        if (!QPixmapCache::find(sprite->path, &pix)) {
            pix = QPixmap(sprite->path);
            if (!pix.isNull())
                QPixmapCache::insert(sprite->path, pix);
        }
        if (pix.isNull()) continue;
        auto* item = new QGraphicsPixmapItem(pix);
        item->setOpacity(m_settings.onionSkinOpacity / 100.0);
        item->setZValue(-0.5);  // above checkerboard (z=-1), below main sprite (z=0)
        // Offset so ghost's pivot aligns with the active sprite's pivot in scene space
        item->setPos(activePivot.x() - sprite->pivotX,
                     activePivot.y() - sprite->pivotY);
        m_scene->addItem(item);
        m_ghostItems.append(item);
    }
}

void PreviewCanvas::setSettings(const AppSettings& settings) {
    m_settings = settings;
    setBackgroundBrush(settings.workspaceColor);

    QPen borderPen(settings.borderColor, 2, settings.borderStyle);
    borderPen.setCosmetic(true);
    for (auto* item : m_borderItems) {
        if (item->zValue() == -1) {
            if (settings.showCheckerboard) {
                item->setBrush(QBrush(createCheckerboardPixmap(settings.spriteFrameColor)));
            } else {
                item->setBrush(settings.spriteFrameColor);
            }
        } else {
            item->setPen(borderPen);
        }
    }

    updateTrimRectItem();
    updateGridItem();
}
