#pragma once

#include <QJsonObject>
#include "models.h"

struct ProjectPayloadBuildInput {
    QString currentFolder;
    QString sourceFolder;   // Source folder for sprite files (used as base for relative paths)
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
    QVector<LayoutModel> layoutModels;
    QString layoutOutput;
    double layoutScale = 1.0;
    QString profile;
    int padding = 0;
    bool trimTransparent = false;
    int sourceResolutionWidth = 1024;
    int sourceResolutionHeight = 1024;
    double layoutZoom = 1.0;
    double previewZoom = 1.0;
    double animationZoom = 1.0;
    QByteArray dockState;
    AppSettings appSettings;
    CliPaths cliPaths;
    SaveConfig saveConfig;
    bool portablePaths = false;  // When true, store relative paths for portable saves
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
    int sourceResolutionWidth = 1024;
    int sourceResolutionHeight = 1024;
    double layoutZoom = 1.0;
    double previewZoom = 1.0;
    double animationZoom = 1.0;
    QByteArray dockState;
    AppSettings appSettings;
    CliPaths cliPaths;
    SaveConfig saveConfig;
};

class ProjectPayloadCodec {
public:
    static QJsonObject build(const ProjectPayloadBuildInput& input);
    static ProjectPayloadApplyResult applyToLayout(const QJsonObject& root, const QString& currentFolder, QVector<LayoutModel>& layoutModels);
};
