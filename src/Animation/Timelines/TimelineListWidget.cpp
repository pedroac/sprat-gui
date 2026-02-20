#include "TimelineListWidget.h"
#include <QDragEnterEvent>
#include <QMimeData>
#include <QKeyEvent>
#include <QDrag>
#include <QPainter>
#include <QMenu>
#include <QAction>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QtGlobal>

/**
 * @brief Constructs the TimelineListWidget.
 * 
 * Sets up drag and drop, selection mode, and context menu policy.
 * @param parent The parent widget.
 */
TimelineListWidget::TimelineListWidget(QWidget* parent) : QListWidget(parent) {
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &TimelineListWidget::onCustomContextMenuRequested);
}

/**
 * @brief Handles drag enter events.
 * 
 * Accepts drags from the layout canvas (application/x-sprat-sprite) or internal reordering.
 */
void TimelineListWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat("application/x-sprat-sprite") ||
        event->source() == this) {
        updatePlaceholder(event->position().toPoint());
        event->acceptProposedAction();
    } else {
        QListWidget::dragEnterEvent(event);
    }
}

/**
 * @brief Handles drag move events to update the placeholder position.
 */
void TimelineListWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasFormat("application/x-sprat-sprite") ||
        event->source() == this) {
        updatePlaceholder(event->position().toPoint());
        event->acceptProposedAction();
    } else {
        QListWidget::dragMoveEvent(event);
    }
}

/**
 * @brief Cleans up the placeholder when a drag leaves the widget.
 */
void TimelineListWidget::dragLeaveEvent(QDragLeaveEvent* event) {
    clearPlaceholder();
    QListWidget::dragLeaveEvent(event);
}

/**
 * @brief Handles drop events to insert frames or reorder items.
 */
void TimelineListWidget::dropEvent(QDropEvent* event) {
    int targetRow = -1;
    if (m_placeholderItem) {
        targetRow = row(m_placeholderItem);
        clearPlaceholder();
    } else {
        QListWidgetItem* item = itemAt(event->position().toPoint());
        targetRow = item ? row(item) : count();
    }

    if (event->mimeData()->hasFormat("application/x-sprat-sprite")) {
        QByteArray data = event->mimeData()->data("application/x-sprat-sprite");
        QStringList paths = QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts);
        for (const QString& path : paths) {
            emit frameDropped(path, targetRow++);
        }
        event->acceptProposedAction();
    } else if (event->source() == this) {
        QList<QListWidgetItem*> items = selectedItems();
        if (items.isEmpty()) {
            return;
        }
        int fromRow = row(items.first());
        emit frameMoved(fromRow, targetRow);
        event->acceptProposedAction();
    } else {
        QListWidget::dropEvent(event);
    }
}

/**
 * @brief Handles key press events (Delete/Backspace to remove frames).
 */
void TimelineListWidget::keyPressEvent(QKeyEvent* event) {
    if (event->matches(QKeySequence::SelectAll)) {
        selectAll();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        emit removeSelectedRequested();
        event->accept();
        return;
    }

    int rowDelta = 0;
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Up) {
        rowDelta = -1;
    } else if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Down) {
        rowDelta = 1;
    }

    if (rowDelta != 0 && count() > 0) {
        int current = currentRow();
        if (current < 0) {
            current = 0;
        }
        const int next = qBound(0, current + rowDelta, count() - 1);
        if (next != current || currentRow() < 0) {
            const auto selectionFlag = (event->modifiers() & Qt::ShiftModifier)
                ? QItemSelectionModel::SelectCurrent
                : QItemSelectionModel::ClearAndSelect;
            setCurrentRow(next, selectionFlag);
            scrollToItem(item(next));
        }
        event->accept();
        return;
    } else {
        QListWidget::keyPressEvent(event);
    }
}

Qt::DropActions TimelineListWidget::supportedDropActions() const {
    return Qt::CopyAction | Qt::MoveAction;
}

/**
 * @brief Starts a drag operation with a custom thumbnail.
 * 
 * Generates a pixmap showing the dragged item(s) centered under the cursor.
 */
void TimelineListWidget::startDrag(Qt::DropActions supportedActions)
{
    QList<QListWidgetItem*> items = selectedItems();
    if (items.isEmpty()) {
        return;
    }

    QMimeData *mimeData = model()->mimeData(selectionModel()->selectedIndexes());
    if (!mimeData) {
        return;
    }

    QPixmap dragPixmap;
    if (items.size() == 1) {
        dragPixmap = items.first()->icon().pixmap(iconSize());
    } else {
        dragPixmap = QPixmap(64, 64);
        dragPixmap.fill(Qt::transparent);
        QPainter p(&dragPixmap);
        p.setBrush(QColor(255, 255, 255, 200));
        p.setPen(Qt::black);
        p.drawRect(0, 0, 63, 63);
        p.drawText(dragPixmap.rect(), Qt::AlignCenter, QString::number(items.size()));
    }

    QDrag *drag = new QDrag(this);
    drag->setMimeData(mimeData);
    drag->setPixmap(dragPixmap);
    drag->setHotSpot(QPoint(dragPixmap.width() / 2, dragPixmap.height() / 2));
    drag->exec(supportedActions, Qt::MoveAction);
}

/**
 * @brief Shows the context menu for a timeline item.
 */
void TimelineListWidget::onCustomContextMenuRequested(const QPoint& pos) {
    QListWidgetItem* item = itemAt(pos);
    if (!item) {
        return;
    }

    int idx = row(item);
    QMenu menu(this);
    
    QAction* moveLeft = menu.addAction("Move to the left");
    moveLeft->setEnabled(idx > 0);
    connect(moveLeft, &QAction::triggered, [this, idx]() {
        emit frameMoved(idx, idx - 1);
    });

    QAction* moveRight = menu.addAction("Move to the right");
    moveRight->setEnabled(idx < count() - 1);
    connect(moveRight, &QAction::triggered, [this, idx]() {
        emit frameMoved(idx, idx + 2);
    });

    QAction* duplicate = menu.addAction("Duplicate");
    connect(duplicate, &QAction::triggered, [this, idx]() {
        emit duplicateFrameRequested(idx);
    });

    menu.addSeparator();

    QAction* del = menu.addAction("Delete");
    connect(del, &QAction::triggered, this, &TimelineListWidget::removeSelectedRequested);

    menu.exec(mapToGlobal(pos));
}

/**
 * @brief Updates the position of the drop placeholder item.
 */
void TimelineListWidget::updatePlaceholder(const QPoint& pos) {
    if (m_placeholderItem) {
        takeItem(row(m_placeholderItem));
    } else {
        m_placeholderItem = new QListWidgetItem();
        m_placeholderItem->setFlags(Qt::NoItemFlags);
        m_placeholderItem->setSizeHint(QSize(64, 64));
    }
    QListWidgetItem* item = itemAt(pos);
    insertItem(item ? row(item) : count(), m_placeholderItem);
}

/**
 * @brief Removes the placeholder item from the list.
 */
void TimelineListWidget::clearPlaceholder() {
    if (m_placeholderItem) {
        delete takeItem(row(m_placeholderItem));
        m_placeholderItem = nullptr;
    }
}
