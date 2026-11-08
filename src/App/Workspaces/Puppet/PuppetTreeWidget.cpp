#include "PuppetTreeWidget.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>

namespace {
constexpr int PUPPET_ROLE = Qt::UserRole;
constexpr int PART_ROLE = Qt::UserRole + 1;
constexpr int VARIANT_ROLE = Qt::UserRole + 2;
constexpr int GAP_ROLE = Qt::UserRole + 3;
/// How strongly the gap is tinted with the highlight colour (0-255).
constexpr int GAP_ALPHA = 60;

/// The part @p mime carries, or part -1 when it carries none.
PuppetSelection draggedPart(const QMimeData* mime) {
    if (!mime || !mime->hasFormat(PuppetTreeWidget::PART_MIME_TYPE)) return {};
    const QList<QByteArray> fields = mime->data(PuppetTreeWidget::PART_MIME_TYPE).split(' ');
    if (fields.size() != 2) return {};
    bool puppetOk = false;
    bool partOk = false;
    const PuppetSelection part{fields[0].toInt(&puppetOk), fields[1].toInt(&partOk), -1};
    if (!puppetOk || !partOk || part.puppet < 0 || part.part < 0) return {};
    return part;
}
}  // namespace

const QString PuppetTreeWidget::PART_MIME_TYPE = QStringLiteral("application/x-sprat-puppet-part");

PuppetTreeWidget::PuppetTreeWidget(QWidget* parent)
    : QTreeWidget(parent)
{
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    // The gap shows where a part lands.
    setDropIndicatorShown(false);
    setEditTriggers(QAbstractItemView::EditKeyPressed);
}

PuppetSelection PuppetTreeWidget::selectionOf(const QTreeWidgetItem* item) {
    if (!item || isDropGap(item)) return {};
    return {item->data(0, PUPPET_ROLE).toInt(), item->data(0, PART_ROLE).toInt(),
            item->data(0, VARIANT_ROLE).toInt()};
}

void PuppetTreeWidget::setSelectionOf(QTreeWidgetItem* item, const PuppetSelection& selection) {
    item->setData(0, PUPPET_ROLE, selection.puppet);
    item->setData(0, PART_ROLE, selection.part);
    item->setData(0, VARIANT_ROLE, selection.variant);
}

bool PuppetTreeWidget::isDropGap(const QTreeWidgetItem* item) {
    return item && item->data(0, GAP_ROLE).toBool();
}

QStringList PuppetTreeWidget::mimeTypes() const {
    return {PART_MIME_TYPE};
}

QMimeData* PuppetTreeWidget::mimeData(const QList<QTreeWidgetItem*>& items) const {
    if (items.size() != 1) return nullptr;
    const PuppetSelection part = selectionOf(items.first());
    // Only parts move: variants are listed by name.
    if (part.part < 0 || part.variant >= 0) return nullptr;
    auto* mime = new QMimeData;
    mime->setData(PART_MIME_TYPE, QByteArray::number(part.puppet) + ' ' + QByteArray::number(part.part));
    return mime;
}

void PuppetTreeWidget::startDrag(Qt::DropActions supportedActions) {
    QTreeWidget::startDrag(supportedActions);
    endDrag();  // also when the drag was cancelled
}

QTreeWidgetItem* PuppetTreeWidget::itemFor(const PuppetSelection& selection) const {
    for (QTreeWidgetItemIterator it(const_cast<PuppetTreeWidget*>(this)); *it; ++it) {
        if (!isDropGap(*it) && selectionOf(*it) == selection) return *it;
    }
    return nullptr;
}

QTreeWidgetItem* PuppetTreeWidget::dropGap() const {
    // Looked up rather than kept, as rebuilding the tree deletes it.
    for (QTreeWidgetItemIterator it(const_cast<PuppetTreeWidget*>(this)); *it; ++it) {
        if (isDropGap(*it)) return *it;
    }
    return nullptr;
}

