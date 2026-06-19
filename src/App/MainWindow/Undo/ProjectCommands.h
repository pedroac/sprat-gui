#pragma once
#include <QUndoCommand>
#include <QStringList>
#include <QVector>
#include <QPair>
#include <QSet>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <functional>
#include <algorithm>
#include "../../../Core/models.h"
#include "TrashBin.h"

// ---------------------------------------------------------------------------
// (1011) CreateGroupCommand
// ---------------------------------------------------------------------------
class CreateGroupCommand : public QUndoCommand {
public:
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
        QDir().mkpath(m_targetDir);
        for (const auto& pair : m_moves) {
            QFile::rename(pair.first, pair.second);
        }
        *m_activeFramePaths = m_newActivePaths;
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        for (const auto& pair : m_moves) {
            QFileInfo fi(pair.first);
            QDir().mkpath(fi.absolutePath());
            QFile::rename(pair.second, pair.first);
        }
        *m_activeFramePaths = m_savedActivePaths;
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
// (1012) UngroupCommand
// ---------------------------------------------------------------------------
class UngroupCommand : public QUndoCommand {
public:
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
        for (const auto& pair : m_moves) {
            QFileInfo fi(pair.second);
            QDir().mkpath(fi.absolutePath());
            QFile::rename(pair.first, pair.second);
        }
        *m_activeFramePaths = m_newActivePaths;
        QStringList sortedDirs(m_removedDirs.begin(), m_removedDirs.end());
        std::sort(sortedDirs.begin(), sortedDirs.end(),
                  [](const QString& a, const QString& b){ return a.length() > b.length(); });
        for (const QString& d : sortedDirs)
            QDir().rmdir(d);
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        for (const QString& d : m_removedDirs) {
            QDir().mkpath(d);
        }
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
// (1025) MoveItemCommand
// ---------------------------------------------------------------------------
class MoveItemCommand : public QUndoCommand {
public:
    MoveItemCommand(QStringList* activeFramePaths,
                    const QString& oldPath,
                    const QString& newPath,
                    const QStringList& savedActivePaths,
                    const QStringList& newActivePaths,
                    std::function<bool()> ensureFrameList,
                    std::function<void()> postExecute,
                    const QString& label,
                    QUndoCommand* parent = nullptr)
        : QUndoCommand(label, parent)
        , m_activeFramePaths(activeFramePaths)
        , m_oldPath(oldPath)
        , m_newPath(newPath)
        , m_savedActivePaths(savedActivePaths)
        , m_newActivePaths(newActivePaths)
        , m_ensureFrameList(std::move(ensureFrameList))
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        QFile::rename(m_oldPath, m_newPath);
        *m_activeFramePaths = m_newActivePaths;
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        QFile::rename(m_newPath, m_oldPath);
        *m_activeFramePaths = m_savedActivePaths;
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1025; }

private:
    QStringList* m_activeFramePaths;
    QString m_oldPath, m_newPath;
    QStringList m_savedActivePaths, m_newActivePaths;
    std::function<bool()> m_ensureFrameList;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1024) RemoveSourceCommand
// ---------------------------------------------------------------------------
class RemoveSourceCommand : public QUndoCommand {
public:
    RemoveSourceCommand(
        QVector<ProjectSource>* sources,
        QVector<SmartFolder>* smartFolders,
        QStringList* activeFramePaths,
        QVector<AnimationTimeline>* timelines,
        int* selectedTimelineIndex,
        QVector<LayoutModel>* layoutModels,
        int sourceIndex,
        const ProjectSource& removedSource,
        const SmartFolder& removedSmartFolder,
        bool hasSmartFolder,
        const QStringList& spritesToRemove,
        const QString& trashPath,
        const QVector<ProjectSource>& savedSources,
        const QVector<SmartFolder>& savedSmartFolders,
        const QStringList& savedActivePaths,
        const QVector<AnimationTimeline>& savedTimelines,
        int savedTimelineIdx,
        const QVector<LayoutModel>& savedLayoutModels,
        std::function<bool()> ensureFrameList,
        std::function<void()> postExecuteRedo,
        std::function<void()> postExecuteUndo,
        QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Remove Source"), parent)
        , m_sources(sources)
        , m_smartFolders(smartFolders)
        , m_activeFramePaths(activeFramePaths)
        , m_timelines(timelines)
        , m_selectedTimelineIndex(selectedTimelineIndex)
        , m_layoutModels(layoutModels)
        , m_sourceIndex(sourceIndex)
        , m_removedSource(removedSource)
        , m_removedSmartFolder(removedSmartFolder)
        , m_hasSmartFolder(hasSmartFolder)
        , m_spritesToRemove(spritesToRemove)
        , m_trashPath(trashPath)
        , m_savedSources(savedSources)
        , m_savedSmartFolders(savedSmartFolders)
        , m_savedActivePaths(savedActivePaths)
        , m_savedTimelines(savedTimelines)
        , m_savedTimelineIdx(savedTimelineIdx)
        , m_savedLayoutModels(savedLayoutModels)
        , m_ensureFrameList(std::move(ensureFrameList))
        , m_postExecuteRedo(std::move(postExecuteRedo))
        , m_postExecuteUndo(std::move(postExecuteUndo))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }

