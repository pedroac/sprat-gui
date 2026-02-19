#include "AnimationPlaybackService.h"

#include <QPushButton>
#include <QTimer>
#include "AnimationTestOps.h"

bool AnimationPlaybackService::prev(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int& frameIndex, bool& playing, QTimer* timer, QPushButton* playPauseButton) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return false;
    }
    if (!AnimationTestOps::stepPrev(timelines[selectedTimelineIndex].frames, frameIndex)) {
        return false;
    }
    playing = false;
    timer->stop();
    playPauseButton->setText("Play");
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
    playPauseButton->setText("Play");
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
    playPauseButton->setText(playing ? "Pause" : "Play");
    if (playing) {
        timer->start(1000 / fps);
    } else {
        timer->stop();
    }
    return true;
}
