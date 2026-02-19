#include "TimelineGenerationService.h"

#include "TimelineBuilder.h"

bool TimelineGenerationService::generateFromLayout(
    const LayoutModel& layoutModel,
    QVector<AnimationTimeline>& timelines,
    int& focusIndex,
    const ConflictResolver& resolveConflict,
    QString& statusMessage) {
    QVector<TimelineSeed> generated = TimelineBuilder::buildFromSprites(layoutModel.sprites);
    if (generated.isEmpty()) {
        statusMessage = "No frame names match the expected pattern.";
        return false;
    }

    bool changed = false;
    focusIndex = -1;
    for (const auto& seed : generated) {
        int existingIndex = -1;
        for (int i = 0; i < timelines.size(); ++i) {
            if (timelines[i].name == seed.name) {
                existingIndex = i;
                break;
            }
        }

        if (existingIndex < 0) {
            AnimationTimeline timeline;
            timeline.name = seed.name;
            timeline.frames = seed.frames;
            timelines.append(timeline);
            focusIndex = focusIndex < 0 ? timelines.size() - 1 : focusIndex;
            changed = true;
            continue;
        }

        ConflictResolution resolution = resolveConflict(seed.name);
        if (resolution == ConflictResolution::Ignore) {
            continue;
        }
        if (resolution == ConflictResolution::Replace) {
            timelines[existingIndex].frames = seed.frames;
            focusIndex = focusIndex < 0 ? existingIndex : focusIndex;
            changed = true;
            continue;
        }

        auto& frames = timelines[existingIndex].frames;
        bool appended = false;
        for (const QString& path : seed.frames) {
            if (!frames.contains(path)) {
                frames.append(path);
                appended = true;
            }
        }
        if (appended) {
            focusIndex = focusIndex < 0 ? existingIndex : focusIndex;
            changed = true;
        }
    }

    statusMessage = changed ? "Timelines generated from layout." : "No timelines were created.";
    return changed;
}
