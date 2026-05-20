#pragma once
#include <QUndoCommand>
#include <QUndoStack>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMap>
#include <QSet>
#include <QVector>
#include <QStringList>
#include <algorithm>
#include <functional>
#include <optional>
#include "../../Core/models.h"
#include "AnimationPreviewService.h"

// ---------------------------------------------------------------------------
// TrashBin — reversible file deletion via hidden trash folder
// ---------------------------------------------------------------------------
class TrashBin {
public:
    static QString trashRoot(const QString& sf) {
        return QDir(sf).filePath(".sprat-trash");
    }

    // Move filePath into trash, return the trash path (empty on failure)
    static QString send(const QString& filePath, const QString& sourceFolder) {
        QDir srcDir(sourceFolder);
        QString relPath = srcDir.relativeFilePath(filePath);
        if (relPath == ".." || relPath.startsWith("../")) {
            relPath = QFileInfo(filePath).fileName();
        }
        QString trashPath = QDir(trashRoot(sourceFolder)).filePath(relPath);
        QFileInfo trashInfo(trashPath);
        if (!QDir().mkpath(trashInfo.absolutePath())) {
            return {};
        }
        // If a stale trash copy exists, remove it
        if (QFile::exists(trashPath)) {
            QFile::remove(trashPath);
        }
        if (QFile::rename(filePath, trashPath)) {
            return trashPath;
        }

        // Some platforms/filesystems can refuse rename for files that are still
        // being touched by another process. Fall back to copy + remove so the
        // session state cannot drift from what remains on disk.
        if (QFile::copy(filePath, trashPath) && QFile::remove(filePath)) {
            return trashPath;
        }

        QFile::remove(trashPath);
        return {};
    }

    // Move trashPath back to originalPath
    static bool restore(const QString& trashPath, const QString& originalPath) {
        QFileInfo origInfo(originalPath);
        QDir().mkpath(origInfo.absolutePath());
        return QFile::rename(trashPath, originalPath);
    }

    // Delete the entire trash folder for a source folder
    static void purge(const QString& sourceFolder) {
        if (sourceFolder.isEmpty()) return;
        QDir(trashRoot(sourceFolder)).removeRecursively();
    }
};

// ---------------------------------------------------------------------------
// (1001) SetPivotCommand — already existed, kept here
// ---------------------------------------------------------------------------
class SetPivotCommand : public QUndoCommand {
public:
    SetPivotCommand(SpritePtr sprite, int oldX, int oldY, int newX, int newY,
                    bool alreadyApplied = false, QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Set Pivot"), parent)
        , m_sprite(sprite)
        , m_oldX(oldX), m_oldY(oldY), m_newX(newX), m_newY(newY)
        , m_skipFirstRedo(alreadyApplied)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_sprite) {
            m_sprite->pivotX = m_newX;
            m_sprite->pivotY = m_newY;
            AnimationPreviewService::invalidateBounds();
        }
    }

    void undo() override {
        if (m_sprite) {
            m_sprite->pivotX = m_oldX;
            m_sprite->pivotY = m_oldY;
            AnimationPreviewService::invalidateBounds();
        }
    }

    int id() const override { return 1001; }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* o = static_cast<const SetPivotCommand*>(other);
        if (o->m_sprite != m_sprite) return false;
        m_newX = o->m_newX;
        m_newY = o->m_newY;
        return true;
    }

private:
    SpritePtr m_sprite;
    int m_oldX, m_oldY, m_newX, m_newY;
    mutable bool m_skipFirstRedo = false;
};

