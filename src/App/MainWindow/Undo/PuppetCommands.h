#pragma once
#include <QUndoCommand>
#include <QVector>
#include <functional>
#include "../../../Core/PuppetModels.h"

/// What the Puppet workspace has selected: a puppet, a part of it, a variant of that part.
/// -1 = nothing at that level.
struct PuppetSelection {
    int puppet = -1;
    int part = -1;
    int variant = -1;

    bool operator==(const PuppetSelection&) const = default;
};

// ---------------------------------------------------------------------------
// (1030) PuppetsEditCommand
// ---------------------------------------------------------------------------
/// One edit of an atlas's puppets, stored as the puppets before and after it.
/// Edits with the same non-empty merge key in a row (typing a position) are one step.
class PuppetsEditCommand : public QUndoCommand {
public:
    /// Puts @p puppets into the atlas with id @p atlasId and shows @p selection.
    using Apply = std::function<void(const QString& atlasId, const QVector<Puppet>& puppets,
                                     const PuppetSelection& selection)>;

    PuppetsEditCommand(const QString& text, const QString& atlasId,
                       const QVector<Puppet>& before, const QVector<Puppet>& after,
                       const PuppetSelection& selectionBefore, const PuppetSelection& selectionAfter,
                       Apply apply, const QString& mergeKey = QString(),
                       QUndoCommand* parent = nullptr)
        : QUndoCommand(text, parent)
        , m_atlasId(atlasId)
        , m_before(before)
        , m_after(after)
        , m_selectionBefore(selectionBefore)
        , m_selectionAfter(selectionAfter)
        , m_apply(std::move(apply))
        , m_mergeKey(mergeKey)
    {}

    void redo() override { m_apply(m_atlasId, m_after, m_selectionAfter); }
    void undo() override { m_apply(m_atlasId, m_before, m_selectionBefore); }

    int id() const override { return 1030; }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* next = static_cast<const PuppetsEditCommand*>(other);
        if (m_mergeKey.isEmpty() || next->m_mergeKey != m_mergeKey || next->m_atlasId != m_atlasId)
            return false;
        m_after = next->m_after;
        m_selectionAfter = next->m_selectionAfter;
        return true;
    }

private:
    QString m_atlasId;
    QVector<Puppet> m_before;
    QVector<Puppet> m_after;
    PuppetSelection m_selectionBefore;
    PuppetSelection m_selectionAfter;
    Apply m_apply;
    QString m_mergeKey;
};
