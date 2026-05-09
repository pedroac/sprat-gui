#pragma once
#include <QUndoCommand>
#include "../../Core/models.h"

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
        if (m_sprite) { m_sprite->pivotX = m_newX; m_sprite->pivotY = m_newY; }
    }

    void undo() override {
        if (m_sprite) { m_sprite->pivotX = m_oldX; m_sprite->pivotY = m_oldY; }
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
