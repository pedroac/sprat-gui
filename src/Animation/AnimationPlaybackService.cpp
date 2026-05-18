#include "AnimationPlaybackService.h"

#include <QCoreApplication>
#include <QIcon>
#include <QPushButton>
#include <QTimer>
#include "AnimationTestOps.h"

namespace {
QString trAnimationPlayback(const char* text) {
    return QCoreApplication::translate("AnimationPlaybackService", text);
}

const QIcon& playIcon() {
    static const QIcon icon = QIcon::fromTheme("media-playback-start");
    return icon;
}

const QIcon& pauseIcon() {
    static const QIcon icon = QIcon::fromTheme("media-playback-pause");
    return icon;
}
}

bool AnimationPlaybackService::prev(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int& frameIndex, bool& playing, QTimer* timer, QPushButton* playPauseButton) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return false;
    }
    if (!AnimationTestOps::stepPrev(timelines[selectedTimelineIndex].frames, frameIndex)) {
        return false;
    }
    playing = false;
    timer->stop();
    playPauseButton->setIcon(playIcon());
    playPauseButton->setText("▶");  // Show play symbol
    return true;
}

bool AnimationPlaybackService::next(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int& frameIndex, bool& playing, QTimer* timer, QPushButton* playPauseButton) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return false;
    }
    if (!AnimationTestOps::stepNext(timelines[selectedTimelineIndex].frames, frameIndex)) {
        return false;
    }
    playing = false;
    timer->stop();
    playPauseButton->setIcon(playIcon());
    playPauseButton->setText("▶");  // Show play symbol
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
    return AnimationTestOps::tick(timelines[selectedTimelineIndex].frames, frameIndex, count);
}

bool AnimationPlaybackService::togglePlayPause(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int fps, bool& playing, QTimer* timer, QPushButton* playPauseButton) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return false;
    }
    if (timelines[selectedTimelineIndex].frames.isEmpty()) {
        return false;
    }
    playing = !playing;
    playPauseButton->setIcon(playing ? pauseIcon() : playIcon());
    playPauseButton->setText(playing ? "⏸" : "▶");  // Unicode pause and play symbols as visual feedback
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