        if (m_sourceIndex >= 0 && m_sourceIndex < m_sources->size())
            m_sources->removeAt(m_sourceIndex);
        if (m_hasSmartFolder && m_sourceIndex >= 0 && m_sourceIndex < m_smartFolders->size())
            m_smartFolders->removeAt(m_sourceIndex);

        const QSet<QString> targetSet(m_spritesToRemove.begin(), m_spritesToRemove.end());
        for (const QString& p : m_spritesToRemove)
            m_activeFramePaths->removeAll(p);

        for (auto& timeline : *m_timelines) {
            for (int i = timeline.frames.size() - 1; i >= 0; --i) {
                if (targetSet.contains(timeline.frames[i]))
                    timeline.frames.removeAt(i);
            }
        }
        for (int i = m_timelines->size() - 1; i >= 0; --i) {
            if ((*m_timelines)[i].frames.isEmpty()) {
                m_timelines->removeAt(i);
                if (*m_selectedTimelineIndex > i) --(*m_selectedTimelineIndex);
                else if (*m_selectedTimelineIndex == i) *m_selectedTimelineIndex = -1;
            }
        }

        for (auto& model : *m_layoutModels) {
            model.sprites.erase(
                std::remove_if(model.sprites.begin(), model.sprites.end(),
                    [&targetSet](const SpritePtr& s) {
                        return s && targetSet.contains(s->path);
                    }),
                model.sprites.end());
        }

        if (!m_removedSource.cachedFolderPath.isEmpty()
                && QDir(m_removedSource.cachedFolderPath).exists()) {
            if (!m_trashPath.isEmpty() && !QDir(m_trashPath).exists()) {
                QFile::rename(m_removedSource.cachedFolderPath, m_trashPath);
            } else {
                m_trashPath = TrashBin::sendFolder(m_removedSource.cachedFolderPath);
            }
        }

        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecuteRedo) m_postExecuteRedo();
    }

    void undo() override {
        if (!m_trashPath.isEmpty() && !m_removedSource.cachedFolderPath.isEmpty())
            TrashBin::restoreFolder(m_trashPath, m_removedSource.cachedFolderPath);

        *m_sources = m_savedSources;
        *m_smartFolders = m_savedSmartFolders;
        *m_activeFramePaths = m_savedActivePaths;
        *m_timelines = m_savedTimelines;
        *m_selectedTimelineIndex = m_savedTimelineIdx;
        *m_layoutModels = m_savedLayoutModels;

        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecuteUndo) m_postExecuteUndo();
    }

    int id() const override { return 1024; }

private:
    QVector<ProjectSource>* m_sources;
    QVector<SmartFolder>* m_smartFolders;
    QStringList* m_activeFramePaths;
    QVector<AnimationTimeline>* m_timelines;
    int* m_selectedTimelineIndex;
    QVector<LayoutModel>* m_layoutModels;
    int m_sourceIndex;
    ProjectSource m_removedSource;
    SmartFolder m_removedSmartFolder;
    bool m_hasSmartFolder;
    QStringList m_spritesToRemove;
    mutable QString m_trashPath;
    QVector<ProjectSource> m_savedSources;
    QVector<SmartFolder> m_savedSmartFolders;
    QStringList m_savedActivePaths;
    QVector<AnimationTimeline> m_savedTimelines;
    int m_savedTimelineIdx;
    QVector<LayoutModel> m_savedLayoutModels;
    std::function<bool()> m_ensureFrameList;
    std::function<void()> m_postExecuteRedo;
    std::function<void()> m_postExecuteUndo;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1021) ExcludeSpriteCommand
