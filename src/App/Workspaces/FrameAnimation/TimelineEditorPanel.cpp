#include "TimelineEditorPanel.h"
#include "ProjectSession.h"
#include "TimelineTreeWidget.h"
#include "TimelineListWidget.h"
#include "UndoCommands.h"
#include "MessageDialog.h"
#include "AnimationTimelineOps.h"
#include "TimelineGenerationService.h"
#include "TimelineUi.h"
#include "SpriteSelectionPresenter.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QUndoStack>
#include <QVBoxLayout>
#include <algorithm>
#include <numeric>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
TimelineEditorPanel::TimelineEditorPanel(ProjectSession* session, QUndoStack* undoStack, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
    , m_undoStack(undoStack)
{
    const int groupMargin      = 4;
    const int groupTopPadding  = 4;
    const int groupBottomMargin = 0;

    // ── Top section: timeline list
    m_listArea = new QWidget(this);
    m_listArea->setStyleSheet("font-weight: normal;");
    auto* listLayout = new QVBoxLayout(m_listArea);
    listLayout->setContentsMargins(groupMargin, groupTopPadding, groupMargin, groupBottomMargin);

    auto* addLayout = new QHBoxLayout();
    addLayout->addWidget(new QLabel(tr("Name:"), m_listArea));
    m_timelineCreateEdit = new QLineEdit(m_listArea);
    m_timelineCreateEdit->setPlaceholderText(tr("Timeline name (optional)"));
    addLayout->addWidget(m_timelineCreateEdit);

    m_addBtn = new QPushButton(
        QApplication::style()->standardIcon(QStyle::SP_FileDialogNewFolder), "", m_listArea);
    m_addBtn->setToolTip(tr("Add timeline"));
    addLayout->addWidget(m_addBtn);
    listLayout->addLayout(addLayout);

    m_timelineList = new TimelineTreeWidget(m_listArea);
    m_timelineList->setHeaderHidden(true);
    m_timelineList->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_timelineList->setIconSize(QSize(32, 32));
    m_timelineList->setDragEnabled(true);
    m_timelineList->setContextMenuPolicy(Qt::CustomContextMenu);
    listLayout->addWidget(m_timelineList, 1);

    // ── Bottom section: selected-timeline editor
    m_timelineEditorContainer = new QWidget(this);
    m_timelineEditorContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto* editorContainerLayout = new QVBoxLayout(m_timelineEditorContainer);
    editorContainerLayout->setContentsMargins(0, 0, 0, 0);

    m_selectedTimelineGroup = new QWidget(m_timelineEditorContainer);
    auto* groupLayout = new QVBoxLayout(m_selectedTimelineGroup);
    editorContainerLayout->addWidget(m_selectedTimelineGroup);

    // Name / FPS / Remove row
    auto* nameLayout = new QHBoxLayout();
    nameLayout->addWidget(new QLabel(tr("Name:"), m_selectedTimelineGroup));
    m_timelineNameEdit = new QLineEdit(m_selectedTimelineGroup);
    m_timelineNameEdit->setEnabled(false);
    nameLayout->addWidget(m_timelineNameEdit);
    nameLayout->addWidget(new QLabel(tr("FPS:"), m_selectedTimelineGroup));
    m_timelineFpsSpin = new QSpinBox(m_selectedTimelineGroup);
    m_timelineFpsSpin->setRange(1, 60);
    m_timelineFpsSpin->setValue(8);
    m_timelineFpsSpin->setEnabled(false);
    m_timelineFpsSpin->setToolTip(tr("Frames per second for animation playback"));
    m_timelineFpsSpin->setAccessibleName(tr("Timeline FPS"));
    nameLayout->addWidget(m_timelineFpsSpin);
    m_removeBtn = new QPushButton(
        QApplication::style()->standardIcon(QStyle::SP_DialogDiscardButton), "",
        m_selectedTimelineGroup);
    m_removeBtn->setToolTip(tr("Remove timeline"));
    nameLayout->addWidget(m_removeBtn);
    groupLayout->addLayout(nameLayout);

    // Alias label
    m_timelineAliasLabel = new QLabel(m_selectedTimelineGroup);
    m_timelineAliasLabel->setVisible(false);
    m_timelineAliasLabel->setToolTip(
        tr("An alias timeline references another timeline's frames but can have its own flip and transform settings."));
    groupLayout->addWidget(m_timelineAliasLabel);

    // Flip row
    auto* flipRow = new QHBoxLayout();
    m_timelineFlipLabel = new QLabel(tr("Flip:"), m_selectedTimelineGroup);
    m_timelineFlipLabel->setVisible(false);
    flipRow->addWidget(m_timelineFlipLabel);
    m_timelineFlipCombo = new QComboBox(m_selectedTimelineGroup);
    m_timelineFlipCombo->addItem(tr("None"),       0);
    m_timelineFlipCombo->addItem(tr("Horizontal"), 1);
    m_timelineFlipCombo->addItem(tr("Vertical"),   2);
    m_timelineFlipCombo->addItem(tr("Both"),       3);
    m_timelineFlipCombo->setVisible(false);
    flipRow->addWidget(m_timelineFlipCombo);
    flipRow->addStretch();
    groupLayout->addLayout(flipRow);

    // Timeline drop area (frames list)
    m_timelineDropArea = new QWidget(m_timelineEditorContainer);
    auto* dropLayout   = new QVBoxLayout(m_timelineDropArea);
    dropLayout->setContentsMargins(0, 4, 0, 0);
    m_timelineDragHintLabel = new QLabel(tr("Drag frames from layout canvas here"),
                                         m_timelineDropArea);
    dropLayout->addWidget(m_timelineDragHintLabel);
    m_timelineFramesList = new TimelineListWidget(m_timelineDropArea);
    m_timelineFramesList->setViewMode(QListWidget::IconMode);
    m_timelineFramesList->setFlow(QListWidget::LeftToRight);
    m_timelineFramesList->setWrapping(false);
    m_timelineFramesList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_timelineFramesList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_timelineFramesList->setResizeMode(QListWidget::Adjust);
    m_timelineFramesList->setIconSize(QSize(48, 48));
    m_timelineFramesList->setFixedHeight(96);
    dropLayout->addWidget(m_timelineFramesList);
    editorContainerLayout->addWidget(m_timelineDropArea);

    // ── Wire internal signals to slots
    connect(m_addBtn, &QPushButton::clicked, this, &TimelineEditorPanel::onTimelineAddClicked);
    connect(m_timelineCreateEdit, &QLineEdit::returnPressed, this, &TimelineEditorPanel::onTimelineAddClicked);
    connect(m_removeBtn, &QPushButton::clicked, this, &TimelineEditorPanel::onTimelineRemoveClicked);
    connect(m_timelineList, &QTreeWidget::itemSelectionChanged, this, &TimelineEditorPanel::onTimelineSelectionChanged);
    connect(m_timelineList, &QWidget::customContextMenuRequested, this, &TimelineEditorPanel::onTimelineContextMenu);
    connect(m_timelineList, &TimelineTreeWidget::deleteKeyPressed, this, &TimelineEditorPanel::onTimelineDeleteKey);
    connect(m_timelineList, &TimelineTreeWidget::dropCompleted, this, &TimelineEditorPanel::onTimelineTreeDropCompleted);
    connect(m_timelineList, &TimelineTreeWidget::spritesDroppedToCreate, this,
        [this](const QStringList& p, const QString& f){ emit spritesToTimelineRequested(p, f); });
    connect(m_timelineList, &QTreeWidget::itemChanged, this, &TimelineEditorPanel::onTimelineItemChanged);
    connect(m_timelineNameEdit, &QLineEdit::editingFinished, this, &TimelineEditorPanel::onTimelineNameChanged);
    connect(m_timelineFpsSpin, &QSpinBox::valueChanged, this, &TimelineEditorPanel::onTimelineFpsChanged);
    connect(m_timelineFlipCombo, &QComboBox::currentIndexChanged, this, &TimelineEditorPanel::onTimelineFlipChanged);
    connect(m_timelineFramesList, &TimelineListWidget::frameDropped, this, &TimelineEditorPanel::onFrameDropped);
    connect(m_timelineFramesList, &TimelineListWidget::frameMoved, this, &TimelineEditorPanel::onFrameMoved);
    connect(m_timelineFramesList, &TimelineListWidget::removeSelectedRequested, this, &TimelineEditorPanel::onFrameRemoveRequested);
    connect(m_timelineFramesList, &TimelineListWidget::duplicateFrameRequested, this, &TimelineEditorPanel::onFrameDuplicateRequested);
    connect(m_timelineFramesList, &QListWidget::itemSelectionChanged, this, &TimelineEditorPanel::onTimelineFrameSelectionChanged);
}

// ---------------------------------------------------------------------------
// Private static helper: compute folder path for a folder node by walking
// parent chain and collecting non-leaf text segments.
// ---------------------------------------------------------------------------
static QString timelineItemFolderPath(QTreeWidgetItem* item)
{
    if (!item) return QString();
    QStringList parts;
    QTreeWidgetItem* cur = item;
    while (cur) {
        if (!cur->data(0, Qt::UserRole).isValid())
            parts.prepend(cur->text(0));
        cur = cur->parent();
    }
    return parts.join('/');
}

// ---------------------------------------------------------------------------
// Private static helper: collect indices of all checked leaf timeline items
// ---------------------------------------------------------------------------
static QVector<int> collectCheckedTimelineIndices(QTreeWidget* tree)
{
    QVector<int> result;
    QTreeWidgetItemIterator it(tree);
    while (*it) {
        const QVariant v = (*it)->data(0, Qt::UserRole);
        if (v.isValid() && (*it)->checkState(0) == Qt::Checked)
            result.append(v.toInt());
        ++it;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Private static helpers: checkbox state propagation
// ---------------------------------------------------------------------------
static void setCheckStateRecursive(QTreeWidgetItem* item, Qt::CheckState state)
{
    item->setCheckState(0, state);
    for (int i = 0; i < item->childCount(); ++i)
        setCheckStateRecursive(item->child(i), state);
}

static void updateParentCheckState(QTreeWidgetItem* parent)
{
    if (!parent) return;
    if (parent->data(0, Qt::UserRole).isValid()) return;

    int checked = 0, unchecked = 0;
    for (int i = 0; i < parent->childCount(); ++i) {
        Qt::CheckState cs = parent->child(i)->checkState(0);
        if (cs == Qt::Checked)        ++checked;
        else if (cs == Qt::Unchecked) ++unchecked;
        else { ++checked; ++unchecked; }
    }

    Qt::CheckState newState;
    if (checked == 0)       newState = Qt::Unchecked;
    else if (unchecked == 0) newState = Qt::Checked;
    else                    newState = Qt::PartiallyChecked;

    parent->setCheckState(0, newState);
    updateParentCheckState(parent->parent());
}

// ---------------------------------------------------------------------------
// timelineItemForIndex
// ---------------------------------------------------------------------------
QTreeWidgetItem* TimelineEditorPanel::timelineItemForIndex(int idx) const
{
    QTreeWidgetItemIterator it(m_timelineList);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).isValid() &&
            (*it)->data(0, Qt::UserRole).toInt() == idx) return *it;
        ++it;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// refreshAndSelect (private helper)
// ---------------------------------------------------------------------------
void TimelineEditorPanel::refreshAndSelect()
{
    refreshTimelineList();
    if (m_session->selectedTimelineIndex >= 0)
        m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));
    else
        onTimelineSelectionChanged();
    refreshTimelineFrames();
    emit animationDataChanged();
}

