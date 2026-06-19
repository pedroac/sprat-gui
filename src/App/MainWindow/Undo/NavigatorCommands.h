#pragma once
#include <QUndoCommand>
#include <QStringList>
#include <functional>

// ---------------------------------------------------------------------------
// (1026) NavigatorHideFolderCommand
// ---------------------------------------------------------------------------
class NavigatorHideFolderCommand : public QUndoCommand {
public:
    NavigatorHideFolderCommand(QStringList* hiddenFolders,
                               const QString& relPath,
                               bool hide,
                               std::function<void()> postExecute,
                               QUndoCommand* parent = nullptr)
        : QUndoCommand(hide ? QObject::tr("Hide Group") : QObject::tr("Unhide Group"), parent)
        , m_hiddenFolders(hiddenFolders)
        , m_relPath(relPath)
        , m_hide(hide)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_hide) { if (!m_hiddenFolders->contains(m_relPath)) m_hiddenFolders->append(m_relPath); }
        else        m_hiddenFolders->removeAll(m_relPath);
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        if (m_hide) m_hiddenFolders->removeAll(m_relPath);
        else        { if (!m_hiddenFolders->contains(m_relPath)) m_hiddenFolders->append(m_relPath); }
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1026; }

private:
    QStringList* m_hiddenFolders;
    QString m_relPath;
    bool m_hide;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1027) ExcludeGroupFromSourceCommand
// ---------------------------------------------------------------------------
class ExcludeGroupFromSourceCommand : public QUndoCommand {
public:
    ExcludeGroupFromSourceCommand(QStringList* excludedFiles,
                                  QStringList* activeFramePaths,
                                  const QStringList& relPaths,
                                  const QStringList& savedActivePaths,
                                  const QStringList& newActivePaths,
                                  bool exclude,
                                  std::function<bool()> ensureFrameList,
                                  std::function<void()> postExecute,
                                  QUndoCommand* parent = nullptr)
        : QUndoCommand(exclude ? QObject::tr("Hide with Descendants")
                               : QObject::tr("Re-include"), parent)
        , m_excludedFiles(excludedFiles)
        , m_activeFramePaths(activeFramePaths)
        , m_relPaths(relPaths)
        , m_savedActivePaths(savedActivePaths)
        , m_newActivePaths(newActivePaths)
        , m_exclude(exclude)
        , m_ensureFrameList(std::move(ensureFrameList))
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        applyState(m_exclude);
    }

    void undo() override {
        applyState(!m_exclude);
    }

    int id() const override { return 1027; }

private:
    void applyState(bool exclude) {
        if (exclude) {
            for (const QString& rel : m_relPaths)
                if (!m_excludedFiles->contains(rel)) m_excludedFiles->append(rel);
            *m_activeFramePaths = m_newActivePaths;
        } else {
            for (const QString& rel : m_relPaths) m_excludedFiles->removeAll(rel);
            *m_activeFramePaths = m_savedActivePaths;
        }
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecute)     m_postExecute();
    }

    QStringList* m_excludedFiles;
    QStringList* m_activeFramePaths;
    QStringList  m_relPaths;
    QStringList  m_savedActivePaths;
    QStringList  m_newActivePaths;
    bool         m_exclude;
    std::function<bool()> m_ensureFrameList;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};
