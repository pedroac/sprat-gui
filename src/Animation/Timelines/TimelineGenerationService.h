#pragma once

#include <functional>
#include "models.h"

class TimelineGenerationService {
public:
    enum class ConflictResolution { Replace, Merge, Ignore };
    using ConflictResolver = std::function<ConflictResolution(const QString&)>;

    static bool generateFromLayout(
        const LayoutModel& layoutModel,
        QVector<AnimationTimeline>& timelines,
        int& focusIndex,
        const ConflictResolver& resolveConflict,
        QString& statusMessage);
};