// ---------------------------------------------------------------------------
// selectTimeline (public)
// ---------------------------------------------------------------------------
void TimelineEditorPanel::selectTimeline(int index)
{
    m_session->selectedTimelineIndex = index;
    // refreshTimelineList() calls onTimelineSelectionChanged() at the end which
    // triggers refreshTimelineFrames() — covering all UI updates.
    refreshTimelineList();
}

// ---------------------------------------------------------------------------
// refreshTimelineList
// ---------------------------------------------------------------------------
void TimelineEditorPanel::refreshTimelineList()
{
    const int savedIndex = m_session->selectedTimelineIndex;

    const bool hadItems = m_timelineList->invisibleRootItem()->childCount() > 0;
    QSet<QString> collapsedFolders;
    QSet<int>     checkedIndices;
    int           scrollPos = 0;
    if (hadItems) {
        QTreeWidgetItemIterator sit(m_timelineList);
        while (*sit) {
            const bool isFolder = !(*sit)->data(0, Qt::UserRole).isValid();
            if (isFolder) {
                if (!(*sit)->isExpanded())
                    collapsedFolders.insert(timelineItemFolderPath(*sit));
            } else {
                if ((*sit)->checkState(0) == Qt::Checked)
                    checkedIndices.insert((*sit)->data(0, Qt::UserRole).toInt());
            }
            ++sit;
        }
        scrollPos = m_timelineList->verticalScrollBar()->value();
    }

    m_timelineList->blockSignals(true);
    m_timelineList->clear();

    const int count = m_session->activeAtlas().timelines.size();
    QVector<int> sortedIndices(count);
    std::iota(sortedIndices.begin(), sortedIndices.end(), 0);
    std::sort(sortedIndices.begin(), sortedIndices.end(), [&](int a, int b) {
        return m_session->activeAtlas().timelines[a].name.compare(
            m_session->activeAtlas().timelines[b].name, Qt::CaseInsensitive) < 0;
    });

    const QIcon folderIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);

    auto findOrCreateFolderPath = [&](const QStringList& parts) -> QTreeWidgetItem* {
        QTreeWidgetItem* current = nullptr;
        for (int pi = 0; pi < parts.size(); ++pi) {
            const QString& part = parts[pi];
            QTreeWidgetItem* found = nullptr;
            int childCount = current ? current->childCount() : m_timelineList->topLevelItemCount();
            for (int i = 0; i < childCount; ++i) {
                QTreeWidgetItem* child = current ? current->child(i) : m_timelineList->topLevelItem(i);
                if (child->text(0) == part && !child->data(0, Qt::UserRole).isValid()) {
                    found = child;
                    break;
                }
            }
            if (!found) {
                found = current ? new QTreeWidgetItem(current) : new QTreeWidgetItem(m_timelineList);
                found->setText(0, part);
                found->setIcon(0, folderIcon);
                found->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                found->setCheckState(0, Qt::Unchecked);
                found->setToolTip(0, parts.mid(0, pi + 1).join('/'));
            }
            current = found;
        }
        return current;
    };

    for (int idx : sortedIndices) {
        const auto& timeline = m_session->activeAtlas().timelines[idx];

        QStringList parts = timeline.name.split('/');
        const QString leafName = parts.last();
        parts.removeLast();

        QTreeWidgetItem* parent = nullptr;
        if (!parts.isEmpty()) {
            parent = findOrCreateFolderPath(parts);
        }

        const QStringList* displayFrames = &timeline.frames;
        if (!timeline.aliasOf.isEmpty()) {
            for (const auto& src : m_session->activeAtlas().timelines) {
                if (src.name == timeline.aliasOf && src.aliasOf.isEmpty()) {
                    displayFrames = &src.frames;
                    break;
                }
            }
        }

        QIcon icon;
        if (!displayFrames->isEmpty()) {
            int middleIndex = displayFrames->size() / 2;
            const QString middlePath = (*displayFrames)[middleIndex];
            icon = m_timelineListIconCache.value(middlePath);
            if (icon.isNull()) {
                icon = QIcon(middlePath);
                m_timelineListIconCache.insert(middlePath, icon);
            }
        }

        const QString displayName = timeline.aliasOf.isEmpty()
            ? leafName
            : QStringLiteral("[alias] %1").arg(leafName);

        QTreeWidgetItem* item = parent
            ? new QTreeWidgetItem(parent)
            : new QTreeWidgetItem(m_timelineList);
        item->setIcon(0, icon);
        item->setText(0, QStringLiteral("%1 | %2 frames | %3 fps")
            .arg(displayName)
            .arg(displayFrames->size())
            .arg(timeline.fps));
        item->setData(0, Qt::UserRole, idx);
        item->setToolTip(0, timeline.name);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Unchecked);
    }

    {
        QTreeWidgetItemIterator rit(m_timelineList);
        while (*rit) {
            if (!(*rit)->data(0, Qt::UserRole).isValid()) {
                const QString fp = timelineItemFolderPath(*rit);
                (*rit)->setExpanded(!collapsedFolders.contains(fp));
            }
            ++rit;
        }
    }

    m_timelineList->setCurrentItem(timelineItemForIndex(savedIndex));
    m_timelineList->blockSignals(false);

    if (!checkedIndices.isEmpty()) {
        QTreeWidgetItemIterator rit(m_timelineList);
        while (*rit) {
            const QVariant v = (*rit)->data(0, Qt::UserRole);
            if (v.isValid() && checkedIndices.contains(v.toInt()))
                (*rit)->setCheckState(0, Qt::Checked);
            ++rit;
        }
    }
    if (hadItems)
        m_timelineList->verticalScrollBar()->setValue(scrollPos);

    // setCurrentItem() was called with signals blocked, so explicitly fire
    // onTimelineSelectionChanged so the editor container stays in sync.
    onTimelineSelectionChanged();
}

