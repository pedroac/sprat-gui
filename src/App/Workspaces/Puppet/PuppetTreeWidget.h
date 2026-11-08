#pragma once
#include <QTreeWidget>
#include "PuppetCommands.h"

/**
 * @brief The Puppet workspace's tree: puppet › part › variant, parts in draw order.
 *
 * Variants are listed by name. Dragging a part only reorders it among its siblings: while it
 * is dragged its row is lifted out and a gap shows where it would land, anywhere over its
 * puppet's rows (over a part's upper half: before it; below the last part: last). The tree is
 * rebuilt from the model afterwards, so a drop asks for the move rather than moving rows
 * itself. Renaming a row (F2) asks for the rename the same way.
 */
class PuppetTreeWidget : public QTreeWidget {
    Q_OBJECT
public:
    /// The MIME type a dragged part carries: "PUPPET PART" (indexes).
    static const QString PART_MIME_TYPE;

    explicit PuppetTreeWidget(QWidget* parent = nullptr);

    /// What @p item stands for; every level below it is -1.
    static PuppetSelection selectionOf(const QTreeWidgetItem* item);
    /// Stores what @p item stands for.
    static void setSelectionOf(QTreeWidgetItem* item, const PuppetSelection& selection);
    /// Whether @p item is the gap shown where a dragged part would land.
    static bool isDropGap(const QTreeWidgetItem* item);

signals:
    /// Move the part @p moved so it ends up at index @p to among its siblings.
    void moveRequested(const PuppetSelection& moved, int to);

protected:
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    QTreeWidgetItem* itemFor(const PuppetSelection& selection) const;
    QTreeWidgetItem* dropGap() const;
    /// The index the dragged part would end up at, or -1 when nothing is dragged.
    int dropIndex(const QPoint& pos) const;
    /// Hides the dragged part's row and opens the gap where it would end up at @p to.
    void showDropGap(int to);
    /// Shows the dragged part's row again and closes the gap.
    void endDrag();

    PuppetSelection m_dragged;  ///< The part being dragged over the tree; part -1 for none
    int m_gapIndex = -1;        ///< Where the gap stands for
};
