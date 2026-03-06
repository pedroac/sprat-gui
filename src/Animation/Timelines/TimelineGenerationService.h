#pragma once

#include <functional>
#include "models.h"

class TimelineGenerationService {
public:
    enum class ConflictResolution { Replace, Merge, Ignore };
    using ConflictResolver = std::function<ConflictResolution(const QString&)>;

    static bool generateFromLayout(
        const QVector<LayoutModel>& layoutModels,
        QVector<AnimationTimeline>& timelines,
        int& focusIndex,
        const ConflictResolver& resolveConflict,
        QString& statusMessage);
};
