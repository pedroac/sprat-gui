#include "TimelineTreeWidget.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QTreeWidgetItem>

static const char* kMimeType       = "application/x-sprat-timeline";
static const char* kSpriteMimeType = "application/x-sprat-sprite";

TimelineTreeWidget::TimelineTreeWidget(QWidget* parent)
    : QTreeWidget(parent)
{
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(false);
    setAcceptDrops(true);
}

void TimelineTreeWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete) {
        emit deleteKeyPressed();
        event->accept();
        return;
    }
    QTreeWidget::keyPressEvent(event);
}

void TimelineTreeWidget::startDrag(Qt::DropActions /*supportedActions*/)
{
    QTreeWidgetItem* item = currentItem();
    if (!item) return;

    m_draggedItem = item;
    const bool isLeaf = item->data(0, Qt::UserRole).isValid();
    m_draggedIsLeaf = isLeaf;

    QByteArray payload;
    if (isLeaf) {
        m_draggedIndex = item->data(0, Qt::UserRole).toInt();
        m_draggedFolderPath.clear();
        payload = QByteArray("leaf:") + QByteArray::number(m_draggedIndex);
    } else {
        m_draggedIndex = -1;
        m_draggedFolderPath = folderNodePath(item);
        payload = QByteArray("folder:") + m_draggedFolderPath.toUtf8();
    }

    QMimeData* mimeData = new QMimeData;
    mimeData->setData(kMimeType, payload);

    QDrag* drag = new QDrag(this);
    drag->setMimeData(mimeData);
    drag->exec(Qt::MoveAction);
}

void TimelineTreeWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(kMimeType) ||
        event->mimeData()->hasFormat(kSpriteMimeType)) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TimelineTreeWidget::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasFormat(kSpriteMimeType)) {
        // Sprites can be dropped anywhere to create new timelines
        event->acceptProposedAction();
        return;
    }

    if (!event->mimeData()->hasFormat(kMimeType)) {
        event->ignore();
        return;
    }

    QTreeWidgetItem* target = itemAt(event->position().toPoint());

    if (target) {
        // Must be a folder node
        if (target->data(0, Qt::UserRole).isValid()) {
            event->ignore();
            return;
        }
        // Must not be the dragged item itself or a descendant of it
        if (!m_draggedIsLeaf && m_draggedItem) {
            if (target == m_draggedItem || isAncestorOf(m_draggedItem, target)) {
                event->ignore();
                return;
            }
        }
    }
    // target == nullptr means viewport (root) — always accept

    event->acceptProposedAction();
}

void TimelineTreeWidget::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasFormat(kSpriteMimeType)) {
        const QStringList paths = QString::fromUtf8(
            event->mimeData()->data(kSpriteMimeType)).split('\n', Qt::SkipEmptyParts);
        if (paths.isEmpty()) { event->ignore(); return; }

        QTreeWidgetItem* target = itemAt(event->position().toPoint());
        QString targetFolderPath;
        if (target) {
            if (!target->data(0, Qt::UserRole).isValid()) {
                // Folder node
                targetFolderPath = folderNodePath(target);
            } else {
                // Leaf node — use its parent folder
                targetFolderPath = folderNodePath(target->parent());
            }
        }

        event->acceptProposedAction();
        emit spritesDroppedToCreate(paths, targetFolderPath);
        return;
    }

    if (!event->mimeData()->hasFormat(kMimeType)) {
        event->ignore();
        return;
    }

    QTreeWidgetItem* target = itemAt(event->position().toPoint());
    QString targetFolderPath;

    if (target) {
        // Must be a folder node
        if (target->data(0, Qt::UserRole).isValid()) {
            event->ignore();
            return;
        }
        // Must not be self or descendant
        if (!m_draggedIsLeaf && m_draggedItem) {
            if (target == m_draggedItem || isAncestorOf(m_draggedItem, target)) {
                event->ignore();
                return;
            }
        }
        targetFolderPath = folderNodePath(target);
    }
    // target == nullptr → root level, targetFolderPath stays empty

    event->acceptProposedAction();
    // Do NOT call base class — we handle everything via the signal
    emit dropCompleted(m_draggedIndex, m_draggedFolderPath, targetFolderPath);
}

QString TimelineTreeWidget::folderNodePath(QTreeWidgetItem* item) const
{
    if (!item) return QString();
    QStringList parts;
    QTreeWidgetItem* cur = item;
    while (cur) {
        // Only folder nodes (no UserRole) contribute to the path
        if (!cur->data(0, Qt::UserRole).isValid()) {
            parts.prepend(cur->text(0));
        }
        cur = cur->parent();
    }
    return parts.join('/');
}

bool TimelineTreeWidget::isAncestorOf(QTreeWidgetItem* ancestor, QTreeWidgetItem* descendant) const
{
    QTreeWidgetItem* cur = descendant ? descendant->parent() : nullptr;
    while (cur) {
        if (cur == ancestor) return true;
        cur = cur->parent();
    }
    return false;
}
