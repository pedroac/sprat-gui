#pragma once

#include <QJsonObject>
#include "models.h"

struct ProjectPayloadBuildInput {
    QString currentFolder;
    QStringList activeFramePaths;
    bool layoutSourceIsList = false;
    QVector<AnimationTimeline> timelines;
    int selectedTimelineIndex = -1;
    QVector<int> selectedTimelineFrameRows;
    int animationFrameIndex = 0;
    bool animationPlaying = false;
    SpritePtr selectedSprite;
    QStringList selectedSpritePaths;
    QString primarySelectedSpritePath;
    QString selectedPointName;
    LayoutModel layoutModel;
    QString layoutOutput;
    double layoutScale = 1.0;
    QString profile;
    int padding = 0;
    bool trimTransparent = false;
    double layoutOptionScale = 1.0;
    int sourceResolutionWidth = 1024;
    int sourceResolutionHeight = 1024;
    double layoutZoom = 1.0;
    double previewZoom = 1.0;
    double animationZoom = 1.0;
    QVector<int> leftSplitterSizes;
    QVector<int> rightSplitterSizes;
    AppSettings appSettings;
    CliPaths cliPaths;
    SaveConfig saveConfig;
};

struct ProjectPayloadApplyResult {
    QVector<AnimationTimeline> timelines;
    int selectedTimelineIndex = -1;
    QVector<int> selectedTimelineFrameRows;
    int animationFrameIndex = 0;
    bool animationPlaying = false;
    QString selectedSpritePath;
    QStringList selectedSpritePaths;
    QString primarySelectedSpritePath;
    QString selectedMarkerName;
    double layoutOptionScale = 1.0;
    int sourceResolutionWidth = 1024;
    int sourceResolutionHeight = 1024;
    double layoutZoom = 1.0;
    double previewZoom = 1.0;
    double animationZoom = 1.0;
    QVector<int> leftSplitterSizes;
    QVector<int> rightSplitterSizes;
    AppSettings appSettings;
    CliPaths cliPaths;
};

class ProjectPayloadCodec {
public:
    static QJsonObject build(const ProjectPayloadBuildInput& input);
    static ProjectPayloadApplyResult applyToLayout(const QJsonObject& root, const QString& currentFolder, LayoutModel& layoutModel);
};
