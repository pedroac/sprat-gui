#include "NavigatorTreeWidget.h"
#include "models.h"

#include <functional>
#include <QDrag>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QTreeWidgetItemIterator>

NavigatorTreeWidget::NavigatorTreeWidget(QWidget* parent)
    : QTreeWidget(parent)
{
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setSelectionMode(QAbstractItemView::SingleSelection);
}

void NavigatorTreeWidget::startDrag(Qt::DropActions /*supportedActions*/)
{
    QStringList paths;
    const auto items = selectedItems();

    // Helper to recursively collect sprite paths from an item and its descendants
    std::function<void(QTreeWidgetItem*)> collectSpritePaths = [&](QTreeWidgetItem* item) {
        QVariant v = item->data(0, Qt::UserRole);
        if (v.isValid()) {
            // This is a sprite leaf
            auto sprite = v.value<SpritePtr>();
            if (sprite && !sprite->path.isEmpty())
                paths.append(sprite->path);
        } else if (item->childCount() > 0) {
            // This is a group node, collect from all descendants
            for (int i = 0; i < item->childCount(); ++i) {
                collectSpritePaths(item->child(i));
            }
        }
    };

    for (QTreeWidgetItem* item : items) {
        collectSpritePaths(item);
    }
    if (paths.isEmpty()) return;

    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData;
    mimeData->setData("application/x-sprat-sprite", paths.join('\n').toUtf8());

    // Build a small thumbnail pixmap
    const int thumbSize = 32;
    const int maxThumbs = qMin(paths.size(), 4);
    QPixmap pixmap(thumbSize + (maxThumbs - 1) * 6, thumbSize + (maxThumbs - 1) * 6);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    for (int i = 0; i < maxThumbs; ++i) {
        QPixmap thumb(paths[i]);
        if (!thumb.isNull()) {
            thumb = thumb.scaled(thumbSize, thumbSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            painter.drawPixmap(i * 6, i * 6, thumb);
        }
    }
    painter.end();

    drag->setMimeData(mimeData);
    drag->setPixmap(pixmap);
    drag->exec(Qt::CopyAction);
}

void NavigatorTreeWidget::mousePressEvent(QMouseEvent* event)
{
    QTreeWidgetItem* item = itemAt(event->pos());
    if (item && event->button() == Qt::LeftButton) {
        const Qt::KeyboardModifiers mods = event->modifiers();

        if (mods & Qt::ShiftModifier) {
            // Range-check: set all visible items from anchor to this item as Checked.
            QTreeWidgetItem* anchor = m_checkboxAnchor ? m_checkboxAnchor : item;
            setCheckStateRange(anchor, item, Qt::Checked);
            setCurrentItem(item);
            event->accept();
            return;
        }

        if (mods & Qt::ControlModifier) {
            // Ctrl+click: toggle the item's checkbox and select it for the editor.
            if (item->flags() & Qt::ItemIsUserCheckable)
                item->setCheckState(0, item->checkState(0) == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            setCurrentItem(item);
            m_checkboxAnchor = item;
            event->accept();
            return;
        }

        // Normal click: update the anchor for future Shift+clicks.
        m_checkboxAnchor = item;
    }

    QTreeWidget::mousePressEvent(event);
}

void NavigatorTreeWidget::setCheckStateRange(QTreeWidgetItem* from, QTreeWidgetItem* to, Qt::CheckState state)
{
    // Collect all currently visible items in visual (top-to-bottom) order.
    QList<QTreeWidgetItem*> visible;
    QTreeWidgetItemIterator it(this, QTreeWidgetItemIterator::NotHidden);
    while (*it) {
        visible.append(*it);
        ++it;
    }

    int fromIdx = visible.indexOf(from);
    int toIdx   = visible.indexOf(to);
    if (fromIdx < 0 || toIdx < 0) return;
    if (fromIdx > toIdx) qSwap(fromIdx, toIdx);

    for (int i = fromIdx; i <= toIdx; ++i) {
        QTreeWidgetItem* node = visible[i];
        if (node->flags() & Qt::ItemIsUserCheckable)
            node->setCheckState(0, state);
    }
}

void NavigatorTreeWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete) {
        QTreeWidgetItem* item = currentItem();
        if (item) {
            emit excludeRequested(item);
            event->accept();
            return;
        }
    }
    QTreeWidget::keyPressEvent(event);
}
