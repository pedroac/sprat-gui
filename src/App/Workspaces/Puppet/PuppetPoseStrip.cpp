#include "PuppetPoseStrip.h"

#include <QDropEvent>
#include <QKeyEvent>
#include <QScrollBar>

namespace {
/// The pose index of an item; Rest has none.
constexpr int POSE_ROLE = Qt::UserRole;
/// Room around a thumbnail for its name.
constexpr int FRAME_MARGIN = 28;
}  // namespace

PuppetPoseStrip::PuppetPoseStrip(QWidget* parent) : QListWidget(parent) {
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(false);
    setIconSize(QSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE));
    setGridSize(QSize(THUMBNAIL_SIZE + 16, THUMBNAIL_SIZE + FRAME_MARGIN));
    setMovement(QListView::Snap);
    setDragDropMode(QAbstractItemView::InternalMove);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(sizeHint().height());

    connect(this, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_rebuilding && row >= 0) emit frameSelected(row - 1);
    });
    connect(this, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        const int pose = poseOf(item);
        if (m_rebuilding || pose < 0 || pose >= m_names.size() || item->text() == m_names[pose]) return;
        // Renaming rebuilds the strip, so it waits until Qt is done with the edited item.
        QMetaObject::invokeMethod(this, [this, pose, name = item->text().trimmed()]() {
            emit renameRequested(pose, name);
        }, Qt::QueuedConnection);
    });
}

QSize PuppetPoseStrip::sizeHint() const {
    return QSize(QListWidget::sizeHint().width(),
                 THUMBNAIL_SIZE + FRAME_MARGIN + horizontalScrollBar()->sizeHint().height() + 2 * frameWidth() + 4);
}

void PuppetPoseStrip::showFrames(const QPixmap& rest, const QStringList& poseNames,
                                 const QVector<QPixmap>& thumbnails, int selected) {
    m_rebuilding = true;
    m_names = poseNames;
    clear();
    auto* restItem = new QListWidgetItem(QIcon(rest), tr("Rest"), this);
    restItem->setData(POSE_ROLE, -1);
    restItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    restItem->setToolTip(tr("The rig as it stands: what poses start from"));
    for (int i = 0; i < poseNames.size(); ++i) {
        auto* item = new QListWidgetItem(QIcon(thumbnails.value(i)), poseNames[i], this);
        item->setData(POSE_ROLE, i);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
        item->setToolTip(tr("Double-click to rename, drag to reorder"));
    }
    setCurrentRow(qBound(-1, selected, int(poseNames.size()) - 1) + 1);
    if (currentItem()) scrollToItem(currentItem());
    m_rebuilding = false;
}

int PuppetPoseStrip::selectedFrame() const {
    return currentItem() ? poseOf(currentItem()) : -1;
}

void PuppetPoseStrip::dropEvent(QDropEvent* event) {
    // The strip is rebuilt from the puppet: the drop only asks for the move.
    const int from = selectedFrame();
    QListWidgetItem* over = itemAt(event->position().toPoint());
    const int to = over ? poseOf(over) : int(m_names.size()) - 1;
    event->setDropAction(Qt::IgnoreAction);
    event->accept();
    if (from >= 0 && to >= 0 && from != to) emit moveRequested(from, to);
}

void PuppetPoseStrip::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete && state() != QAbstractItemView::EditingState && selectedFrame() >= 0) {
        emit removeRequested(selectedFrame());
        event->accept();
        return;
    }
    QListWidget::keyPressEvent(event);
}

int PuppetPoseStrip::poseOf(const QListWidgetItem* item) {
    return item ? item->data(POSE_ROLE).toInt() : -1;
}