// ---------------------------------------------------------------------------
// refreshTimelineFrames
// ---------------------------------------------------------------------------
void TimelineEditorPanel::refreshTimelineFrames()
{
    if (m_timelineFrameIconCache.size() > 4096) {
        auto it = m_timelineFrameIconCache.begin();
        int toRemove = m_timelineFrameIconCache.size() / 2;
        while (toRemove > 0 && it != m_timelineFrameIconCache.end()) {
            it = m_timelineFrameIconCache.erase(it);
            --toRemove;
        }
    }
    m_timelineFramesList->setUpdatesEnabled(false);
    m_timelineFramesList->clear();
    if (m_session->selectedTimelineIndex < 0
            || m_session->selectedTimelineIndex >= m_session->activeAtlas().timelines.size()) {
        m_timelineFramesList->setUpdatesEnabled(true);
        return;
    }

    const auto& timeline = m_session->activeAtlas().timelines[m_session->selectedTimelineIndex];

    const QStringList* framesToShow = &timeline.frames;
    if (!timeline.aliasOf.isEmpty()) {
        for (const auto& src : m_session->activeAtlas().timelines) {
            if (src.name == timeline.aliasOf && src.aliasOf.isEmpty()) {
                framesToShow = &src.frames;
                break;
            }
        }
    }
    m_timelineDragHintLabel->setVisible(framesToShow->isEmpty() && timeline.aliasOf.isEmpty());
    for (const QString& path : *framesToShow) {
        QFileInfo fi(path);
        QIcon icon = m_timelineFrameIconCache.value(path);
        if (icon.isNull()) {
            icon = QIcon(path);
            m_timelineFrameIconCache.insert(path, icon);
        }
        QListWidgetItem* item = new QListWidgetItem(icon, fi.baseName());
        item->setToolTip(path);
        m_timelineFramesList->addItem(item);
    }
    m_timelineFramesList->setUpdatesEnabled(true);
}

// ---------------------------------------------------------------------------
// onTimelineAddClicked
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onTimelineAddClicked()
{
    QString name = m_timelineCreateEdit->text().trimmed();
    if (name.isEmpty()) {
        name = QString("Timeline %1").arg(m_session->activeAtlas().timelines.size() + 1);
    }

    AnimationTimeline timeline;
    timeline.name = name;
    m_session->activeAtlas().timelines.append(timeline);
    m_session->selectedTimelineIndex = m_session->activeAtlas().timelines.size() - 1;

    m_timelineCreateEdit->clear();
    refreshTimelineList();
    m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));

    m_undoStack->push(new TimelineAddCommand(
        &m_session->activeAtlas().timelines,
        timeline,
        &m_session->selectedTimelineIndex,
        [this]() { refreshAndSelect(); }
    ));
}

// ---------------------------------------------------------------------------
// onTimelineRemoveClicked
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onTimelineRemoveClicked()
{
    if (m_session->selectedTimelineIndex < 0
            || m_session->selectedTimelineIndex >= m_session->activeAtlas().timelines.size()) {
        return;
    }
    const int removeIdx = m_session->selectedTimelineIndex;
    const AnimationTimeline savedTimeline = m_session->activeAtlas().timelines[removeIdx];

    m_session->activeAtlas().timelines.removeAt(removeIdx);
    m_session->selectedTimelineIndex = qMin(removeIdx, m_session->activeAtlas().timelines.size() - 1);

    refreshTimelineList();
    if (m_session->selectedTimelineIndex >= 0) {
        m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));
    } else {
        onTimelineSelectionChanged();
    }

    m_undoStack->push(new TimelineRemoveCommand(
        &m_session->activeAtlas().timelines,
        removeIdx,
        savedTimeline,
        &m_session->selectedTimelineIndex,
        [this]() { refreshAndSelect(); }
    ));
}

