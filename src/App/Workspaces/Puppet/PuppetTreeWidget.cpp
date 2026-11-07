#include "PuppetTreeWidget.h"

#include <QDragMoveEvent>
#include <QDropEvent>

namespace {
constexpr int PUPPET_ROLE = Qt::UserRole;
constexpr int PART_ROLE = Qt::UserRole + 1;
constexpr int VARIANT_ROLE = Qt::UserRole + 2;

/// Whether @p a and @p b are parts of the same puppet.
bool siblingParts(const PuppetSelection& a, const PuppetSelection& b) {
    return a.puppet == b.puppet && a.part >= 0 && b.part >= 0 && a.variant < 0 && b.variant < 0;
}
}  // namespace

PuppetTreeWidget::PuppetTreeWidget(QWidget* parent)
    : QTreeWidget(parent)
{
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(true);
    setEditTriggers(QAbstractItemView::EditKeyPressed);
}

PuppetSelection PuppetTreeWidget::selectionOf(const QTreeWidgetItem* item) {
    if (!item) return {};
    return {item->data(0, PUPPET_ROLE).toInt(), item->data(0, PART_ROLE).toInt(),
            item->data(0, VARIANT_ROLE).toInt()};
}

void PuppetTreeWidget::setSelectionOf(QTreeWidgetItem* item, const PuppetSelection& selection) {
    item->setData(0, PUPPET_ROLE, selection.puppet);
    item->setData(0, PART_ROLE, selection.part);
    item->setData(0, VARIANT_ROLE, selection.variant);
}

int PuppetTreeWidget::dropIndex(const QPoint& pos) const {
    const PuppetSelection moved = selectionOf(currentItem());
    const QTreeWidgetItem* target = itemAt(pos);
    // Only parts move: variants are listed by name.
    if (!target || moved.part < 0 || moved.variant >= 0) return -1;
    const PuppetSelection over = selectionOf(target);
    const int from = moved.part;

    int to = -1;
    if (siblingParts(moved, over)) {
        // On the row's upper half, or above it: before it; otherwise after it.
        const QRect rect = visualItemRect(target);
        const bool before = dropIndicatorPosition() == AboveItem
            || (dropIndicatorPosition() == OnItem && pos.y() < rect.center().y());
        to = before ? over.part : over.part + 1;
    } else if (over.puppet == moved.puppet && over.part < 0) {
        to = 0;  // on the puppet's row: first
    } else {
        return -1;
    }
    // The row leaves its place first, so the ones after it move up by one.
    return to > from ? to - 1 : to;
}

void PuppetTreeWidget::dragMoveEvent(QDragMoveEvent* event) {
    QTreeWidget::dragMoveEvent(event);
    if (event->source() != this || dropIndex(event->position().toPoint()) < 0)
        event->ignore();
}

void PuppetTreeWidget::dropEvent(QDropEvent* event) {
    const int to = event->source() == this ? dropIndex(event->position().toPoint()) : -1;
    const PuppetSelection moved = selectionOf(currentItem());
    // The tree is rebuilt from the model, so the rows are never moved here.
    event->setDropAction(Qt::IgnoreAction);
    event->accept();
    if (to >= 0) emit moveRequested(moved, to);
}