// ---------------------------------------------------------------------------
// (1002) TimelineFrameDropCommand — drop a frame into a timeline
// ---------------------------------------------------------------------------
class TimelineFrameDropCommand : public QUndoCommand {
public:
    TimelineFrameDropCommand(QVector<AnimationTimeline>* timelines, int timelineIndex,
                              const QString& path, int insertedIndex,
                              std::function<void()> postExecute,
                              QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Drop Frame"), parent)
        , m_timelines(timelines)
        , m_timelineIndex(timelineIndex)
        , m_path(path)
        , m_insertedIndex(insertedIndex)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_timelineIndex < 0 || m_timelineIndex >= m_timelines->size()) return;
        auto& frames = (*m_timelines)[m_timelineIndex].frames;
        int idx = qMin(m_insertedIndex, (int)frames.size());
        frames.insert(idx, m_path);
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        if (m_timelineIndex < 0 || m_timelineIndex >= m_timelines->size()) return;
        auto& frames = (*m_timelines)[m_timelineIndex].frames;
        if (m_insertedIndex >= 0 && m_insertedIndex < frames.size()) {
            frames.removeAt(m_insertedIndex);
        }
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1002; }

private:
    QVector<AnimationTimeline>* m_timelines;
    int m_timelineIndex;
    QString m_path;
    int m_insertedIndex;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1003) TimelineFrameMoveCommand — reorder a frame via drag-and-drop
// ---------------------------------------------------------------------------
class TimelineFrameMoveCommand : public QUndoCommand {
public:
    TimelineFrameMoveCommand(QVector<AnimationTimeline>* timelines, int timelineIndex,
                              int from, int to,
                              const QStringList& savedFramesBefore,
                              std::function<void()> postExecute,
                              QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Move Frame"), parent)
        , m_timelines(timelines)
        , m_timelineIndex(timelineIndex)
        , m_from(from)
        , m_to(to)
        , m_savedFramesBefore(savedFramesBefore)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_timelineIndex < 0 || m_timelineIndex >= m_timelines->size()) return;
        auto& frames = (*m_timelines)[m_timelineIndex].frames;
        if (m_from < 0 || m_from >= frames.size()) return;
        QString path = frames.takeAt(m_from);
        int to = m_to;
        if (to > m_from) to--;
        to = qBound(0, to, (int)frames.size());
        frames.insert(to, path);
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        if (m_timelineIndex < 0 || m_timelineIndex >= m_timelines->size()) return;
        (*m_timelines)[m_timelineIndex].frames = m_savedFramesBefore;
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1003; }

private:
    QVector<AnimationTimeline>* m_timelines;
    int m_timelineIndex;
    int m_from, m_to;
    QStringList m_savedFramesBefore;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1004) TimelineFrameRemoveCommand — remove selected frames from a timeline
// ---------------------------------------------------------------------------
class TimelineFrameRemoveCommand : public QUndoCommand {
public:
    // removed: (index, path) pairs sorted ascending by index
    TimelineFrameRemoveCommand(QVector<AnimationTimeline>* timelines, int timelineIndex,
                                const QVector<QPair<int,QString>>& removed,
                                std::function<void()> postExecute,
                                QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Remove Frame(s)"), parent)
        , m_timelines(timelines)
        , m_timelineIndex(timelineIndex)
        , m_removed(removed)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_timelineIndex < 0 || m_timelineIndex >= m_timelines->size()) return;
        auto& frames = (*m_timelines)[m_timelineIndex].frames;
        // Remove descending to preserve indices
        for (int i = m_removed.size() - 1; i >= 0; --i) {
            int idx = m_removed[i].first;
            if (idx >= 0 && idx < frames.size()) {
                frames.removeAt(idx);
            }
        }
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        if (m_timelineIndex < 0 || m_timelineIndex >= m_timelines->size()) return;
        auto& frames = (*m_timelines)[m_timelineIndex].frames;
        // Re-insert ascending to preserve indices
        for (const auto& pair : m_removed) {
            int idx = qMin(pair.first, (int)frames.size());
            frames.insert(idx, pair.second);
        }
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1004; }

private:
    QVector<AnimationTimeline>* m_timelines;
    int m_timelineIndex;
    QVector<QPair<int,QString>> m_removed;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1005) TimelineFrameDuplicateCommand — duplicate a frame in a timeline
