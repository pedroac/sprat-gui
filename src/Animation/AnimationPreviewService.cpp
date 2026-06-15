#include "AnimationPreviewService.h"

#include <QImageReader>
#include <QPainter>
#include <QPixmapCache>
#include <QTimer>
#include <QCoreApplication>
#include <QHash>
#include <QtConcurrent>
#include <QtGlobal>

namespace {

QString trAnimationPreview(const char* text) {
    return QCoreApplication::translate("AnimationPreviewService", text);
}

QHash<QString, SpritePtr> buildSpriteMap(const QVector<LayoutModel>& layoutModels) {
    QHash<QString, SpritePtr> map;
    for (const auto& model : layoutModels) {
        map.reserve(map.size() + model.sprites.size());
        for (const auto& sprite : model.sprites) {
            map.insert(sprite->path, sprite);
        }
    }
    return map;
}

// ---------------------------------------------------------------------------
// Sprite map cache — rebuilt only on invalidateSpriteMap()
// ---------------------------------------------------------------------------
quint64 g_spriteMapGeneration = 0;
quint64 g_cachedSpriteMapGen  = ~quint64(0); // mismatched → first call builds it
QHash<QString, SpritePtr> g_cachedSpriteMap;

// ---------------------------------------------------------------------------
// Bounds cache — the O(frames) pivot-aligned canvas-size computation.
// Rebuilt only when the frames list changes or invalidateBounds() is called.
// ---------------------------------------------------------------------------
quint64 g_boundsGeneration = 0;

struct BoundsCache {
    int     selectedTimelineIndex = -2;  // -2 = never computed; compared O(1) instead of O(N) frame list
    quint64 generation = ~quint64(0);
    int     left = 0, right = 0, top = 0, bottom = 0;
};
BoundsCache g_boundsCache;

// ---------------------------------------------------------------------------
// Frame size cache — shared between refresh() and calculateAnimationSize()
// ---------------------------------------------------------------------------
QHash<QString, QSize> g_frameSizeCache;

// ---------------------------------------------------------------------------
// Preload tracking — avoid re-preloading unchanged timelines
// ---------------------------------------------------------------------------
size_t g_preloadedTimelineHash = 0;

} // namespace

// ---------------------------------------------------------------------------

void AnimationPreviewService::invalidateSpriteMap()
{
    ++g_spriteMapGeneration;
    ++g_boundsGeneration; // layout rebuild may affect pivots
}

void AnimationPreviewService::invalidateBounds()
{
    ++g_boundsGeneration;
}

int AnimationPreviewService::cachedBoundsLeft() { return g_boundsCache.left; }
int AnimationPreviewService::cachedBoundsTop()  { return g_boundsCache.top;  }

void AnimationPreviewService::preloadTimeline(const QStringList& frames)
{
    if (frames.isEmpty())
        return;

    size_t h = static_cast<size_t>(frames.size());
    for (const QString& f : frames)
        h ^= static_cast<size_t>(qHash(f)) + 0x9e3779b9u + (h << 6) + (h >> 2);

    if (h == g_preloadedTimelineHash)
        return;
    g_preloadedTimelineHash = h;

    // Collect only the frames not already in the pixmap cache.
    QStringList toLoad;
    for (const QString& path : frames) {
        QPixmap tmp;
        if (!QPixmapCache::find(path, &tmp))
            toLoad.append(path);
    }
    if (toLoad.isEmpty())
        return;

    // Load QImages on a background thread (thread-safe disk I/O) and insert
    // them into QPixmapCache on the main thread (QPixmap is not re-entrant).
    // The returned QFuture is intentionally discarded — this is fire-and-forget.
    auto preloadFuture = QtConcurrent::run([toLoad]() {
        for (const QString& path : toLoad) {
            QImage img(path);
            if (img.isNull())
                continue;
            QMetaObject::invokeMethod(qApp, [path, img]() {
                // Double-check: another path may have been inserted since we checked.
                QPixmap tmp;
                if (!QPixmapCache::find(path, &tmp))
                    QPixmapCache::insert(path, QPixmap::fromImage(img));
            }, Qt::QueuedConnection);
        }
    });
    Q_UNUSED(preloadFuture)
}

// ---------------------------------------------------------------------------

