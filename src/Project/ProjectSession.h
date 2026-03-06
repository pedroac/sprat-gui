#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>
#include <QTemporaryDir>
#include <memory>
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
    QStringList activeFramePaths;
    QString frameListPath; // Temporary file path for frame list

    // --- Layout Model ---
    QVector<LayoutModel> layoutModels;
    QString cachedLayoutOutput;
    double cachedLayoutScale = 1.0;
    QString lastSuccessfulProfile;
    bool lastRunUsedTrim = false;

    // --- Animation Timelines ---
    QVector<AnimationTimeline> timelines;
    int selectedTimelineIndex = -1;

    // --- UI State Selection (Data-side) ---
    SpritePtr selectedSprite;
    QList<SpritePtr> selectedSprites;
    QString selectedPointName;

    // --- Transient State ---
    QJsonObject pendingProjectPayload;

    void clear();
    bool isEmpty() const;

    /**
     * @brief Adds a temporary directory to be managed by the session.
     * 
     * @param dir Pointer to the temporary directory. Ownership is transferred.
     */
    void addTempDir(std::unique_ptr<QTemporaryDir> dir);

    /**
     * @brief Clears all managed temporary directories.
     */
    void clearTempDirs();

signals:
    void changed();
    void layoutChanged();
    void timelinesChanged();
    void selectionChanged();

private:
    std::vector<std::unique_ptr<QTemporaryDir>> m_tempDirs;
};
