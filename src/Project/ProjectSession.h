#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>
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
    LayoutModel layoutModel;
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

signals:
    void changed();
    void layoutChanged();
    void timelinesChanged();
    void selectionChanged();
};
