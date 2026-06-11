#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QJsonObject>
#include <QUuid>
#include "models.h"

/**
 * @class ProjectSession
 * @brief Encapsulates the state and data models of a project session.
 * 
 * This class separates the project data from the MainWindow UI,
 * allowing for better maintainability and potential for headless processing.
 */
class ProjectSession : public QObject {
    Q_OBJECT
public:
    explicit ProjectSession(QObject* parent = nullptr);

    // --- Project Identity ---
    QString currentFolder;
    QString layoutSourcePath;
    bool layoutSourceIsList = false;
    QString sourceFolder;  // Primary source folder for sync operations (mirrors smartFolders[0].path when non-empty)
    QVector<SmartFolder> smartFolders; // Smart folders — the tool reads from these but never modifies them
    QVector<ProjectSource> sources;   // Named sources (new model, parallel to smartFolders during migration)
    QStringList activeFramePaths;
    QString frameListPath; // Temporary file path for frame list
    QStringList orphanedSpritePaths; // Sprites whose source image is no longer available

    // --- Atlases ---
    QVector<AtlasEntry> atlases;
    int activeAtlasIndex = 0;

    /// Path → SpritePtr for all sprites known to the project.
    /// Populated during image scan; rect/trim fields filled in after layout.
    QHash<QString, SpritePtr> spriteIndex;

    // Per-atlas layout cache (kept at session level for the active atlas)
    QString cachedLayoutOutput;
    double cachedLayoutScale = 1.0;
    QString lastSuccessfulProfile;
    bool lastRunUsedTrim = false;

    // --- Timeline selection (UI state, not per-atlas) ---
    int selectedTimelineIndex = -1;

    AtlasEntry& activeAtlas();
    const AtlasEntry& activeAtlas() const;
    AtlasEntry* atlasById(const QString& id);
    AtlasEntry* atlasForSprite(const QString& path);
    int neutralAtlasIndex() const;
    int excludedAtlasIndex() const;

    // --- UI State Selection (Data-side) ---
    SpritePtr selectedSprite;
    QList<SpritePtr> selectedSprites;
    QString selectedPointName;

    // --- Transient State ---
    QJsonObject pendingProjectPayload;

    void clear();
    bool isEmpty() const;
    void rebuildSpriteIndex();

signals:
    void changed();
    void layoutChanged();
    void timelinesChanged();
    void selectionChanged();
    void atlasesChanged();

};