// ---------------------------------------------------------------------------
class TimelineFrameDuplicateCommand : public QUndoCommand {
public:
    TimelineFrameDuplicateCommand(QVector<AnimationTimeline>* timelines, int timelineIndex,
                                   int index, const QString& path,
                                   std::function<void()> postExecute,
                                   QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Duplicate Frame"), parent)
        , m_timelines(timelines)
        , m_timelineIndex(timelineIndex)
        , m_index(index)
        , m_path(path)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_timelineIndex < 0 || m_timelineIndex >= m_timelines->size()) return;
        auto& frames = (*m_timelines)[m_timelineIndex].frames;
        if (m_index < 0 || m_index >= frames.size()) return;
        frames.insert(m_index + 1, m_path);
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        if (m_timelineIndex < 0 || m_timelineIndex >= m_timelines->size()) return;
        auto& frames = (*m_timelines)[m_timelineIndex].frames;
        int dupIdx = m_index + 1;
        if (dupIdx >= 0 && dupIdx < frames.size()) {
            frames.removeAt(dupIdx);
        }
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1005; }

private:
    QVector<AnimationTimeline>* m_timelines;
    int m_timelineIndex;
    int m_index;
    QString m_path;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1006) TimelineAddCommand — add a new timeline
// ---------------------------------------------------------------------------
class TimelineAddCommand : public QUndoCommand {
public:
    TimelineAddCommand(QVector<AnimationTimeline>* timelines,
                        const AnimationTimeline& timeline,
                        int* selectedTimelineIndex,
                        std::function<void()> postExecute,
                        QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Add Timeline"), parent)
        , m_timelines(timelines)
        , m_timeline(timeline)
        , m_selectedTimelineIndex(selectedTimelineIndex)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {
        m_addedIndex = m_timelines->size() - 1;
    }

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        m_addedIndex = m_timelines->size();
        m_timelines->append(m_timeline);
        *m_selectedTimelineIndex = m_timelines->size() - 1;
        if (m_postExecute) {
            // Defer UI refresh to ensure it runs in the event loop after the command is fully processed
            QMetaObject::invokeMethod(QApplication::instance(), m_postExecute, Qt::QueuedConnection);
        }
    }

    void undo() override {
        if (m_addedIndex >= 0 && m_addedIndex < m_timelines->size()) {
            m_timelines->removeAt(m_addedIndex);
        }
        *m_selectedTimelineIndex = qMax(-1, m_timelines->size() - 1);
        if (m_postExecute) {
            // Defer UI refresh to ensure it runs in the event loop after the command is fully processed
            QMetaObject::invokeMethod(QApplication::instance(), m_postExecute, Qt::QueuedConnection);
        }
    }

    int id() const override { return 1006; }

private:
    QVector<AnimationTimeline>* m_timelines;
    AnimationTimeline m_timeline;
    int* m_selectedTimelineIndex;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
    int m_addedIndex = -1; // Added for precise undo
};

// ---------------------------------------------------------------------------
// (1007) TimelinesUpdateCommand — bulk update timelines state
// ---------------------------------------------------------------------------
class TimelinesUpdateCommand : public QUndoCommand {
public:
    TimelinesUpdateCommand(QVector<AnimationTimeline>* timelines,
                          const QVector<AnimationTimeline>& oldState,
                          const QVector<AnimationTimeline>& newState,
                          int oldSelection,
                          int newSelection,
                          int* selectedIndexPtr,
                          std::function<void()> postExecute,
                          const QString& text = QObject::tr("Update Timelines"),
                          QUndoCommand* parent = nullptr)
        : QUndoCommand(text, parent)
        , m_timelines(timelines)
        , m_oldState(oldState)
        , m_newState(newState)
        , m_oldSelection(oldSelection)
        , m_newSelection(newSelection)
        , m_selectedIndexPtr(selectedIndexPtr)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        *m_timelines = m_newState;
        *m_selectedIndexPtr = m_newSelection;
        if (m_postExecute) {
            QMetaObject::invokeMethod(QApplication::instance(), m_postExecute, Qt::QueuedConnection);
        }
    }

    void undo() override {
        *m_timelines = m_oldState;
        *m_selectedIndexPtr = m_oldSelection;
        if (m_postExecute) {
            QMetaObject::invokeMethod(QApplication::instance(), m_postExecute, Qt::QueuedConnection);
        }
    }

    int id() const override { return 1007; }