// Recompute the pivot-aligned bounds for every frame in the timeline and
// store the result in g_boundsCache.  Called only on cache miss.
static void recomputeBounds(const QStringList& frames,
                             const QHash<QString, SpritePtr>& spriteMap,
                             int timelineIndex)
{
    if (g_frameSizeCache.size() > 16384) {
        auto it = g_frameSizeCache.begin();
        int toRemove = g_frameSizeCache.size() / 2;
        while (toRemove-- > 0 && it != g_frameSizeCache.end()) {
            it = g_frameSizeCache.erase(it);
        }
    }

    int maxLeft = 0, maxRight = 0, maxTop = 0, maxBottom = 0;

    for (const QString& framePath : frames) {
        QSize frameSize = g_frameSizeCache.value(framePath);
        if (!frameSize.isValid()) {
            frameSize = QImageReader(framePath).size();
            if (frameSize.isValid())
                g_frameSizeCache.insert(framePath, frameSize);
        }
        if (!frameSize.isValid())
            continue;

        int pivotX = frameSize.width()  / 2;
        int pivotY = frameSize.height() / 2;
        auto it = spriteMap.constFind(framePath);
        if (it != spriteMap.constEnd()) {
            pivotX = qBound(0, it.value()->pivotX, frameSize.width());
            pivotY = qBound(0, it.value()->pivotY, frameSize.height());
        }

        maxLeft   = qMax(maxLeft,   pivotX);
        maxRight  = qMax(maxRight,  frameSize.width()  - pivotX);
        maxTop    = qMax(maxTop,    pivotY);
        maxBottom = qMax(maxBottom, frameSize.height() - pivotY);
    }

    g_boundsCache.selectedTimelineIndex = timelineIndex;
    g_boundsCache.generation            = g_boundsGeneration;
    g_boundsCache.left       = maxLeft;
    g_boundsCache.right      = maxRight;
    g_boundsCache.top        = maxTop;
    g_boundsCache.bottom     = maxBottom;
}

// ---------------------------------------------------------------------------

QPixmap AnimationPreviewService::refresh(
    const QVector<AnimationTimeline>& timelines,
    int selectedTimelineIndex,
    int& frameIndex,
    const QVector<LayoutModel>& layoutModels,
    QString& statusText,
    bool& hasFrames,
    bool& playing,
    QTimer* timer,
    bool onionSkin,
    int onionOpacity)
{
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        statusText = trAnimationPreview("Create/select a timeline and drag frames into it.");
        hasFrames = false;
        if (playing) {
            playing = false;
            timer->stop();
        }
        return QPixmap();
    }

    const auto& selectedTimeline = timelines[selectedTimelineIndex];

    // Resolve effective frames (alias support)
    const QStringList* effectiveFrames = &selectedTimeline.frames;
    bool hFlip = selectedTimeline.hFlip;
    bool vFlip = selectedTimeline.vFlip;

    if (!selectedTimeline.aliasOf.isEmpty()) {
        for (const auto& tl : timelines) {
            if (tl.name == selectedTimeline.aliasOf && tl.aliasOf.isEmpty()) {
                effectiveFrames = &tl.frames;
                break;
            }
        }
    }

    if (effectiveFrames->isEmpty()) {
        statusText = trAnimationPreview("Create/select a timeline and drag frames into it.");
        hasFrames = false;
        if (playing) {
            playing = false;
            timer->stop();
        }
        return QPixmap();
    }

    hasFrames = true;
    const QStringList& frames = *effectiveFrames;
    if (frameIndex >= frames.size())
        frameIndex = 0;

    // Sprite map — rebuild only after a layout change.
    if (g_cachedSpriteMapGen != g_spriteMapGeneration) {
        g_cachedSpriteMap    = buildSpriteMap(layoutModels);
        g_cachedSpriteMapGen = g_spriteMapGeneration;
    }
    const QHash<QString, SpritePtr>& spriteMap = g_cachedSpriteMap;

    const QString& path = frames[frameIndex];
    SpritePtr currentSprite = spriteMap.value(path);

    statusText = QString("frame %1/%2")
                     .arg(frameIndex + 1)
                     .arg(frames.size());

    QPixmap pix;
    if (!QPixmapCache::find(path, &pix)) {
        pix.load(path);
        QPixmapCache::insert(path, pix);
    }
    if (pix.isNull())
        return QPixmap();

    // Bounds — O(frames) computation, cached until the selected timeline or pivots change.
    // Comparing selectedTimelineIndex is O(1); changing frames within a timeline already
    // increments g_boundsGeneration via invalidateBounds(), so the generation check covers it.
    if (g_boundsCache.generation != g_boundsGeneration
            || g_boundsCache.selectedTimelineIndex != selectedTimelineIndex)
        recomputeBounds(frames, spriteMap, selectedTimelineIndex);

    int maxLeftExtent   = g_boundsCache.left;
    int maxRightExtent  = g_boundsCache.right;
    int maxTopExtent    = g_boundsCache.top;
    int maxBottomExtent = g_boundsCache.bottom;

    // Degenerate fallback: all frames have unknown/zero size.
    if (maxLeftExtent <= 0 && maxRightExtent <= 0 && maxTopExtent <= 0 && maxBottomExtent <= 0) {
        int pivotX = currentSprite ? qBound(0, currentSprite->pivotX, pix.width())  : pix.width()  / 2;
        int pivotY = currentSprite ? qBound(0, currentSprite->pivotY, pix.height()) : pix.height() / 2;
        maxLeftExtent   = qMax(1, pivotX);
        maxRightExtent  = qMax(1, pix.width()  - pivotX);
        maxTopExtent    = qMax(1, pivotY);
        maxBottomExtent = qMax(1, pix.height() - pivotY);
    }

    const int animationWidth  = qMax(1, maxLeftExtent + maxRightExtent);
    const int animationHeight = qMax(1, maxTopExtent  + maxBottomExtent);

    QPixmap canvas(animationWidth, animationHeight);
    canvas.fill(Qt::transparent);

    QPainter p(&canvas);

    // Onion skin: draw adjacent frames as semi-transparent ghosts underneath.
    if (onionSkin && onionOpacity > 0) {
        const qreal ghostAlpha = onionOpacity / 100.0;
        // Indices of adjacent frames to render as ghosts (prev and next).
        const int ghostIndices[2] = { frameIndex - 1, frameIndex + 1 };
        for (int gi : ghostIndices) {
            if (gi < 0 || gi >= frames.size()) continue;
            const QString& ghostPath = frames[gi];
            QPixmap ghostPix;
            if (!QPixmapCache::find(ghostPath, &ghostPix)) {
                ghostPix.load(ghostPath);
                if (!ghostPix.isNull())
                    QPixmapCache::insert(ghostPath, ghostPix);
            }
            if (ghostPix.isNull()) continue;
            SpritePtr ghostSprite = spriteMap.value(ghostPath);
            int gx = ghostSprite ? qBound(0, ghostSprite->pivotX, ghostPix.width())  : ghostPix.width()  / 2;
            int gy = ghostSprite ? qBound(0, ghostSprite->pivotY, ghostPix.height()) : ghostPix.height() / 2;
            p.setOpacity(ghostAlpha);
            if (hFlip || vFlip) {
                // Use a world-transform flip instead of allocating a new QPixmap per frame.
                // For scale s in axis X (s = ±1): the pixel at (gx, gy) in image space must land
                // at (maxLeftExtent, maxTopExtent) on the canvas.
                // With QTransform().translate(tx, ty).scale(sx, sy): point p' = sx·p + tx.
                // Setting tx = maxLeftExtent - sx·gx satisfies sx·gx + tx = maxLeftExtent. ✓
                const qreal sx = hFlip ? -1.0 : 1.0;
                const qreal sy = vFlip ? -1.0 : 1.0;
                p.save();
                p.setWorldTransform(QTransform()
                    .translate(maxLeftExtent - sx * gx, maxTopExtent - sy * gy)
                    .scale(sx, sy));
                p.drawPixmap(0, 0, ghostPix);
                p.restore();
            } else {
                p.drawPixmap(maxLeftExtent - gx, maxTopExtent - gy, ghostPix);
            }
        }
        p.setOpacity(1.0);
    }

    int pivotX = currentSprite ? qBound(0, currentSprite->pivotX, pix.width())  : pix.width()  / 2;
    int pivotY = currentSprite ? qBound(0, currentSprite->pivotY, pix.height()) : pix.height() / 2;
    if (hFlip || vFlip) {
        const qreal sx = hFlip ? -1.0 : 1.0;
        const qreal sy = vFlip ? -1.0 : 1.0;
        p.save();
        p.setWorldTransform(QTransform()
            .translate(maxLeftExtent - sx * pivotX, maxTopExtent - sy * pivotY)
            .scale(sx, sy));
        p.drawPixmap(0, 0, pix);
        p.restore();
    } else {
        p.drawPixmap(maxLeftExtent - pivotX, maxTopExtent - pivotY, pix);
    }
    p.end();

    return canvas;
}

