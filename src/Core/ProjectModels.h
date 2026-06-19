#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include "AnimationModels.h"
#include "LayoutModels.h"

/**
 * @enum SourceType
 * @brief Type of a project source.
 */
enum class SourceType {
    Folder,
    SingleImage,
    Archive,
    Url
};

/**
 * @struct ProjectSource
 * @brief A named image source for the project.
 */
struct ProjectSource {
    QString name;
    SourceType type = SourceType::Folder;
    QString originalPath;
    QString cachedFolderPath;
    QStringList excludedFiles;
    QStringList hiddenFolders;
};

/**
 * @struct SmartFolder
 * @brief A source folder whose images are read into the layout automatically.
 */
struct SmartFolder {
    QString path;
    QStringList excludedFiles;
};

/** Per-atlas export overrides. Empty values mean "use the global setting." */
struct AtlasExportConfig {
    QStringList profiles;
    QString     transform;
    QString     scaleFilter;
};

/**
 * @struct AtlasEntry
 * @brief A named atlas within a project, owning its sprites, timelines, and layout pages.
 */
struct AtlasEntry {
    QString id;
    QString name;
    bool isNeutral = false;
    bool isExcluded = false;
    QStringList spritePaths;
    QVector<AnimationTimeline> timelines;
    QVector<LayoutModel> layoutModels;
    QString outputSubdir;
    AtlasExportConfig exportConfig;
};

/** A named export configuration saved within the project. */
struct ExportPreset {
    QString     name;
    QString     outputPath;
    QString     transform;
    QString     scaleFilter;
    QStringList profiles;
    QString     postExportCommand;
};
