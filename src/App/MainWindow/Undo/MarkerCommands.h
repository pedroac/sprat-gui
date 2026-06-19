#pragma once
#include <QUndoCommand>
#include <QVector>
#include <functional>
#include "../../../Core/SpriteModels.h"

// ---------------------------------------------------------------------------
// (1009) SetMarkersCommand
// ---------------------------------------------------------------------------
class SetMarkersCommand : public QUndoCommand {
public:
    struct CoTarget {
        SpritePtr sprite;
        QVector<NamedPoint> oldPoints;
        QVector<NamedPoint> newPoints;
    };

    SetMarkersCommand(SpritePtr sprite,
                       const QVector<NamedPoint>& oldPoints,
                       const QVector<NamedPoint>& newPoints,
                       std::function<void()> postExecute,
                       QVector<CoTarget> coTargets = {},
                       QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Edit Markers"), parent)
        , m_sprite(sprite)
        , m_oldPoints(oldPoints)
        , m_newPoints(newPoints)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
        , m_coTargets(std::move(coTargets))
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_sprite) m_sprite->points = m_newPoints;
        for (const auto& target : m_coTargets) {
            if (target.sprite) target.sprite->points = target.newPoints;
        }
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        if (m_sprite) m_sprite->points = m_oldPoints;
        for (const auto& target : m_coTargets) {
            if (target.sprite) target.sprite->points = target.oldPoints;
        }
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1009; }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* o = static_cast<const SetMarkersCommand*>(other);
        if (o->m_sprite != m_sprite) return false;
        if (o->m_coTargets.size() != m_coTargets.size()) return false;
        for (int i = 0; i < m_coTargets.size(); ++i) {
            if (m_coTargets[i].sprite != o->m_coTargets[i].sprite) return false;
        }
        m_newPoints = o->m_newPoints;
        for (int i = 0; i < m_coTargets.size(); ++i)
            m_coTargets[i].newPoints = o->m_coTargets[i].newPoints;
        return true;
    }

private:
    SpritePtr m_sprite;
    QVector<NamedPoint> m_oldPoints;
    QVector<NamedPoint> m_newPoints;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
    QVector<CoTarget> m_coTargets;
};