private:
    QVector<AnimationTimeline>* m_timelines;
    QVector<AnimationTimeline> m_oldState;
    QVector<AnimationTimeline> m_newState;
    int m_oldSelection;
    int m_newSelection;
    int* m_selectedIndexPtr;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1008) TimelineRemoveCommand — remove the selected timeline
// ---------------------------------------------------------------------------
class TimelineRemoveCommand : public QUndoCommand {
public:
    TimelineRemoveCommand(QVector<AnimationTimeline>* timelines,
                           int index,
                           const AnimationTimeline& savedTimeline,
                           int* selectedTimelineIndex,
                           std::function<void()> postExecute,
                           QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Remove Timeline"), parent)
        , m_timelines(timelines)
        , m_index(index)
        , m_savedTimeline(savedTimeline)
        , m_selectedTimelineIndex(selectedTimelineIndex)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_index >= 0 && m_index < m_timelines->size()) {
            m_timelines->removeAt(m_index);
        }
        *m_selectedTimelineIndex = qMin(m_index, m_timelines->size() - 1);
        if (m_postExecute) {
            QMetaObject::invokeMethod(QApplication::instance(), m_postExecute, Qt::QueuedConnection);
        }
    }

    void undo() override {
        int insertIdx = qMin(m_index, (int)m_timelines->size());
        m_timelines->insert(insertIdx, m_savedTimeline);
        *m_selectedTimelineIndex = insertIdx;
        if (m_postExecute) {
            QMetaObject::invokeMethod(QApplication::instance(), m_postExecute, Qt::QueuedConnection);
        }
    }

    int id() const override { return 1008; }

private:
    QVector<AnimationTimeline>* m_timelines;
    int m_index;
    AnimationTimeline m_savedTimeline;
    int* m_selectedTimelineIndex;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1008) SetMarkersCommand — edit sprite markers via dialog
// ---------------------------------------------------------------------------
class SetMarkersCommand : public QUndoCommand {
public:
    SetMarkersCommand(SpritePtr sprite,
                       const QVector<NamedPoint>& oldPoints,
                       const QVector<NamedPoint>& newPoints,
                       std::function<void()> postExecute,
                       QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Edit Markers"), parent)
        , m_sprite(sprite)
        , m_oldPoints(oldPoints)
        , m_newPoints(newPoints)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_sprite) m_sprite->points = m_newPoints;
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        if (m_sprite) m_sprite->points = m_oldPoints;
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1009; }

private:
    SpritePtr m_sprite;
    QVector<NamedPoint> m_oldPoints;
    QVector<NamedPoint> m_newPoints;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1009) ApplyPivotToFramesCommand — copy pivot from one sprite to many
// ---------------------------------------------------------------------------
class ApplyPivotToFramesCommand : public QUndoCommand {
public:
    // targets: (sprite, (oldX, oldY))
    ApplyPivotToFramesCommand(const QVector<QPair<SpritePtr, QPair<int,int>>>& targets,
                               int newX, int newY,
                               std::function<void()> postExecute,
                               QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Apply Pivot to Frames"), parent)
        , m_targets(targets)
        , m_newX(newX)
        , m_newY(newY)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        for (const auto& pair : m_targets) {
            if (pair.first) {
                pair.first->pivotX = m_newX;
                pair.first->pivotY = m_newY;
            }
        }
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        for (const auto& pair : m_targets) {
            if (pair.first) {
                pair.first->pivotX = pair.second.first;
                pair.first->pivotY = pair.second.second;
            }
        }
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1010; }

private:
    QVector<QPair<SpritePtr, QPair<int,int>>> m_targets;
    int m_newX, m_newY;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1010) ApplyMarkerToFramesCommand — copy a marker from one sprite to many