// ---------------------------------------------------------------------------

QSize AnimationPreviewService::calculateAnimationSize(
    const QVector<AnimationTimeline>& timelines,
    int selectedTimelineIndex,
    const QVector<LayoutModel>& layoutModels,
    double zoom,
    int previewPadding)
{
    Q_UNUSED(previewPadding);
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return QSize(280, 180);
    }

    const auto& calcTimeline = timelines[selectedTimelineIndex];
    const QStringList* effectiveFramesCalc = &calcTimeline.frames;
    if (!calcTimeline.aliasOf.isEmpty()) {
        for (const auto& tl : timelines) {
            if (tl.name == calcTimeline.aliasOf && tl.aliasOf.isEmpty()) {
                effectiveFramesCalc = &tl.frames;
                break;
            }
        }
    }

    if (effectiveFramesCalc->isEmpty()) {
        return QSize(280, 180);
    }

    const QStringList& frames = *effectiveFramesCalc;

    if (g_cachedSpriteMapGen != g_spriteMapGeneration) {
        g_cachedSpriteMap    = buildSpriteMap(layoutModels);
        g_cachedSpriteMapGen = g_spriteMapGeneration;
    }

    if (g_boundsCache.generation != g_boundsGeneration
            || g_boundsCache.selectedTimelineIndex != selectedTimelineIndex)
        recomputeBounds(frames, g_cachedSpriteMap, selectedTimelineIndex);

    const int animationWidth  = qMax(1, g_boundsCache.left  + g_boundsCache.right);
    const int animationHeight = qMax(1, g_boundsCache.top   + g_boundsCache.bottom);

    return QSize(qRound(animationWidth * zoom), qRound(animationHeight * zoom));
}
