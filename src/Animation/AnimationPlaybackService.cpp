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

bool AnimationPlaybackService::tick(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int& frameIndex) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return false;
    }
    return AnimationTestOps::tick(timelines[selectedTimelineIndex].frames, frameIndex);
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
        timer->start(qRound(1000.0 / fps));
    } else {
        timer->stop();
    }
    return true;
}