// ---------------------------------------------------------------------------
class ExcludeSpriteCommand : public QUndoCommand {
public:
    ExcludeSpriteCommand(QVector<SmartFolder>* smartFolders,
                          int folderIndex,
                          const QString& relPath,
                          QStringList* activeFramePaths,
                          QVector<AnimationTimeline>* timelines,
                          int* selectedTimelineIndex,
                          QVector<LayoutModel>* layoutModels,
                          const QString& absolutePath,
                          const QStringList& savedActivePaths,
                          const QVector<AnimationTimeline>& savedTimelines,
                          int savedTimelineIdx,
                          const QVector<LayoutModel>& savedLayoutModels,
                          std::function<bool()> ensureFrameList,
                          std::function<void()> postExecuteRedo,
                          std::function<void()> postExecuteUndo,
                          QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Exclude from Layout"), parent)
        , m_smartFolders(smartFolders)
        , m_folderIndex(folderIndex)
        , m_relPath(relPath)
        , m_activeFramePaths(activeFramePaths)
        , m_timelines(timelines)
        , m_selectedTimelineIndex(selectedTimelineIndex)
        , m_layoutModels(layoutModels)
        , m_absolutePath(absolutePath)
        , m_savedActivePaths(savedActivePaths)
        , m_savedTimelines(savedTimelines)
        , m_savedTimelineIdx(savedTimelineIdx)
        , m_savedLayoutModels(savedLayoutModels)
        , m_ensureFrameList(std::move(ensureFrameList))
        , m_postExecuteRedo(std::move(postExecuteRedo))
        , m_postExecuteUndo(std::move(postExecuteUndo))
    {}

    void redo() override {
        if (m_folderIndex >= 0 && m_folderIndex < m_smartFolders->size()) {
            auto& sf = (*m_smartFolders)[m_folderIndex];
            if (!sf.excludedFiles.contains(m_relPath)) {
                sf.excludedFiles.append(m_relPath);
            }
        }
        m_activeFramePaths->removeAll(m_absolutePath);
        const QSet<QString> targetSet = {m_absolutePath};
        for (auto& timeline : *m_timelines) {
            for (int i = timeline.frames.size() - 1; i >= 0; --i) {
                if (targetSet.contains(timeline.frames[i])) timeline.frames.removeAt(i);
            }
        }
        for (int i = m_timelines->size() - 1; i >= 0; --i) {
            if ((*m_timelines)[i].frames.isEmpty()) {
                m_timelines->removeAt(i);
                if (*m_selectedTimelineIndex > i) --(*m_selectedTimelineIndex);
                else if (*m_selectedTimelineIndex == i) *m_selectedTimelineIndex = -1;
            }
        }
        for (auto& model : *m_layoutModels) {
            model.sprites.erase(
                std::remove_if(model.sprites.begin(), model.sprites.end(),
                    [&targetSet](const SpritePtr& s) { return s && targetSet.contains(s->path); }),
                model.sprites.end());
        }
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecuteRedo) m_postExecuteRedo();
    }

    void undo() override {
        if (m_folderIndex >= 0 && m_folderIndex < m_smartFolders->size()) {
            (*m_smartFolders)[m_folderIndex].excludedFiles.removeAll(m_relPath);
        }
        *m_activeFramePaths = m_savedActivePaths;
        *m_timelines = m_savedTimelines;
        *m_selectedTimelineIndex = m_savedTimelineIdx;
        *m_layoutModels = m_savedLayoutModels;
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecuteUndo) m_postExecuteUndo();
    }

    int id() const override { return 1021; }

private:
    QVector<SmartFolder>* m_smartFolders;
    int m_folderIndex;
    QString m_relPath;
    QStringList* m_activeFramePaths;
    QVector<AnimationTimeline>* m_timelines;
    int* m_selectedTimelineIndex;
    QVector<LayoutModel>* m_layoutModels;
    QString m_absolutePath;
    QStringList m_savedActivePaths;
    QVector<AnimationTimeline> m_savedTimelines;
    int m_savedTimelineIdx;
    QVector<LayoutModel> m_savedLayoutModels;
    std::function<bool()> m_ensureFrameList;
    std::function<void()> m_postExecuteRedo;
    std::function<void()> m_postExecuteUndo;
};
