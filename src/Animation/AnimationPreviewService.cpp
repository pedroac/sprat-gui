#include "AnimationPreviewService.h"

#include <QImageReader>
#include <QPainter>
#include <QPixmapCache>
#include <QTimer>
#include <QCoreApplication>
#include <QHash>
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
    QStringList frames;
    quint64 generation = ~quint64(0);
    int left = 0, right = 0, top = 0, bottom = 0;
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

    for (const QString& path : frames) {
        QPixmap cached;
        if (!QPixmapCache::find(path, &cached)) {
            QPixmap pix;
            if (pix.load(path))
                QPixmapCache::insert(path, pix);
        }
    }
}

// ---------------------------------------------------------------------------

// Recompute the pivot-aligned bounds for every frame in the timeline and
// store the result in g_boundsCache.  Called only on cache miss.
static void recomputeBounds(const QStringList& frames,
                             const QHash<QString, SpritePtr>& spriteMap)
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

    g_boundsCache.frames     = frames;
    g_boundsCache.generation = g_boundsGeneration;
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
    QTimer* timer)
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

    // Bounds — O(frames) computation, cached until frames or pivots change.
    if (g_boundsCache.generation != g_boundsGeneration || g_boundsCache.frames != frames)
        recomputeBounds(frames, spriteMap);

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
    int pivotX = currentSprite ? qBound(0, currentSprite->pivotX, pix.width())  : pix.width()  / 2;
    int pivotY = currentSprite ? qBound(0, currentSprite->pivotY, pix.height()) : pix.height() / 2;
    if (hFlip || vFlip) {
        pix = pix.transformed(QTransform().scale(hFlip ? -1.0 : 1.0, vFlip ? -1.0 : 1.0));
        if (hFlip) pivotX = pix.width()  - pivotX;
        if (vFlip) pivotY = pix.height() - pivotY;
    }
    p.drawPixmap(maxLeftExtent - pivotX, maxTopExtent - pivotY, pix);
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

    if (g_boundsCache.generation != g_boundsGeneration || g_boundsCache.frames != frames)
        recomputeBounds(frames, g_cachedSpriteMap);

    const int animationWidth  = qMax(1, g_boundsCache.left  + g_boundsCache.right);
    const int animationHeight = qMax(1, g_boundsCache.top   + g_boundsCache.bottom);

    return QSize(qRound(animationWidth * zoom), qRound(animationHeight * zoom));
}
