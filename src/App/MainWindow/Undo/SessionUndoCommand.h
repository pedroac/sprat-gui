#pragma once
#include <QUndoCommand>
#include "../../../Project/ProjectSession.h"

// Forward declaration of MainWindow or a generic UI refresher
class IMainWindowUndoHost {
public:
    virtual ~IMainWindowUndoHost() = default;
    virtual void refreshUiAfterUndo() = 0;
    virtual void setSourceFolderIsTemp(bool isTemp) = 0;
};

// ---------------------------------------------------------------------------
// SessionUndoCommand — bulk session state update
// ---------------------------------------------------------------------------
class SessionUndoCommand : public QUndoCommand {
public:
    SessionUndoCommand(IMainWindowUndoHost* host,
                       ProjectSession* session,
                       const QString& text,
                       const ProjectSession::SessionState& before,
                       const ProjectSession::SessionState& after,
                       bool alreadyApplied)
        : QUndoCommand(text)
        , m_host(host)
        , m_session(session)
        , m_before(before)
        , m_after(after)
        , m_skipFirstRedo(alreadyApplied)
    {}

    void undo() override {
        m_session->applyState(m_before);
        m_host->setSourceFolderIsTemp(m_before.sourceFolderIsTemp);
        m_host->refreshUiAfterUndo();
    }

    void redo() override {
        if (m_skipFirstRedo) {
            m_skipFirstRedo = false;
            return;
        }
        m_session->applyState(m_after);
        m_host->setSourceFolderIsTemp(m_after.sourceFolderIsTemp);
        m_host->refreshUiAfterUndo();
    }

private:
    IMainWindowUndoHost* m_host;
    ProjectSession* m_session;
    ProjectSession::SessionState m_before;
    ProjectSession::SessionState m_after;
    bool m_skipFirstRedo;
};