// ---------------------------------------------------------------------------
// onTimelineSelectionChanged
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onTimelineSelectionChanged()
{
    QTreeWidgetItem* item = m_timelineList->currentItem();
    if (item && !item->data(0, Qt::UserRole).isValid()) {
        return;
    }
    const int idx = (item && item->data(0, Qt::UserRole).isValid())
        ? item->data(0, Qt::UserRole).toInt() : -1;
    if (idx >= 0 && idx < m_session->activeAtlas().timelines.size()) {
        m_session->selectedTimelineIndex = idx;
        const auto& tl = m_session->activeAtlas().timelines[idx];
        const bool isAlias = !tl.aliasOf.isEmpty();

        m_timelineNameEdit->setText(tl.name);
        m_timelineNameEdit->setEnabled(true);

        if (m_timelineAliasLabel) {
            m_timelineAliasLabel->setVisible(isAlias);
            if (isAlias)
                m_timelineAliasLabel->setText(tr("Alias of: %1").arg(tl.aliasOf));
        }

        if (m_timelineFlipLabel) m_timelineFlipLabel->setVisible(isAlias);
        if (m_timelineFlipCombo) {
            m_timelineFlipCombo->setVisible(isAlias);
            if (isAlias) {
                const QSignalBlocker b(m_timelineFlipCombo);
                int flipIdx = (tl.hFlip ? 1 : 0) + (tl.vFlip ? 2 : 0);
                m_timelineFlipCombo->setCurrentIndex(flipIdx);
            }
        }

        int fpsToShow = tl.fps;
        if (isAlias) {
            for (const auto& src : m_session->activeAtlas().timelines) {
                if (src.name == tl.aliasOf && src.aliasOf.isEmpty()) {
                    fpsToShow = src.fps;
                    break;
                }
            }
        }
        {
            const QSignalBlocker blocker(m_timelineFpsSpin);
            m_timelineFpsSpin->setValue(fpsToShow);
        }
        m_timelineFpsSpin->setEnabled(!isAlias);

        emit animPlaybackIntervalChanged(fpsToShow);

        m_timelineFramesList->setReadOnly(isAlias);
        m_timelineDragHintLabel->setText(tr("Drag frames from layout canvas here"));
        m_removeBtn->setEnabled(true);
        refreshTimelineFrames();
        emit animFrameReset();
        return;
    }
    m_session->selectedTimelineIndex = -1;
    m_timelineNameEdit->clear();
    m_timelineNameEdit->setEnabled(false);
    {
        const QSignalBlocker blocker(m_timelineFpsSpin);
        m_timelineFpsSpin->setValue(8);
    }
    m_timelineFpsSpin->setEnabled(false);
    if (m_timelineAliasLabel) m_timelineAliasLabel->setVisible(false);
    if (m_timelineFlipLabel)  m_timelineFlipLabel->setVisible(false);
    if (m_timelineFlipCombo)  m_timelineFlipCombo->setVisible(false);
    m_removeBtn->setEnabled(false);
    m_timelineDragHintLabel->setText(tr("Drop sprites or a group here to create a new timeline"));
    m_timelineDragHintLabel->setVisible(true);
    m_timelineFramesList->clear();
    m_timelineFramesList->setReadOnly(false);
    emit animationDataChanged();
}

// ---------------------------------------------------------------------------
// onTimelineNameChanged
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onTimelineNameChanged()
{
    const int idx = m_session->selectedTimelineIndex;
    if (idx < 0 || idx >= m_session->activeAtlas().timelines.size()) return;
    const QString oldName = m_session->activeAtlas().timelines[idx].name;
    const QString newName = m_timelineNameEdit->text();
    if (oldName == newName) return;
    m_session->activeAtlas().timelines[idx].name = newName;
    for (auto& tl : m_session->activeAtlas().timelines) {
        if (tl.aliasOf == oldName) tl.aliasOf = newName;
    }
    refreshTimelineList();
    m_undoStack->push(new SetTimelineNameCommand(
        &m_session->activeAtlas().timelines, idx, oldName, newName,
        [this, idx]() {
            refreshTimelineList();
            if (m_session->selectedTimelineIndex == idx
                    && idx >= 0 && idx < m_session->activeAtlas().timelines.size()) {
                const QSignalBlocker blocker(m_timelineNameEdit);
                m_timelineNameEdit->setText(m_session->activeAtlas().timelines[idx].name);
            }
        }
    ));
}

// ---------------------------------------------------------------------------
// onTimelineFpsChanged
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onTimelineFpsChanged(int fps)
{
    const int idx = m_session->selectedTimelineIndex;
    if (idx < 0 || idx >= m_session->activeAtlas().timelines.size()) return;
    const int oldFps = m_session->activeAtlas().timelines[idx].fps;
    if (oldFps == fps) return;
    m_session->activeAtlas().timelines[idx].fps = fps;
    emit animPlaybackIntervalChanged(fps);
    refreshTimelineList();
    emit animationDataChanged();
    m_undoStack->push(new SetTimelineFpsCommand(
        &m_session->activeAtlas().timelines, idx, oldFps, fps,
        [this, idx]() {
            if (m_session->selectedTimelineIndex == idx
                    && idx >= 0 && idx < m_session->activeAtlas().timelines.size()) {
                const int curFps = m_session->activeAtlas().timelines[idx].fps;
                {
                    const QSignalBlocker blocker(m_timelineFpsSpin);
                    m_timelineFpsSpin->setValue(curFps);
                }
                emit animPlaybackIntervalChanged(curFps);
            }
            refreshTimelineList();
            emit animationDataChanged();
        }
    ));
}

// ---------------------------------------------------------------------------
// onTimelineCreateAlias
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onTimelineCreateAlias()
{
    const int idx = m_session->selectedTimelineIndex;
    if (idx < 0 || idx >= m_session->activeAtlas().timelines.size()) return;
    const auto& source = m_session->activeAtlas().timelines[idx];
    if (!source.aliasOf.isEmpty()) return;

    const QString sourceName = source.name;

    AnimationTimeline alias;
    alias.name    = sourceName + "_alias";
    alias.aliasOf = sourceName;
    alias.fps     = source.fps;

    m_session->activeAtlas().timelines.append(alias);
    m_session->selectedTimelineIndex = m_session->activeAtlas().timelines.size() - 1;

    refreshTimelineList();
    m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));

    m_undoStack->push(new TimelineAddCommand(
        &m_session->activeAtlas().timelines,
        alias,
        &m_session->selectedTimelineIndex,
        [this]() { refreshAndSelect(); }
    ));
}

