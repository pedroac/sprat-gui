#pragma once
#include <QUndoCommand>
#include <QString>
#include <QComboBox>
#include <functional>

// ---------------------------------------------------------------------------
// (1017) SetProfileCommand
// ---------------------------------------------------------------------------
class SetProfileCommand : public QUndoCommand {
public:
    SetProfileCommand(QComboBox* profileCombo, const QString& oldProfile, const QString& newProfile,
                      std::function<void()> postExecute, QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Change Profile"), parent)
        , m_profileCombo(profileCombo), m_oldProfile(oldProfile), m_newProfile(newProfile)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        int idx = m_profileCombo->findData(m_newProfile);
        if (idx >= 0) {
            m_profileCombo->blockSignals(true);
            m_profileCombo->setCurrentIndex(idx);
            m_profileCombo->blockSignals(false);
            if (m_postExecute) m_postExecute();
        }
    }

    void undo() override {
        int idx = m_profileCombo->findData(m_oldProfile);
        if (idx >= 0) {
            m_profileCombo->blockSignals(true);
            m_profileCombo->setCurrentIndex(idx);
            m_profileCombo->blockSignals(false);
            if (m_postExecute) m_postExecute();
        }
    }

    int id() const override { return 1018; }

private:
    QComboBox* m_profileCombo;
    QString m_oldProfile, m_newProfile;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1018) SetSourceResolutionCommand
// ---------------------------------------------------------------------------
class SetSourceResolutionCommand : public QUndoCommand {
public:
    SetSourceResolutionCommand(QComboBox* resCombo, const QString& oldRes, const QString& newRes,
                               std::function<void()> postExecute, QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Change Source Resolution"), parent)
        , m_resCombo(resCombo), m_oldRes(oldRes), m_newRes(newRes)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        int idx = m_resCombo->findText(m_newRes);
        if (idx >= 0) {
            m_resCombo->blockSignals(true);
            m_resCombo->setCurrentIndex(idx);
            m_resCombo->blockSignals(false);
            if (m_postExecute) m_postExecute();
        }
    }

    void undo() override {
        int idx = m_resCombo->findText(m_oldRes);
        if (idx >= 0) {
            m_resCombo->blockSignals(true);
            m_resCombo->setCurrentIndex(idx);
            m_resCombo->blockSignals(false);
            if (m_postExecute) m_postExecute();
        }
    }

    int id() const override { return 1019; }

private:
    QComboBox* m_resCombo;
    QString m_oldRes, m_newRes;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};
