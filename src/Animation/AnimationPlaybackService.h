#pragma once

#include <QVector>
#include "models.h"

class QTimer;
class QPushButton;

class AnimationPlaybackService {
public:
    static bool prev(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int& frameIndex, bool& playing, QTimer* timer, QPushButton* playPauseButton);
    static bool next(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int& frameIndex, bool& playing, QTimer* timer, QPushButton* playPauseButton);
    static bool tick(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int& frameIndex);
    static bool togglePlayPause(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int fps, bool& playing, QTimer* timer, QPushButton* playPauseButton);
};
