#pragma once

#include <functional>
#include <QVector>
#include "AnimationModels.h"
#include "LayoutModels.h"

class QWidget;

class AnimationExportService {
public:
    struct ExportCallbacks {
        std::function<void(bool)>                           setLoading;
        std::function<void(const QString&)>                 setStatus;
        std::function<void(const QString&, const QString&)> showError; // title, msg
    };

    static QString chooseOutputPath(QWidget* parent);
    static bool exportAnimation(
        const QVector<AnimationTimeline>& timelines,
        int selectedTimelineIndex,
        const QVector<LayoutModel>& layoutModels,
        int fps,
        const QString& outPath,
        ExportCallbacks callbacks = {});
};
