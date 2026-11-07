#pragma once
#include <QTreeWidget>
#include "PuppetCommands.h"

/**
 * @brief The Puppet workspace's tree: puppet › part › variant, parts in draw order.
 *
 * Variants are listed by name. Dragging a part only reorders it among its siblings; the tree
 * is rebuilt from the model afterwards, so a drop asks for the move rather than moving rows
 * itself. Renaming a row (F2) asks for the rename the same way.
 */
class PuppetTreeWidget : public QTreeWidget {
    Q_OBJECT
public:
    explicit PuppetTreeWidget(QWidget* parent = nullptr);

    /// What @p item stands for; every level below it is -1.
    static PuppetSelection selectionOf(const QTreeWidgetItem* item);
    /// Stores what @p item stands for.
    static void setSelectionOf(QTreeWidgetItem* item, const PuppetSelection& selection);

signals:
    /// Move the part @p moved so it ends up at index @p to among its siblings.
    void moveRequested(const PuppetSelection& moved, int to);

protected:
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    /// Where the dragged row would end up among its siblings, or -1 when the drop
    /// isn't next to one of them.
    int dropIndex(const QPoint& pos) const;
};
