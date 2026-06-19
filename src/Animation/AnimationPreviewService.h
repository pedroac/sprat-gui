#pragma once

#include <QVector>
#include <QPixmap>
#include "AnimationModels.h"
#include "LayoutModels.h"

class QLabel;
class QPushButton;
class QTimer;

class AnimationPreviewService {
public:
    static QPixmap refresh(
        const QVector<AnimationTimeline>& timelines,
        int selectedTimelineIndex,
        int& frameIndex,
        const QVector<LayoutModel>& layoutModels,
        QString& statusText,
        bool& hasFrames,
        bool& playing,
        QTimer* timer,
        bool onionSkin = false,
        int onionOpacity = 30);

    static QSize calculateAnimationSize(
        const QVector<AnimationTimeline>& timelines,
        int selectedTimelineIndex,
        const QVector<LayoutModel>& layoutModels,
        double zoom,
        int previewPadding);

    // Preload all frames in the given list into QPixmapCache so the first
    // playthrough has no per-frame disk reads.  Safe to call every tick —
    // a hash check makes it a no-op when the timeline has not changed.
    static void preloadTimeline(const QStringList& frames);

    // Invalidate the cached sprite map so the next refresh() rebuilds it
    // from the current layout models.  Call whenever layoutModels changes.
    // Also invalidates the bounds cache.
    static void invalidateSpriteMap();

    // Invalidate the cached canvas bounds so the next refresh() recomputes
    // them.  Call whenever any sprite pivot changes.
    static void invalidateBounds();

    // Return the cached composite-canvas left/top extents (pixels from the
    // pivot alignment point to the left/top edge of the canvas).  Valid after
    // the most recent refresh() call.  Used to align the overlay item so that
    // its item-local coordinate system matches sprite-local coordinates.
    static int cachedBoundsLeft();
    static int cachedBoundsTop();
};
