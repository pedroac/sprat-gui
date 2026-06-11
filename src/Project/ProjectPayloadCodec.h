#pragma once

#include <QJsonObject>
#include "models.h"

struct ProjectPayloadBuildInput {
    QString currentFolder;
    QString sourceFolder;          // Primary source folder for sprite files (used as base for relative paths)
    QVector<SmartFolder> smartFolders; // Smart folders list (saved as "smart_folders" in project JSON)
    QVector<ProjectSource> sources;    // Named sources (new model; serialized as "sources" alongside "smart_folders")
    QString projectDir;            // Directory containing project.spart.json (used to compute portable relative folder path)
    QStringList activeFramePaths;
    bool layoutSourceIsList = false;
    // Multi-atlas support (v4+)
    QVector<AtlasEntry> atlases;
    int activeAtlasIndex = 0;
    // Active-atlas timelines kept for backward-compat serialization in the "animations" section
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
    QStringList orphanedSpritePaths; // Sprite paths recorded as orphaned (no backing file)
};

struct ProjectPayloadApplyResult {
    QVector<SmartFolder> smartFolders; // Smart folders resolved from the project file
    QVector<ProjectSource> sources;    // Named sources resolved from the project file
    QStringList orphanedSpritePaths;   // Sprite paths recorded as orphaned in the project file
    // Multi-atlas support (v4+); empty for v1-3 files (use timelines below instead)
    QVector<AtlasEntry> atlases;
    int activeAtlasIndex = 0;
    // Active-atlas timelines (populated for v1-3 backward compat, and for the active atlas in v4)
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