// ---------------------------------------------------------------------------
class ApplyMarkerToFramesCommand : public QUndoCommand {
public:
    // targets: (sprite, old marker at that name — nullopt if it didn't exist)
    ApplyMarkerToFramesCommand(const QVector<QPair<SpritePtr, std::optional<NamedPoint>>>& targets,
                                const NamedPoint& newMarker,
                                std::function<void()> postExecute,
                                QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Apply Marker to Frames"), parent)
        , m_targets(targets)
        , m_newMarker(newMarker)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        for (const auto& pair : m_targets) {
            SpritePtr sprite = pair.first;
            if (!sprite) continue;
            auto it = std::find_if(sprite->points.begin(), sprite->points.end(),
                [this](const NamedPoint& p){ return p.name == m_newMarker.name; });
            if (it == sprite->points.end()) {
                sprite->points.append(m_newMarker);
            } else {
                *it = m_newMarker;
            }
        }
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        for (const auto& pair : m_targets) {
            SpritePtr sprite = pair.first;
            if (!sprite) continue;
            auto it = std::find_if(sprite->points.begin(), sprite->points.end(),
                [this](const NamedPoint& p){ return p.name == m_newMarker.name; });
            if (pair.second.has_value()) {
                // Restore old marker
                if (it != sprite->points.end()) {
                    *it = pair.second.value();
                } else {
                    sprite->points.append(pair.second.value());
                }
            } else {
                // Was not present — remove it
                if (it != sprite->points.end()) {
                    sprite->points.erase(it);
                }
            }
        }
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1011; }

private:
    QVector<QPair<SpritePtr, std::optional<NamedPoint>>> m_targets;
    NamedPoint m_newMarker;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1011) CreateGroupCommand — move files into a new subfolder
// ---------------------------------------------------------------------------
class CreateGroupCommand : public QUndoCommand {
public:
    // moves: (oldPath, newPath) pairs; savedActivePaths = before; newActivePaths = after
    CreateGroupCommand(QStringList* activeFramePaths,
                        const QVector<QPair<QString,QString>>& moves,
                        const QString& targetDir,
                        const QStringList& savedActivePaths,
                        const QStringList& newActivePaths,
                        std::function<bool()> ensureFrameList,
                        std::function<void()> postExecute,
                        QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Create Group"), parent)
        , m_activeFramePaths(activeFramePaths)
        , m_moves(moves)
        , m_targetDir(targetDir)
        , m_savedActivePaths(savedActivePaths)
        , m_newActivePaths(newActivePaths)
        , m_ensureFrameList(std::move(ensureFrameList))
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        // Re-create target directory
        QDir().mkpath(m_targetDir);
        // Move files old → new
        for (const auto& pair : m_moves) {
            QFile::rename(pair.first, pair.second);
        }
        *m_activeFramePaths = m_newActivePaths;
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        // Move files new → old
        for (const auto& pair : m_moves) {
            QFileInfo fi(pair.first);
            QDir().mkpath(fi.absolutePath());
            QFile::rename(pair.second, pair.first);
        }
        *m_activeFramePaths = m_savedActivePaths;
        // Try to remove the now-empty group directory
        QDir().rmdir(m_targetDir);
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1012; }

private:
    QStringList* m_activeFramePaths;
    QVector<QPair<QString,QString>> m_moves;
    QString m_targetDir;
    QStringList m_savedActivePaths;
    QStringList m_newActivePaths;
    std::function<bool()> m_ensureFrameList;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1012) UngroupCommand — move files up one level, remove subfolder
