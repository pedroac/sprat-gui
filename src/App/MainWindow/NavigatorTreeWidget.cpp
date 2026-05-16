#include "NavigatorTreeWidget.h"
#include "models.h"

#include <functional>
#include <QDrag>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>

NavigatorTreeWidget::NavigatorTreeWidget(QWidget* parent)
    : QTreeWidget(parent)
{
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
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

void NavigatorTreeWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete) {
        QStringList paths;
        std::function<void(QTreeWidgetItem*)> collectPaths = [&](QTreeWidgetItem* item) {
            QVariant v = item->data(0, Qt::UserRole);
            if (v.isValid()) {
                auto sprite = v.value<SpritePtr>();
                if (sprite && !sprite->path.isEmpty())
                    paths.append(sprite->path);
            } else if (item->childCount() > 0) {
                for (int i = 0; i < item->childCount(); ++i)
                    collectPaths(item->child(i));
            }
        };
        for (QTreeWidgetItem* item : selectedItems())
            collectPaths(item);

        if (!paths.isEmpty()) {
            emit deleteRequested(paths);
            event->accept();
            return;
        }
    }
    QTreeWidget::keyPressEvent(event);
}