// ---------------------------------------------------------------------------
// onTimelineFlipChanged
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onTimelineFlipChanged(int index)
{
    const int idx = m_session->selectedTimelineIndex;
    if (idx < 0 || idx >= m_session->activeAtlas().timelines.size()) return;
    auto& tl = m_session->activeAtlas().timelines[idx];
    const bool oldH = tl.hFlip;
    const bool oldV = tl.vFlip;
    const bool newH = (index == 1 || index == 3);
    const bool newV = (index == 2 || index == 3);
    if (oldH == newH && oldV == newV) return;
    tl.hFlip = newH;
    tl.vFlip = newV;
    emit animationDataChanged();
    m_undoStack->push(new SetTimelineFlipCommand(
        &m_session->activeAtlas().timelines, idx, oldH, oldV, newH, newV,
        [this]() { emit animationDataChanged(); }
    ));
}

// ---------------------------------------------------------------------------
// onTimelineItemChanged
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onTimelineItemChanged(QTreeWidgetItem* item, int column)
{
    if (column != 0) return;
    QSignalBlocker blocker(m_timelineList);
    if (!item->data(0, Qt::UserRole).isValid()) {
        Qt::CheckState state = item->checkState(0);
        if (state != Qt::PartiallyChecked)
            setCheckStateRecursive(item, state);
    } else {
        updateParentCheckState(item->parent());
    }
}