// ---------------------------------------------------------------------------
class UngroupCommand : public QUndoCommand {
public:
    // moves: (oldPath, newPath) pairs; removedDirs: directories to re-create on undo
    UngroupCommand(QStringList* activeFramePaths,
                    const QVector<QPair<QString,QString>>& moves,
                    const QSet<QString>& removedDirs,
                    const QStringList& savedActivePaths,
                    const QStringList& newActivePaths,
                    std::function<bool()> ensureFrameList,
                    std::function<void()> postExecute,
                    QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Ungroup"), parent)
        , m_activeFramePaths(activeFramePaths)
        , m_moves(moves)
        , m_removedDirs(removedDirs)
        , m_savedActivePaths(savedActivePaths)
        , m_newActivePaths(newActivePaths)
        , m_ensureFrameList(std::move(ensureFrameList))
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        // Move files old → new (up one level)
        for (const auto& pair : m_moves) {
            QFileInfo fi(pair.second);
            QDir().mkpath(fi.absolutePath());
            QFile::rename(pair.first, pair.second);
        }
        *m_activeFramePaths = m_newActivePaths;
        // Remove now-empty directories
        for (const QString& d : m_removedDirs) {
            QDir().rmdir(d);
        }
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        // Re-create removed directories
        for (const QString& d : m_removedDirs) {
            QDir().mkpath(d);
        }
        // Move files new → old
        for (const auto& pair : m_moves) {
            QFileInfo fi(pair.first);
            QDir().mkpath(fi.absolutePath());
            QFile::rename(pair.second, pair.first);
        }
        *m_activeFramePaths = m_savedActivePaths;
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1013; }

private:
    QStringList* m_activeFramePaths;
    QVector<QPair<QString,QString>> m_moves;
    QSet<QString> m_removedDirs;
    QStringList m_savedActivePaths;
    QStringList m_newActivePaths;
    std::function<bool()> m_ensureFrameList;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1013) RemoveSpritesCommand — move sprites to trash (reversible deletion)
// ---------------------------------------------------------------------------
class RemoveSpritesCommand : public QUndoCommand {
public:
    RemoveSpritesCommand(QStringList* activeFramePaths,
                          QVector<AnimationTimeline>* timelines,
                          int* selectedTimelineIndex,
                          QVector<LayoutModel>* layoutModels,
                          const QString& sourceFolder,
                          const QStringList& targets,
                          const QStringList& savedActivePaths,
                          const QVector<AnimationTimeline>& savedTimelines,
                          int savedTimelineIdx,
                          const QVector<LayoutModel>& savedLayoutModels,
                          std::function<bool()> ensureFrameList,
                          std::function<void()> postExecuteRedo,
                          std::function<void()> postExecuteUndo,
                          QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Remove Sprite(s)"), parent)
        , m_activeFramePaths(activeFramePaths)
        , m_timelines(timelines)
        , m_selectedTimelineIndex(selectedTimelineIndex)
        , m_layoutModels(layoutModels)
        , m_sourceFolder(sourceFolder)
        , m_targets(targets)
        , m_savedActivePaths(savedActivePaths)
        , m_savedTimelines(savedTimelines)
        , m_savedTimelineIdx(savedTimelineIdx)
        , m_savedLayoutModels(savedLayoutModels)
        , m_ensureFrameList(std::move(ensureFrameList))
        , m_postExecuteRedo(std::move(postExecuteRedo))
        , m_postExecuteUndo(std::move(postExecuteUndo))
    {}

    void redo() override {
        m_trashMap.clear();
        QStringList removedTargets;
        removedTargets.reserve(m_targets.size());

        // Move files to trash
        for (const QString& path : m_targets) {
            QString trashPath = TrashBin::send(path, m_sourceFolder);
            if (!trashPath.isEmpty()) {
                m_trashMap[path] = trashPath;
                removedTargets.append(path);
            } else if (!QFileInfo::exists(path)) {
                removedTargets.append(path);
            } else {
                qWarning() << "RemoveSpritesCommand: failed to remove source file, keeping sprite in layout:" << path;
            }
        }
        if (removedTargets.isEmpty()) {
            return;
        }

        // Remove from activeFramePaths
        for (const QString& path : removedTargets) {
            m_activeFramePaths->removeAll(path);
        }
        // Remove from timelines
        const QSet<QString> targetSet(removedTargets.begin(), removedTargets.end());
        for (auto& timeline : *m_timelines) {
            for (int i = timeline.frames.size() - 1; i >= 0; --i) {
                if (targetSet.contains(timeline.frames[i])) {
                    timeline.frames.removeAt(i);
                }
            }
        }
        // Remove empty timelines and fix selectedTimelineIndex
        for (int i = m_timelines->size() - 1; i >= 0; --i) {
            if ((*m_timelines)[i].frames.isEmpty()) {
                m_timelines->removeAt(i);
                if (*m_selectedTimelineIndex > i) {
                    --(*m_selectedTimelineIndex);
                } else if (*m_selectedTimelineIndex == i) {
                    *m_selectedTimelineIndex = -1;
                }
            }
        }
        // Remove from layoutModels
        for (auto& model : *m_layoutModels) {
            model.sprites.erase(
                std::remove_if(model.sprites.begin(), model.sprites.end(),
                    [&targetSet](const SpritePtr& s) {
                        return s && targetSet.contains(s->path);
                    }),
                model.sprites.end());
        }
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecuteRedo) m_postExecuteRedo();
    }

