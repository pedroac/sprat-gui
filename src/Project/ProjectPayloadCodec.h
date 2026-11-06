#pragma once

#include <optional>
#include <QJsonObject>
#include "models.h"

struct ProjectPayloadBuildInput {
    QString currentFolder;
    QString sourceFolder;          // Primary source folder for sprite files (used as base for relative paths)
    QVector<ProjectSource> sources;    // Named sources (serialized as "sources" alongside legacy "smart_folders")
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
    /// Every atlas's laid-out sprites, the active atlas's last (its sprites win).
    QVector<LayoutModel> layoutModels;
    /// Sprite states (keyed like "spritemarkers") of sprites that aren't laid out: written
    /// first, then replaced by the laid-out ones.
    QJsonObject storedSpriteStates;
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
    double exportZoom = 0.0;   // 0 = not set; stored as a ratio (1.0 = 100%)
    QByteArray dockState;
    AppSettings appSettings;
    CliPaths cliPaths;
    SaveConfig saveConfig;
    bool portablePaths = false;  // When true, store relative paths for portable saves
    QStringList orphanedSpritePaths; // Sprite paths recorded as orphaned (no backing file)
    QVector<ExportPreset> exportPresets;
    QVector<MarkerTemplate> markerTemplates;
};

struct ProjectPayloadApplyResult {
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
    double exportZoom = 0.0;   // 0 = not set; stored as a ratio (1.0 = 100%)
    QByteArray dockState;
    AppSettings appSettings;
    CliPaths cliPaths;
    SaveConfig saveConfig;
    QVector<ExportPreset> exportPresets;
    QVector<MarkerTemplate> markerTemplates;
};

class ProjectPayloadCodec {
public:
    static QJsonObject build(const ProjectPayloadBuildInput& input);
    /// @param projectDir Folder holding project.spart.json (the extraction folder for a ZIP).
    ///        Source paths are saved relative to it. Empty: resolve them against currentFolder.
    static ProjectPayloadApplyResult applyToLayout(const QJsonObject& root, const QString& currentFolder,
                                                   QVector<LayoutModel>& layoutModels,
                                                   const QString& projectDir = QString());

    /// A sprite's name, aliases, pivot, markers and nine-slice insets as the project file
    /// stores them in "spritemarkers"/"sprites".
    static QJsonObject spriteStateToJson(const Sprite& sprite);
    /// Applies a state written by spriteStateToJson() (or an older project) to a sprite.
    static void applySpriteState(Sprite& sprite, const QJsonObject& state);
    /// The "spritemarkers"/"sprites" key of a sprite: its path relative to currentFolder.
    static QString spriteStateKey(const QString& currentFolder, const QString& spritePath);
    /// A sprite's state by its relative path, or by its file name as older projects stored it.
    static std::optional<QJsonObject> findSpriteState(const QJsonObject& spriteStates,
                                                      const QString& currentFolder,
                                                      const QString& spritePath);

    /// The payload ProjectSaveService exports one atlas from: only the atlas's sprites in
    /// "spritemarkers" and its own timelines in "animations".
    static QJsonObject atlasExportPayload(const QJsonObject& payload, const QStringList& spritePaths,
                                          const QVector<AnimationTimeline>& timelines,
                                          const QString& currentFolder);
};