int PuppetTreeWidget::dropIndex(const QPoint& pos) const {
    const QTreeWidgetItem* puppetItem = itemFor({m_dragged.puppet, -1, -1});
    if (m_dragged.part < 0 || !puppetItem) return -1;
    // Over the gap, it stays: the rows moved to open it.
    if (const QTreeWidgetItem* gap = dropGap()) {
        const QRect rect = visualItemRect(gap);
        if (pos.y() >= rect.top() && pos.y() <= rect.bottom()) return m_gapIndex;
    }
    // After every other part whose row's middle is above the pointer, wherever it is: over a
    // part's variants, the puppet's row or past the last row.
    int to = 0;
    for (int i = 0; i < puppetItem->childCount(); ++i) {
        const QTreeWidgetItem* child = puppetItem->child(i);
        if (isDropGap(child) || selectionOf(child) == m_dragged) continue;
        if (visualItemRect(child).center().y() < pos.y()) ++to;
    }
    return to;
}

void PuppetTreeWidget::showDropGap(int to) {
    QTreeWidgetItem* puppetItem = itemFor({m_dragged.puppet, -1, -1});
    QTreeWidgetItem* draggedItem = itemFor(m_dragged);
    if (!puppetItem || !draggedItem) return;
    QTreeWidgetItem* gap = dropGap();
    if (gap && m_gapIndex == to) return;
    const int rowHeight = visualItemRect(draggedItem).height();
    delete gap;
    draggedItem->setHidden(true);

    gap = new QTreeWidgetItem;
    gap->setData(0, GAP_ROLE, true);
    gap->setFlags(Qt::ItemIsEnabled);
    gap->setText(0, draggedItem->text(0));
    QFont font = gap->font(0);
    font.setItalic(true);
    gap->setFont(0, font);
    QColor tint = palette().color(QPalette::Highlight);
    gap->setForeground(0, tint);
    tint.setAlpha(GAP_ALPHA);
    for (int column = 0; column < columnCount(); ++column) gap->setBackground(column, tint);
    if (rowHeight > 0) gap->setSizeHint(0, QSize(0, rowHeight));
    // The dragged row is still a child (hidden), so the ones after it are one further on.
    const int child = to < m_dragged.part ? to : to + 1;
    puppetItem->insertChild(qBound(0, child, puppetItem->childCount()), gap);
    m_gapIndex = to;
}

void PuppetTreeWidget::endDrag() {
    delete dropGap();
    if (QTreeWidgetItem* draggedItem = itemFor(m_dragged)) draggedItem->setHidden(false);
    m_dragged = {};
    m_gapIndex = -1;
}

void PuppetTreeWidget::dragEnterEvent(QDragEnterEvent* event) {
    QTreeWidget::dragEnterEvent(event);
    m_dragged = draggedPart(event->mimeData());
    const int to = dropIndex(event->position().toPoint());
    if (to < 0) {
        m_dragged = {};
        event->ignore();
        return;
    }
    showDropGap(to);
    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void PuppetTreeWidget::dragMoveEvent(QDragMoveEvent* event) {
    QTreeWidget::dragMoveEvent(event);  // scrolls near the edges
    const int to = dropIndex(event->position().toPoint());
    if (to < 0) {
        event->ignore();
        return;
    }
    showDropGap(to);
    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void PuppetTreeWidget::dragLeaveEvent(QDragLeaveEvent* event) {
    QTreeWidget::dragLeaveEvent(event);
    endDrag();
}

void PuppetTreeWidget::dropEvent(QDropEvent* event) {
    const PuppetSelection moved = m_dragged;
    const int to = dropIndex(event->position().toPoint());
    // Before the move rebuilds the tree.
    endDrag();
    // The tree is rebuilt from the model, so the rows are never moved here.
    event->setDropAction(Qt::IgnoreAction);
    event->accept();
    if (to >= 0 && to != moved.part) emit moveRequested(moved, to);
}