    void undo() override {
        // Restore files from trash
        for (auto it = m_trashMap.constBegin(); it != m_trashMap.constEnd(); ++it) {
            TrashBin::restore(it.value(), it.key());
        }
        // Restore session data
        *m_activeFramePaths = m_savedActivePaths;
        *m_timelines = m_savedTimelines;
        *m_selectedTimelineIndex = m_savedTimelineIdx;
        *m_layoutModels = m_savedLayoutModels;
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecuteUndo) m_postExecuteUndo();
    }

    int id() const override { return 1014; }

private:
    QStringList* m_activeFramePaths;
    QVector<AnimationTimeline>* m_timelines;
    int* m_selectedTimelineIndex;
    QVector<LayoutModel>* m_layoutModels;
    QString m_sourceFolder;
    QStringList m_targets;
    QStringList m_savedActivePaths;
    QVector<AnimationTimeline> m_savedTimelines;
    int m_savedTimelineIdx;
    QVector<LayoutModel> m_savedLayoutModels;
    std::function<bool()> m_ensureFrameList;
    std::function<void()> m_postExecuteRedo;
    std::function<void()> m_postExecuteUndo;
    QMap<QString, QString> m_trashMap; // originalPath → trashPath
};

// ---------------------------------------------------------------------------
// (1014) SplitSpriteCommand — split one sprite file into two
// ---------------------------------------------------------------------------
class SplitSpriteCommand : public QUndoCommand {
public:
    SplitSpriteCommand(QStringList* activeFramePaths,
                        const QString& originalPath,
                        const QString& pathA,
                        const QString& pathB,
                        int insertedIdx,
                        Qt::Orientation orientation,
                        int localPos,
                        bool rotated,
                        const QRect& trimRect,
                        std::function<bool()> ensureFrameList,
                        std::function<void()> postExecute,
                        QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Split Sprite"), parent)
        , m_activeFramePaths(activeFramePaths)
        , m_originalPath(originalPath)
        , m_pathA(pathA)
        , m_pathB(pathB)
        , m_insertedIdx(insertedIdx)
        , m_orientation(orientation)
        , m_localPos(localPos)
        , m_rotated(rotated)
        , m_trimRect(trimRect)
        , m_ensureFrameList(std::move(ensureFrameList))
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        // Re-split the original image
        QImage originalImage(m_originalPath);
        if (originalImage.isNull()) return;

        int l = m_trimRect.x();
        int t = m_trimRect.y();
        int trimmedH = originalImage.height() - t - m_trimRect.height();

        QImage imgA, imgB;
        if (!m_rotated) {
            if (m_orientation == Qt::Horizontal) {
                int origY = qBound(1, m_localPos + t, originalImage.height() - 1);
                imgA = originalImage.copy(0, 0, originalImage.width(), origY);
                imgB = originalImage.copy(0, origY, originalImage.width(), originalImage.height() - origY);
            } else {
                int origX = qBound(1, m_localPos + l, originalImage.width() - 1);
                imgA = originalImage.copy(0, 0, origX, originalImage.height());
                imgB = originalImage.copy(origX, 0, originalImage.width() - origX, originalImage.height());
            }
        } else {
            if (m_orientation == Qt::Horizontal) {
                int origX = qBound(1, m_localPos + l, originalImage.width() - 1);
                imgA = originalImage.copy(0, 0, origX, originalImage.height());
                imgB = originalImage.copy(origX, 0, originalImage.width() - origX, originalImage.height());
            } else {
                int origY = qBound(1, (trimmedH - 1 - m_localPos) + t, originalImage.height() - 1);
                imgA = originalImage.copy(0, 0, originalImage.width(), origY);
                imgB = originalImage.copy(0, origY, originalImage.width(), originalImage.height() - origY);
            }
        }
        if (imgA.isNull() || imgB.isNull()) return;
        imgA.save(m_pathA);
        imgB.save(m_pathB);

        // Update activeFramePaths: remove original, insert A+B at insertedIdx
        m_activeFramePaths->removeAll(m_originalPath);
        int idx = qMin(m_insertedIdx, (int)m_activeFramePaths->size());
        m_activeFramePaths->insert(idx, m_pathB);
        m_activeFramePaths->insert(idx, m_pathA);

        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        // Remove split files
        QFile::remove(m_pathA);
        QFile::remove(m_pathB);
        // Restore original path in activeFramePaths
        m_activeFramePaths->removeAll(m_pathA);
        m_activeFramePaths->removeAll(m_pathB);
        int idx = qMin(m_insertedIdx, (int)m_activeFramePaths->size());
        m_activeFramePaths->insert(idx, m_originalPath);
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1015; }

private:
    QStringList* m_activeFramePaths;
    QString m_originalPath;
    QString m_pathA;
    QString m_pathB;
    int m_insertedIdx;
    Qt::Orientation m_orientation;
    int m_localPos;
    bool m_rotated;
    QRect m_trimRect;
    std::function<bool()> m_ensureFrameList;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1015) SetTimelineNameCommand — rename a timeline