// ---------------------------------------------------------------------------
// onTimelineContextMenu
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onTimelineContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_timelineList->itemAt(pos);
    const QVector<int> checkedIndices = collectCheckedTimelineIndices(m_timelineList);
    const bool hasChecked = !checkedIndices.isEmpty();

    auto refreshAndSelectLambda = [this]() { refreshAndSelect(); };

    QMenu menu(this);
    bool hasItems = false;

    if (item && !item->data(0, Qt::UserRole).isValid()) {
        // Folder node
        const QString folderPath = timelineItemFolderPath(item);
        const int lastSlash = folderPath.lastIndexOf('/');
        const bool hasParent = lastSlash >= 0;

        if (hasParent) {
            menu.addAction(tr("Ungroup"), this, [this, folderPath, refreshAndSelectLambda]() {
                const QVector<AnimationTimeline> oldTimelines = m_session->activeAtlas().timelines;
                const int oldSel = m_session->selectedTimelineIndex;

                const int ls            = folderPath.lastIndexOf('/');
                const QString folderName    = folderPath.mid(ls + 1);
                const QString parentPath    = folderPath.left(ls);
                const int grandparentSlash  = parentPath.lastIndexOf('/');
                const QString grandparentPath = (grandparentSlash < 0) ? QString()
                                                : parentPath.left(grandparentSlash);
                const QString oldPrefix = folderPath + "/";
                const QString newPrefix = (grandparentPath.isEmpty() ? QString()
                                                                      : grandparentPath + "/")
                                          + folderName + "/";

                for (const auto& tl : m_session->activeAtlas().timelines) {
                    if (tl.name.startsWith(oldPrefix)) {
                        const QString newName = newPrefix + tl.name.mid(oldPrefix.length());
                        for (const auto& other : m_session->activeAtlas().timelines) {
                            if (other.name == newName && !other.name.startsWith(oldPrefix)) {
                                MessageDialog::warning(this, tr("Ungroup"),
                                    tr("Cannot ungroup: \"%1\" would conflict with an existing timeline.").arg(newName));
                                return;
                            }
                        }
                    }
                }

                for (auto& tl : m_session->activeAtlas().timelines) {
                    if (tl.name.startsWith(oldPrefix)) {
                        const QString oldName = tl.name;
                        tl.name = newPrefix + tl.name.mid(oldPrefix.length());
                        for (auto& other : m_session->activeAtlas().timelines) {
                            if (other.aliasOf == oldName)
                                other.aliasOf = tl.name;
                        }
                    }
                }

                m_undoStack->push(new TimelinesUpdateCommand(
                    &m_session->activeAtlas().timelines, oldTimelines, m_session->activeAtlas().timelines,
                    oldSel, m_session->selectedTimelineIndex,
                    &m_session->selectedTimelineIndex,
                    refreshAndSelectLambda, tr("Ungroup")));
                refreshAndSelectLambda();
            });
        }

        QMenu* removeMenu = menu.addMenu(tr("Remove..."));

        removeMenu->addAction(tr("and descendants"), this, [this, folderPath, refreshAndSelectLambda]() {
            const QVector<AnimationTimeline> oldTimelines = m_session->activeAtlas().timelines;
            const int oldSel = m_session->selectedTimelineIndex;

            const QString prefix = folderPath + "/";
            QVector<QString> toRemove;
            for (const auto& tl : m_session->activeAtlas().timelines) {
                if (tl.name.startsWith(prefix))
                    toRemove.append(tl.name);
            }
            const QSet<QString> removeSet(toRemove.begin(), toRemove.end());
            m_session->activeAtlas().timelines.erase(
                std::remove_if(m_session->activeAtlas().timelines.begin(), m_session->activeAtlas().timelines.end(),
                    [&removeSet](const AnimationTimeline& tl) {
                        return removeSet.contains(tl.name) || removeSet.contains(tl.aliasOf);
                    }),
                m_session->activeAtlas().timelines.end());
            m_session->selectedTimelineIndex = qMin(oldSel, m_session->activeAtlas().timelines.size() - 1);

            m_undoStack->push(new TimelinesUpdateCommand(
                &m_session->activeAtlas().timelines, oldTimelines, m_session->activeAtlas().timelines,
                oldSel, m_session->selectedTimelineIndex,
                &m_session->selectedTimelineIndex,
                refreshAndSelectLambda, tr("Remove Group")));
            refreshAndSelectLambda();
        });

        removeMenu->addAction(tr("keep descendants"), this, [this, folderPath, refreshAndSelectLambda]() {
            const QVector<AnimationTimeline> oldTimelines = m_session->activeAtlas().timelines;
            const int oldSel = m_session->selectedTimelineIndex;

            const QString oldPrefix = folderPath + "/";
            const int ls2 = folderPath.lastIndexOf('/');
            const QString parentPath2 = (ls2 < 0) ? QString() : folderPath.left(ls2);
            const QString newPrefix = parentPath2.isEmpty() ? QString() : parentPath2 + "/";

            for (const auto& tl : m_session->activeAtlas().timelines) {
                if (tl.name.startsWith(oldPrefix)) {
                    const QString newName = newPrefix + tl.name.mid(oldPrefix.length());
                    for (const auto& other : m_session->activeAtlas().timelines) {
                        if (other.name == newName && !other.name.startsWith(oldPrefix)) {
                            MessageDialog::warning(this, tr("Remove Group"),
                                tr("Cannot dissolve: \"%1\" would conflict with an existing timeline.").arg(newName));
                            return;
                        }
                    }
                }
            }

            for (auto& tl : m_session->activeAtlas().timelines) {
                if (tl.name.startsWith(oldPrefix)) {
                    const QString oldName = tl.name;
                    tl.name = newPrefix + tl.name.mid(oldPrefix.length());
                    for (auto& other : m_session->activeAtlas().timelines) {
                        if (other.aliasOf == oldName)
                            other.aliasOf = tl.name;
                    }
                }
            }

            m_undoStack->push(new TimelinesUpdateCommand(
                &m_session->activeAtlas().timelines, oldTimelines, m_session->activeAtlas().timelines,
                oldSel, m_session->selectedTimelineIndex,
                &m_session->selectedTimelineIndex,
                refreshAndSelectLambda, tr("Remove Group (keep descendants)")));
            refreshAndSelectLambda();
        });

        menu.addSeparator();

        menu.addAction(tr("Add Timeline Into Group"), this, [this, folderPath, refreshAndSelectLambda]() {
            bool ok = false;
            const QString suggested = folderPath + "/";
            const QString name = QInputDialog::getText(this, tr("Add Timeline Into Group"),
                tr("Timeline name:"), QLineEdit::Normal, suggested, &ok);
            if (!ok || name.trimmed().isEmpty()) return;
            const QString trimmed = name.trimmed();
            if (hasDuplicateTimelineName(trimmed)) {
                MessageDialog::warning(this, tr("Add Timeline"),
                    tr("A timeline named \"%1\" already exists.").arg(trimmed));
                return;
            }

            AnimationTimeline tl;
            tl.name = trimmed;
            m_session->activeAtlas().timelines.append(tl);
            m_session->selectedTimelineIndex = m_session->activeAtlas().timelines.size() - 1;

            m_undoStack->push(new TimelineAddCommand(
                &m_session->activeAtlas().timelines, tl,
                &m_session->selectedTimelineIndex,
                refreshAndSelectLambda));
            refreshAndSelectLambda();
        });

        hasItems = true;
    } else if (item && item->data(0, Qt::UserRole).isValid()) {
        // Leaf node
        const int idx = item->data(0, Qt::UserRole).toInt();
        m_timelineList->setCurrentItem(item);

        menu.addAction(tr("Remove Timeline"), this, [this, idx, refreshAndSelectLambda]() {
            if (idx < 0 || idx >= m_session->activeAtlas().timelines.size()) return;
            const QVector<AnimationTimeline> oldTimelines = m_session->activeAtlas().timelines;
            const int oldSel = m_session->selectedTimelineIndex;
            const QString name = m_session->activeAtlas().timelines[idx].name;

            m_session->activeAtlas().timelines.erase(
                std::remove_if(m_session->activeAtlas().timelines.begin(), m_session->activeAtlas().timelines.end(),
                    [&name](const AnimationTimeline& tl) {
                        return tl.name == name || tl.aliasOf == name;
                    }),
                m_session->activeAtlas().timelines.end());
            m_session->selectedTimelineIndex = qMin(oldSel, m_session->activeAtlas().timelines.size() - 1);

            m_undoStack->push(new TimelinesUpdateCommand(
                &m_session->activeAtlas().timelines, oldTimelines, m_session->activeAtlas().timelines,
                oldSel, m_session->selectedTimelineIndex,
                &m_session->selectedTimelineIndex,
                refreshAndSelectLambda, tr("Remove Timeline")));
            refreshAndSelectLambda();
        });

        menu.addAction(tr("Create Alias"), this, &TimelineEditorPanel::onTimelineCreateAlias);
        hasItems = true;
    }

    if (hasChecked) {
        if (hasItems) menu.addSeparator();
        menu.addAction(tr("Remove Selected"), this, [this, checkedIndices, refreshAndSelectLambda]() {
            const QVector<AnimationTimeline> oldTimelines = m_session->activeAtlas().timelines;
            const int oldSel = m_session->selectedTimelineIndex;

            QVector<int> sorted = checkedIndices;
            std::sort(sorted.begin(), sorted.end(), std::greater<int>());

            QSet<QString> removedNames;
            for (int i : sorted) {
                if (i >= 0 && i < m_session->activeAtlas().timelines.size()) {
                    removedNames.insert(m_session->activeAtlas().timelines[i].name);
                }
            }
            m_session->activeAtlas().timelines.erase(
                std::remove_if(m_session->activeAtlas().timelines.begin(), m_session->activeAtlas().timelines.end(),
                    [&removedNames](const AnimationTimeline& tl) {
                        return removedNames.contains(tl.name) || removedNames.contains(tl.aliasOf);
                    }),
                m_session->activeAtlas().timelines.end());
            m_session->selectedTimelineIndex = qMin(oldSel, m_session->activeAtlas().timelines.size() - 1);

            m_undoStack->push(new TimelinesUpdateCommand(
                &m_session->activeAtlas().timelines, oldTimelines, m_session->activeAtlas().timelines,
                oldSel, m_session->selectedTimelineIndex,
                &m_session->selectedTimelineIndex,
                refreshAndSelectLambda, tr("Remove Selected Timelines")));
            refreshAndSelectLambda();
        });
    }

    if (!item && !hasChecked) {
        QMenu emptyMenu(this);
        emptyMenu.addAction(tr("Add Timeline"), this, &TimelineEditorPanel::onTimelineAddClicked);
        emptyMenu.exec(m_timelineList->mapToGlobal(pos));
        return;
    }

    if (!menu.isEmpty())
        menu.exec(m_timelineList->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// onTimelineDeleteKey
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onTimelineDeleteKey()
{
    QTreeWidgetItem* item = m_timelineList->currentItem();
    if (!item) return;

    auto refreshAndSelectLambda = [this]() { refreshAndSelect(); };

    if (!item->data(0, Qt::UserRole).isValid()) {
        // Folder node
        const QString folderPath = timelineItemFolderPath(item);
        const QVector<AnimationTimeline> oldTimelines = m_session->activeAtlas().timelines;
        const int oldSel = m_session->selectedTimelineIndex;

        const QString prefix = folderPath + "/";
        QSet<QString> removedNames;
        for (const auto& tl : m_session->activeAtlas().timelines) {
            if (tl.name.startsWith(prefix))
                removedNames.insert(tl.name);
        }
        m_session->activeAtlas().timelines.erase(
            std::remove_if(m_session->activeAtlas().timelines.begin(), m_session->activeAtlas().timelines.end(),
                [&removedNames](const AnimationTimeline& tl) {
                    return removedNames.contains(tl.name) || removedNames.contains(tl.aliasOf);
                }),
            m_session->activeAtlas().timelines.end());
        m_session->selectedTimelineIndex = qMin(oldSel, m_session->activeAtlas().timelines.size() - 1);

        m_undoStack->push(new TimelinesUpdateCommand(
            &m_session->activeAtlas().timelines, oldTimelines, m_session->activeAtlas().timelines,
            oldSel, m_session->selectedTimelineIndex,
            &m_session->selectedTimelineIndex,
            refreshAndSelectLambda, tr("Remove Group")));
        refreshAndSelectLambda();
    } else {
        // Leaf node
        const int idx = item->data(0, Qt::UserRole).toInt();
        if (idx < 0 || idx >= m_session->activeAtlas().timelines.size()) return;

        const QVector<AnimationTimeline> oldTimelines = m_session->activeAtlas().timelines;
        const int oldSel = m_session->selectedTimelineIndex;
        const QString name = m_session->activeAtlas().timelines[idx].name;

        m_session->activeAtlas().timelines.erase(
            std::remove_if(m_session->activeAtlas().timelines.begin(), m_session->activeAtlas().timelines.end(),
                [&name](const AnimationTimeline& tl) {
                    return tl.name == name || tl.aliasOf == name;
                }),
            m_session->activeAtlas().timelines.end());
        m_session->selectedTimelineIndex = qMin(oldSel, m_session->activeAtlas().timelines.size() - 1);

        m_undoStack->push(new TimelinesUpdateCommand(
            &m_session->activeAtlas().timelines, oldTimelines, m_session->activeAtlas().timelines,
            oldSel, m_session->selectedTimelineIndex,
            &m_session->selectedTimelineIndex,
            refreshAndSelectLambda, tr("Remove Timeline")));
        refreshAndSelectLambda();
    }
}

// ---------------------------------------------------------------------------
// onTimelineTreeDropCompleted
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onTimelineTreeDropCompleted(int draggedIndex,
                                                       const QString& draggedFolder,
                                                       const QString& targetFolder)
{
    auto refreshAndSelectLambda = [this]() { refreshAndSelect(); };

    const QVector<AnimationTimeline> oldTimelines = m_session->activeAtlas().timelines;
    const int oldSel = m_session->selectedTimelineIndex;

    if (draggedIndex >= 0) {
        if (draggedIndex >= m_session->activeAtlas().timelines.size()) return;
        const QString oldName = m_session->activeAtlas().timelines[draggedIndex].name;
        const QString leafName = oldName.split('/').last();
        const QString newName = targetFolder.isEmpty() ? leafName : targetFolder + "/" + leafName;
        if (newName == oldName) return;

        for (const auto& tl : m_session->activeAtlas().timelines) {
            if (tl.name == newName) {
                MessageDialog::warning(this, tr("Move Timeline"),
                    tr("A timeline named \"%1\" already exists.").arg(newName));
                return;
            }
        }

        m_session->activeAtlas().timelines[draggedIndex].name = newName;
        for (auto& tl : m_session->activeAtlas().timelines) {
            if (tl.aliasOf == oldName)
                tl.aliasOf = newName;
        }
    } else {
        const QString lastSegment = draggedFolder.split('/').last();
        const QString oldPrefix = draggedFolder + "/";
        const QString newPrefix = targetFolder.isEmpty()
            ? lastSegment + "/"
            : targetFolder + "/" + lastSegment + "/";
        if (newPrefix == oldPrefix) return;

        for (const auto& tl : m_session->activeAtlas().timelines) {
            if (tl.name.startsWith(oldPrefix)) {
                const QString newName = newPrefix + tl.name.mid(oldPrefix.length());
                for (const auto& other : m_session->activeAtlas().timelines) {
                    if (other.name == newName && !other.name.startsWith(oldPrefix)) {
                        MessageDialog::warning(this, tr("Move Timeline"),
                            tr("Cannot move: \"%1\" would conflict with an existing timeline.").arg(newName));
                        return;
                    }
                }
            }
        }

        for (auto& tl : m_session->activeAtlas().timelines) {
            if (tl.name.startsWith(oldPrefix)) {
                const QString oldName = tl.name;
                tl.name = newPrefix + tl.name.mid(oldPrefix.length());
                for (auto& other : m_session->activeAtlas().timelines) {
                    if (other.aliasOf == oldName)
                        other.aliasOf = tl.name;
                }
            }
        }
    }

    m_undoStack->push(new TimelinesUpdateCommand(
        &m_session->activeAtlas().timelines, oldTimelines, m_session->activeAtlas().timelines,
        oldSel, m_session->selectedTimelineIndex,
        &m_session->selectedTimelineIndex,
        refreshAndSelectLambda, tr("Move Timeline")));
    refreshAndSelectLambda();
}

// ---------------------------------------------------------------------------
// onCreateTimelineFromDroppedPaths
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onCreateTimelineFromDroppedPaths()
{
    if (m_pendingCreateTimelinePaths.isEmpty()) return;
    QStringList paths;
    paths.swap(m_pendingCreateTimelinePaths);
    emit spritesToTimelineRequested(paths, "");
}

// ---------------------------------------------------------------------------
// onFrameDropped
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onFrameDropped(const QString& path, int index)
{
    if (m_session->selectedTimelineIndex < 0 ||
        m_session->selectedTimelineIndex >= m_session->activeAtlas().timelines.size()) {
        m_pendingCreateTimelinePaths.append(path);
        QTimer::singleShot(0, this, &TimelineEditorPanel::onCreateTimelineFromDroppedPaths);
        return;
    }
    if (!AnimationTimelineOps::dropFrame(m_session->activeAtlas().timelines,
                                          m_session->selectedTimelineIndex, path, index)) {
        return;
    }

    const auto& frames = m_session->activeAtlas().timelines[m_session->selectedTimelineIndex].frames;
    const int insertedIdx = (index < 0 || index >= (int)frames.size())
                                ? (int)frames.size() - 1
                                : index;

    refreshTimelineFrames();
    refreshTimelineList();
    emit animZoomResetAndFitRequested();

    m_undoStack->push(new TimelineFrameDropCommand(
        &m_session->activeAtlas().timelines,
        m_session->selectedTimelineIndex,
        path,
        insertedIdx,
        [this]() {
            refreshTimelineFrames();
            refreshTimelineList();
            emit animationDataChanged();
        }
    ));
}

// ---------------------------------------------------------------------------
// onFrameMoved
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onFrameMoved(int from, int to)
{
    if (m_session->selectedTimelineIndex < 0 ||
        m_session->selectedTimelineIndex >= m_session->activeAtlas().timelines.size()) {
        return;
    }
    QStringList savedFramesBefore =
        m_session->activeAtlas().timelines[m_session->selectedTimelineIndex].frames;

    if (!AnimationTimelineOps::moveFrame(m_session->activeAtlas().timelines,
                                          m_session->selectedTimelineIndex, from, to)) {
        return;
    }
    refreshTimelineFrames();
    refreshTimelineList();
    emit animationDataChanged();

    m_undoStack->push(new TimelineFrameMoveCommand(
        &m_session->activeAtlas().timelines,
        m_session->selectedTimelineIndex,
        from, to,
        savedFramesBefore,
        [this]() {
            refreshTimelineFrames();
            refreshTimelineList();
            emit animationDataChanged();
        }
    ));
}

// ---------------------------------------------------------------------------
// onFrameDuplicateRequested
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onFrameDuplicateRequested(int index)
{
    if (m_session->selectedTimelineIndex < 0 ||
        m_session->selectedTimelineIndex >= m_session->activeAtlas().timelines.size()) {
        return;
    }
    const auto& frames = m_session->activeAtlas().timelines[m_session->selectedTimelineIndex].frames;
    if (index < 0 || index >= frames.size()) return;
    const QString path = frames[index];

    if (!AnimationTimelineOps::duplicateFrame(m_session->activeAtlas().timelines,
                                               m_session->selectedTimelineIndex, index)) {
        return;
    }
    refreshTimelineFrames();
    refreshTimelineList();
    emit animationDataChanged();

    m_undoStack->push(new TimelineFrameDuplicateCommand(
        &m_session->activeAtlas().timelines,
        m_session->selectedTimelineIndex,
        index, path,
        [this]() {
            refreshTimelineFrames();
            refreshTimelineList();
            emit animationDataChanged();
        }
    ));
}

// ---------------------------------------------------------------------------
// onFrameRemoveRequested
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onFrameRemoveRequested()
{
    if (m_session->selectedTimelineIndex < 0 ||
        m_session->selectedTimelineIndex >= m_session->activeAtlas().timelines.size()) {
        return;
    }
    QList<QListWidgetItem*> items = m_timelineFramesList->selectedItems();
    if (items.isEmpty()) return;

    QVector<int> rows;
    for (auto* item : items) {
        rows.append(m_timelineFramesList->row(item));
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());

    const auto& frames = m_session->activeAtlas().timelines[m_session->selectedTimelineIndex].frames;
    QVector<QPair<int,QString>> removed;
    removed.reserve(rows.size());
    for (int i = rows.size() - 1; i >= 0; --i) {
        int row = rows[i];
        if (row >= 0 && row < frames.size()) {
            removed.append({row, frames[row]});
        }
    }

    AnimationTimelineOps::removeFrames(m_session->activeAtlas().timelines,
                                        m_session->selectedTimelineIndex, rows);
    refreshTimelineFrames();
    refreshTimelineList();
    emit animationDataChanged();

    if (!removed.isEmpty()) {
        m_undoStack->push(new TimelineFrameRemoveCommand(
            &m_session->activeAtlas().timelines,
            m_session->selectedTimelineIndex,
            removed,
            [this]() {
                refreshTimelineFrames();
                refreshTimelineList();
                emit animationDataChanged();
            }
        ));
    }
}

// ---------------------------------------------------------------------------
// onTimelineFrameSelectionChanged
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onTimelineFrameSelectionChanged()
{
    if (m_session->selectedTimelineIndex < 0
            || m_session->selectedTimelineIndex >= m_session->activeAtlas().timelines.size()
            || !m_timelineFramesList) {
        return;
    }
    const int selectedRow = m_timelineFramesList->currentRow();
    if (selectedRow < 0
            || selectedRow >= m_session->activeAtlas().timelines[m_session->selectedTimelineIndex].frames.size()) {
        return;
    }
    emit animFrameIndexSelected(selectedRow);
}

// ---------------------------------------------------------------------------
// onGenerateTimelinesFromFrames
// ---------------------------------------------------------------------------
void TimelineEditorPanel::onGenerateTimelinesFromFrames()
{
    if (m_session->activeAtlas().layoutModels.isEmpty()
            || m_session->activeAtlas().layoutModels.first().sprites.isEmpty()) {
        MessageDialog::information(this, tr("Generate Timelines"),
            tr("Load or generate a layout before creating timelines."));
        return;
    }

    const QVector<AnimationTimeline> oldState = m_session->activeAtlas().timelines;
    const int oldSelection = m_session->selectedTimelineIndex;

    QVector<SpritePtr> renamedSprites;
    const bool multiSource = m_session->sources.size() > 1;
    for (const auto& model : m_session->activeAtlas().layoutModels) {
        for (const auto& sprite : model.sprites) {
            if (!sprite) continue;
            const QString cp = QDir::cleanPath(sprite->path);

            int bestMatchLen = -1;
            QString localName;
            QString srcDisplayName;
            for (const auto& src : m_session->sources) {
                if (src.cachedFolderPath.isEmpty()) continue;
                const QString cleanedCache = QDir::cleanPath(src.cachedFolderPath);
                if (cp.startsWith(cleanedCache + QLatin1Char('/'))
                        && cleanedCache.length() > bestMatchLen) {
                    bestMatchLen = cleanedCache.length();
                    const QString rel  = cp.mid(cleanedCache.length() + 1);
                    const QString dir  = QFileInfo(rel).path();
                    const QString base = QFileInfo(rel).baseName();
                    localName      = (dir.isEmpty() || dir == QLatin1String("."))
                                     ? base : dir + QLatin1Char('/') + base;
                    srcDisplayName = src.name;
                }
            }

            if (localName.isEmpty())
                localName = QFileInfo(sprite->path).baseName();

            const QString finalName = (multiSource && !srcDisplayName.isEmpty())
                                      ? srcDisplayName + QLatin1Char('/') + localName
                                      : localName;

            auto namedSprite = std::make_shared<Sprite>(*sprite);
            namedSprite->name = finalName;
            renamedSprites.append(namedSprite);
        }
    }

    int focusIndex = -1;
    QString status;
    bool changed = TimelineGenerationService::generateFromSprites(
        renamedSprites,
        m_session->activeAtlas().timelines,
        focusIndex,
        [this](const QString& timelineName) {
            return TimelineUi::askTimelineConflictResolution(this, timelineName);
        },
        status);

    if (changed) {
        if (focusIndex >= 0)
            m_session->selectedTimelineIndex = focusIndex;

        auto postExecute = [this]() {
            refreshTimelineList();
            if (m_session->selectedTimelineIndex >= 0)
                m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));
            else
                onTimelineSelectionChanged();
            emit animZoomResetAndFitRequested();
        };

        m_undoStack->push(new TimelinesUpdateCommand(
            &m_session->activeAtlas().timelines,
            oldState,
            m_session->activeAtlas().timelines,
            oldSelection,
            m_session->selectedTimelineIndex,
            &m_session->selectedTimelineIndex,
            postExecute,
            tr("Auto-create Timelines")
        ));

        postExecute();
        emit statusMessage(status);
    } else {
        MessageDialog::information(this, tr("Generate Timelines"), status);
    }
}

// ---------------------------------------------------------------------------
// hasDuplicateTimelineName
// ---------------------------------------------------------------------------
bool TimelineEditorPanel::hasDuplicateTimelineName(const QString& name) const
{
    if (!m_session) return false;
    for (const auto& timeline : m_session->activeAtlas().timelines) {
        if (timeline.name == name)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// getUniqueTimelineName
// ---------------------------------------------------------------------------
QString TimelineEditorPanel::getUniqueTimelineName(const QString& baseName, const QString& folderPath) const
{
    QString fullName = baseName;
    if (!folderPath.isEmpty())
        fullName = folderPath + "/" + baseName;

    if (!hasDuplicateTimelineName(fullName))
        return fullName;

    for (int i = 1; i <= 1000; ++i) {
        const QString candidate = fullName + "_" + QString::number(i);
        if (!hasDuplicateTimelineName(candidate))
            return candidate;
    }

    return fullName + "_unique";
}
