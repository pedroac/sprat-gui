#include "AnimationPlaybackService.h"

#include <QCoreApplication>
#include <QApplication>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include "AnimationTestOps.h"

namespace {
// Resolve the effective frame list for playback, following alias references.
const QStringList& effectiveFrames(const QVector<AnimationTimeline>& timelines, int index) {
    static const QStringList kEmpty;
    if (index < 0 || index >= timelines.size())
        return kEmpty;
    const auto& tl = timelines[index];
    if (!tl.aliasOf.isEmpty()) {
        for (const auto& src : timelines) {
            if (src.name == tl.aliasOf && src.aliasOf.isEmpty())
                return src.frames;
        }
    }
    return tl.frames;
}

const QIcon& playIcon() {
    static const QIcon icon = QApplication::style()->standardIcon(QStyle::SP_MediaPlay);
    return icon;
}

const QIcon& pauseIcon() {
    static const QIcon icon = QApplication::style()->standardIcon(QStyle::SP_MediaPause);
    return icon;
}
}

bool AnimationPlaybackService::prev(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int& frameIndex, bool& playing, QTimer* timer, QPushButton* playPauseButton) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return false;
    }
    if (!AnimationTestOps::stepPrev(effectiveFrames(timelines, selectedTimelineIndex), frameIndex)) {
        return false;
    }
    playing = false;
    timer->stop();
    playPauseButton->setIcon(playIcon());
    return true;
}

bool AnimationPlaybackService::next(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int& frameIndex, bool& playing, QTimer* timer, QPushButton* playPauseButton) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return false;
    }
    if (!AnimationTestOps::stepNext(effectiveFrames(timelines, selectedTimelineIndex), frameIndex)) {
        return false;
    }
    playing = false;
    timer->stop();
    playPauseButton->setIcon(playIcon());
    return true;
}

bool AnimationPlaybackService::tick(const QVector<AnimationTimeline>& timelines,
                                    int selectedTimelineIndex,
                                    int& frameIndex,
                                    qint64 elapsedMs,
                                    int fps) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return false;
    }
    // How many frame-durations elapsed in real time?
    // Capped at 8 to prevent large jumps after the tab was backgrounded.
    const double msPerFrame = 1000.0 / qMax(1, fps);
    const int count = qBound(1, qRound(static_cast<double>(elapsedMs) / msPerFrame), 8);
    return AnimationTestOps::tick(effectiveFrames(timelines, selectedTimelineIndex), frameIndex, count);
}

bool AnimationPlaybackService::togglePlayPause(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int fps, bool& playing, QTimer* timer, QPushButton* playPauseButton) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return false;
    }
    if (effectiveFrames(timelines, selectedTimelineIndex).isEmpty()) {
        return false;
    }
    playing = !playing;
    playPauseButton->setIcon(playing ? pauseIcon() : playIcon());
    if (playing) {
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
