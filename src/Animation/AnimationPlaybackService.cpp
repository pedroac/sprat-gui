#include "AnimationPlaybackService.h"

#include <QTimer>
#include "AnimationTestOps.h"

namespace {
const QStringList& effectiveFrames(const QVector<AnimationTimeline>& timelines, int index) {
    static const QStringList kEmpty;
    if (index < 0 || index >= timelines.size())
        return kEmpty;
    return aliasSourceTimeline(timelines, index).frames;
}
}

bool AnimationPlaybackService::prev(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int& frameIndex, bool& playing, QTimer* timer) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return false;
    }
    if (!AnimationTestOps::stepPrev(effectiveFrames(timelines, selectedTimelineIndex), frameIndex)) {
        return false;
    }
    playing = false;
    timer->stop();
    return true;
}

bool AnimationPlaybackService::next(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int& frameIndex, bool& playing, QTimer* timer) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return false;
    }
    if (!AnimationTestOps::stepNext(effectiveFrames(timelines, selectedTimelineIndex), frameIndex)) {
        return false;
    }
    playing = false;
    timer->stop();
    return true;
}

bool AnimationPlaybackService::tick(const QVector<AnimationTimeline>& timelines,
                                    int selectedTimelineIndex,
                                    int& frameIndex,
                                    int& playStep,
                                    qint64 elapsedMs,
                                    int fps,
                                    bool& finished) {
    finished = false;
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return false;
    }
    // How many frame-durations elapsed in real time?
    // Capped at 8 to prevent large jumps after the tab was backgrounded.
    const double msPerFrame = 1000.0 / qMax(1, fps);
    const int count = qBound(1, qRound(static_cast<double>(elapsedMs) / msPerFrame), 8);
    const AnimationTimeline& source = aliasSourceTimeline(timelines, selectedTimelineIndex);
    return AnimationTestOps::tickPlayOrder(
        animationPlayOrder(source.frames.size(), source.loopMode),
        animationLoopModeRepeats(source.loopMode),
        frameIndex, playStep, count, finished);
}

bool AnimationPlaybackService::togglePlayPause(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int fps, int& frameIndex, int& playStep, bool& playing, QTimer* timer) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return false;
    }
    if (effectiveFrames(timelines, selectedTimelineIndex).isEmpty()) {
        return false;
    }
    playing = !playing;
    if (playing) {
        // An animation that plays once and already ended starts over.
        const AnimationTimeline& source = aliasSourceTimeline(timelines, selectedTimelineIndex);
        const QVector<int> order = animationPlayOrder(source.frames.size(), source.loopMode);
        if (!animationLoopModeRepeats(source.loopMode)
                && AnimationTestOps::currentPlayStep(order, frameIndex, playStep) == order.size() - 1) {
            playStep = 0;
            frameIndex = order.first();
        }
#ifdef Q_OS_WASM
        // In WASM, Qt's event loop is driven by requestAnimationFrame (~60 fps).
        // interval=0 fires on every RAF callback; QElapsedTimer in onAnimTimerTimeout
        // enforces the actual frame rate so we don't render more often than needed.
        timer->start(0);
#else
        timer->start(qRound(1000.0 / fps));
#endif
    } else {
        timer->stop();
    }
    return true;
}
