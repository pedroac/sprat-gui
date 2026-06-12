#include "NavigatorTreeWidget.h"
#include "SpritePreviewTooltip.h"
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

    viewport()->setMouseTracking(true);

    m_hoverTimer.setSingleShot(true);
    m_hoverTimer.setInterval(400);
    connect(&m_hoverTimer, &QTimer::timeout, this, &NavigatorTreeWidget::showPreview);
}

void NavigatorTreeWidget::setPreviewDelay(int ms) {
    m_hoverTimer.setInterval(ms);
}

void NavigatorTreeWidget::setPreviewEnabled(bool enabled) {
    m_previewEnabled = enabled;
    if (!enabled) {
        m_hoverTimer.stop();
        m_hoveredItem = nullptr;
        if (m_previewTooltip) m_previewTooltip->hide();
    }
}

void NavigatorTreeWidget::mouseMoveEvent(QMouseEvent* event) {
    QTreeWidget::mouseMoveEvent(event);

    if (!m_previewEnabled) return;

    QTreeWidgetItem* item = itemAt(event->pos());
    SpritePtr sprite;
    if (item && item->childCount() == 0) {
        const QVariant v = item->data(0, Qt::UserRole);
        if (v.isValid()) sprite = v.value<SpritePtr>();
    }

    if (!sprite) {
        m_hoverTimer.stop();
        m_hoveredItem = nullptr;
        if (m_previewTooltip) m_previewTooltip->hide();
        return;
    }

    m_hoverGlobalPos = event->globalPosition().toPoint();
    if (item != m_hoveredItem) {
        m_hoveredItem = item;
        m_hoverTimer.stop();
        if (m_previewTooltip) m_previewTooltip->hide();
        m_hoverTimer.start();
    }
}

void NavigatorTreeWidget::leaveEvent(QEvent* event) {
    QTreeWidget::leaveEvent(event);
    m_hoverTimer.stop();
    m_hoveredItem = nullptr;
    if (m_previewTooltip) m_previewTooltip->hide();
}

void NavigatorTreeWidget::showPreview() {
    if (!m_hoveredItem) return;
    const QVariant v = m_hoveredItem->data(0, Qt::UserRole);
    if (!v.isValid()) return;
    const auto sprite = v.value<SpritePtr>();
    if (!sprite || sprite->path.isEmpty()) return;

    if (!m_previewTooltip)
        m_previewTooltip = new SpritePreviewTooltip(this);
    m_previewTooltip->showAt(sprite->path, m_hoverGlobalPos);
}

void NavigatorTreeWidget::startDrag(Qt::DropActions /*supportedActions*/)
{
    QStringList paths;
    const auto items = selectedItems();

    std::function<void(QTreeWidgetItem*)> collectSpritePaths = [&](QTreeWidgetItem* item) {
        QVariant v = item->data(0, Qt::UserRole);
        if (v.isValid()) {
            auto sprite = v.value<SpritePtr>();
            if (sprite && !sprite->path.isEmpty())
                paths.append(sprite->path);
        } else if (item->childCount() > 0) {
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
            QTreeWidgetItem* anchor = m_checkboxAnchor ? m_checkboxAnchor : item;
            setCheckStateRange(anchor, item, Qt::Checked);
            m_checkboxAnchor = item;
        } else if (mods & Qt::ControlModifier) {
            if (item->flags() & Qt::ItemIsUserCheckable)
                item->setCheckState(0, item->checkState(0) == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            m_checkboxAnchor = item;
        } else {
            m_checkboxAnchor = item;
        }
    }

    QTreeWidget::mousePressEvent(event);
}

void NavigatorTreeWidget::setCheckStateRange(QTreeWidgetItem* from, QTreeWidgetItem* to, Qt::CheckState state)
{
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
