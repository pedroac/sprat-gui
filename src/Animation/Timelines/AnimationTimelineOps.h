#pragma once

#include <QVector>
#include "models.h"

class AnimationTimelineOps {
public:
    static bool dropFrame(QVector<AnimationTimeline>& timelines, int timelineIndex, const QString& path, int index);
    static bool moveFrame(QVector<AnimationTimeline>& timelines, int timelineIndex, int from, int to);
    static bool duplicateFrame(QVector<AnimationTimeline>& timelines, int timelineIndex, int index);
    static bool removeFrames(QVector<AnimationTimeline>& timelines, int timelineIndex, const QVector<int>& rowsDescending);
};
