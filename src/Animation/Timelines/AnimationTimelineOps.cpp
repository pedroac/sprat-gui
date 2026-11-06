#include "AnimationTimelineOps.h"
#include <QCollator>
#include <algorithm>

bool AnimationTimelineOps::dropFrame(QVector<AnimationTimeline>& timelines, int timelineIndex, const QString& path, int index) {
    if (timelineIndex < 0 || timelineIndex >= timelines.size()) {
        return false;
    }
    auto& frames = timelines[timelineIndex].frames;
    if (index < 0) {
        index = frames.size();
    }
    if (index > frames.size()) {
        index = frames.size();
    }
    frames.insert(index, path);
    return true;
}

bool AnimationTimelineOps::moveFrame(QVector<AnimationTimeline>& timelines, int timelineIndex, int from, int to) {
    if (timelineIndex < 0 || timelineIndex >= timelines.size()) {
        return false;
    }
    auto& frames = timelines[timelineIndex].frames;
    if (from < 0 || from >= frames.size()) {
        return false;
    }
    QString path = frames.takeAt(from);
    if (to > from) {
        to--;
    }
    if (to > frames.size()) {
        to = frames.size();
    }
    if (to < 0) {
        to = 0;
    }
    frames.insert(to, path);
    return true;
}

bool AnimationTimelineOps::duplicateFrame(QVector<AnimationTimeline>& timelines, int timelineIndex, int index) {
    if (timelineIndex < 0 || timelineIndex >= timelines.size()) {
        return false;
    }
    auto& frames = timelines[timelineIndex].frames;
    if (index < 0 || index >= frames.size()) {
        return false;
    }
    frames.insert(index + 1, frames[index]);
    return true;
}

bool AnimationTimelineOps::removeFrames(QVector<AnimationTimeline>& timelines, int timelineIndex, const QVector<int>& rowsDescending) {
    if (timelineIndex < 0 || timelineIndex >= timelines.size()) {
        return false;
    }
    auto& frames = timelines[timelineIndex].frames;
    bool removed = false;
    for (int row : rowsDescending) {
        if (row >= 0 && row < frames.size()) {
            frames.removeAt(row);
            removed = true;
        }
    }
    return removed;
}

QStringList AnimationTimelineOps::naturallySorted(QStringList framePaths) {
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(framePaths.begin(), framePaths.end(),
                     [&collator](const QString& a, const QString& b) { return collator.compare(a, b) < 0; });
    return framePaths;
}
