#pragma once

#include <functional>
#include "models.h"

class TimelineGenerationService {
public:
    enum class ConflictResolution { Replace, Merge, Ignore };
    using ConflictResolver = std::function<ConflictResolution(const QString&)>;

    /** Generate timelines from an explicit set of sprites (e.g. a single source). */
    static bool generateFromSprites(
        const QVector<SpritePtr>& sprites,
        QVector<AnimationTimeline>& timelines,
        int& focusIndex,
        const ConflictResolver& resolveConflict,
        QString& statusMessage);

    /** Convenience overload: collects sprites from all layout models. */
    static bool generateFromLayout(
        const QVector<LayoutModel>& layoutModels,
        QVector<AnimationTimeline>& timelines,
        int& focusIndex,
        const ConflictResolver& resolveConflict,
        QString& statusMessage);
};