// ---------------------------------------------------------------------------
class SetTimelineNameCommand : public QUndoCommand {
public:
    SetTimelineNameCommand(QVector<AnimationTimeline>* timelines, int index,
                            const QString& oldName, const QString& newName,
                            std::function<void()> postExecute,
                            QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Rename Timeline"), parent)
        , m_timelines(timelines), m_index(index)
        , m_oldName(oldName), m_newName(newName)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_index >= 0 && m_index < m_timelines->size())
            (*m_timelines)[m_index].name = m_newName;
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        if (m_index >= 0 && m_index < m_timelines->size())
            (*m_timelines)[m_index].name = m_oldName;
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1016; }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* o = static_cast<const SetTimelineNameCommand*>(other);
        if (o->m_timelines != m_timelines || o->m_index != m_index) return false;
        m_newName = o->m_newName;
        return true;
    }

private:
    QVector<AnimationTimeline>* m_timelines;
    int m_index;
    QString m_oldName, m_newName;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1016) SetTimelineFpsCommand — change a timeline's FPS
// ---------------------------------------------------------------------------
class SetTimelineFpsCommand : public QUndoCommand {
public:
    SetTimelineFpsCommand(QVector<AnimationTimeline>* timelines, int index,
                           int oldFps, int newFps,
                           std::function<void()> postExecute,
                           QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Change Timeline FPS"), parent)
        , m_timelines(timelines), m_index(index)
        , m_oldFps(oldFps), m_newFps(newFps)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_index >= 0 && m_index < m_timelines->size())
            (*m_timelines)[m_index].fps = m_newFps;
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        if (m_index >= 0 && m_index < m_timelines->size())
            (*m_timelines)[m_index].fps = m_oldFps;
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1017; }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* o = static_cast<const SetTimelineFpsCommand*>(other);
        if (o->m_timelines != m_timelines || o->m_index != m_index) return false;
        m_newFps = o->m_newFps;
        return true;
    }

private:
    QVector<AnimationTimeline>* m_timelines;
    int m_index;
    int m_oldFps, m_newFps;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};
