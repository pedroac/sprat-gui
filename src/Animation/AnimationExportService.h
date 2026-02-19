#pragma once

#include <functional>
#include <QVector>
#include "models.h"

class QWidget;

class AnimationExportService {
public:
    static QString chooseOutputPath(QWidget* parent);
    static bool exportAnimation(
        QWidget* parent,
        const QVector<AnimationTimeline>& timelines,
        int selectedTimelineIndex,
        const LayoutModel& layoutModel,
        int fps,
        const QString& outPath,
        const std::function<void(bool)>& setLoading,
        const std::function<void(const QString&)>& setStatus);
};
