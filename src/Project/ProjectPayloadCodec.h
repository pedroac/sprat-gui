#pragma once

#include <QJsonObject>
#include "models.h"

struct ProjectPayloadBuildInput {
    QString currentFolder;
    QVector<AnimationTimeline> timelines;
    int selectedTimelineIndex = -1;
    SpritePtr selectedSprite;
    QString selectedPointName;
    LayoutModel layoutModel;
    QString layoutOutput;
    double layoutScale = 1.0;
    QString profile;
    int padding = 0;
    bool trimTransparent = false;
    double layoutZoom = 1.0;
    double previewZoom = 1.0;
    double animationZoom = 1.0;
    int animationFps = 8;
    SaveConfig saveConfig;
};

struct ProjectPayloadApplyResult {
    int animationFps = 8;
    QVector<AnimationTimeline> timelines;
};

class ProjectPayloadCodec {
public:
    static QJsonObject build(const ProjectPayloadBuildInput& input);
    static ProjectPayloadApplyResult applyToLayout(const QJsonObject& root, const QString& currentFolder, LayoutModel& layoutModel);
};
