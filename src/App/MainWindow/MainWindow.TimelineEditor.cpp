#include "MainWindow.h"
#include "UndoCommands.h"
#include "TimelineTreeWidget.h"
#include "MessageDialog.h"

#include <QComboBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>

void MainWindow::onTimelineAddClicked() {
    QString name = m_timelineCreateEdit->text().trimmed();
    if (name.isEmpty()) {
        name = QString("Timeline %1").arg(m_session->timelines.size() + 1);
    }

    AnimationTimeline timeline;
    timeline.name = name;
    m_session->timelines.append(timeline);
    m_session->selectedTimelineIndex = m_session->timelines.size() - 1;

    m_timelineCreateEdit->clear();
    refreshTimelineList();
    m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));

    m_undoStack->push(new TimelineAddCommand(
        &m_session->timelines,
        timeline,
        &m_session->selectedTimelineIndex,
        [this]() {
            refreshTimelineList();
            if (m_session->selectedTimelineIndex >= 0) {
                m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));
            } else {
                onTimelineSelectionChanged();
            }
            refreshTimelineFrames();
            refreshAnimationTest();
        }
    ));
}

void MainWindow::onTimelineRemoveClicked() {
    if (m_session->selectedTimelineIndex < 0 || m_session->selectedTimelineIndex >= m_session->timelines.size()) {
        return;
    }
    const int removeIdx = m_session->selectedTimelineIndex;
    const AnimationTimeline savedTimeline = m_session->timelines[removeIdx];

    m_session->timelines.removeAt(removeIdx);
    m_session->selectedTimelineIndex = qMin(removeIdx, m_session->timelines.size() - 1);

    refreshTimelineList();
    if (m_session->selectedTimelineIndex >= 0) {
        m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));
    } else {
        // No timelines left, hide the timeline editor
        onTimelineSelectionChanged();
    }

    m_undoStack->push(new TimelineRemoveCommand(
        &m_session->timelines,
        removeIdx,
        savedTimeline,
        &m_session->selectedTimelineIndex,
        [this]() {
            refreshTimelineList();
            if (m_session->selectedTimelineIndex >= 0) {
                m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));
            } else {
                onTimelineSelectionChanged();
            }
            refreshTimelineFrames();
            refreshAnimationTest();
        }
    ));
}

void MainWindow::onTimelineSelectionChanged() {
    QTreeWidgetItem* item = m_timelineList->currentItem();
    // Folder node: keep current state unchanged (click only expands/collapses)
    if (item && !item->data(0, Qt::UserRole).isValid()) {
        return;
    }
    const int idx = (item && item->data(0, Qt::UserRole).isValid())
        ? item->data(0, Qt::UserRole).toInt() : -1;
    if (idx >= 0 && idx < m_session->timelines.size()) {
        m_session->selectedTimelineIndex = idx;
        const auto& tl = m_session->timelines[idx];
        const bool isAlias = !tl.aliasOf.isEmpty();

        m_timelineNameEdit->setText(tl.name);
        m_timelineNameEdit->setEnabled(true);

        // Alias label
        if (m_timelineAliasLabel) {
            m_timelineAliasLabel->setVisible(isAlias);
            if (isAlias)
                m_timelineAliasLabel->setText(tr("Alias of: %1").arg(tl.aliasOf));
        }

        // Flip combo
        if (m_timelineFlipLabel) m_timelineFlipLabel->setVisible(isAlias);
        if (m_timelineFlipCombo) {
            m_timelineFlipCombo->setVisible(isAlias);
            if (isAlias) {
                const QSignalBlocker b(m_timelineFlipCombo);
                int flipIdx = (tl.hFlip ? 1 : 0) + (tl.vFlip ? 2 : 0);
                m_timelineFlipCombo->setCurrentIndex(flipIdx);
            }
        }

        // FPS: always show; disable for aliases, sync from source
        int fpsToShow = tl.fps;
        if (isAlias) {
            for (const auto& src : m_session->timelines) {
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

        if (m_animPlaying) {
#ifndef Q_OS_WASM
            m_animTimer->setInterval(1000 / qMax(1, fpsToShow));
#endif
            m_animElapsed.restart(); // reset timing to avoid frame skip on first tick
        }

        // Frames list: read-only for aliases
        m_timelineFramesList->setReadOnly(isAlias);

        refreshTimelineFrames();
        m_animFrameIndex = 0;
        m_timelineEditorContainer->setVisible(true);
        fitAnimationToViewport();
        refreshAnimationTest();
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
    m_timelineEditorContainer->setVisible(false);
    m_timelineDragHintLabel->setVisible(false);
    m_timelineFramesList->clear();
    m_timelineFramesList->setReadOnly(false);
    refreshAnimationTest();
}

void MainWindow::onTimelineNameChanged() {
    const int idx = m_session->selectedTimelineIndex;
    if (idx < 0 || idx >= m_session->timelines.size()) return;
    const QString oldName = m_session->timelines[idx].name;
    const QString newName = m_timelineNameEdit->text();
    if (oldName == newName) return;
    m_session->timelines[idx].name = newName;
    // Cascade rename to any aliases pointing to the old name
    for (auto& tl : m_session->timelines) {
        if (tl.aliasOf == oldName) tl.aliasOf = newName;
    }
    refreshTimelineList();
    m_undoStack->push(new SetTimelineNameCommand(
        &m_session->timelines, idx, oldName, newName,
        [this, idx]() {
            refreshTimelineList();
            if (m_session->selectedTimelineIndex == idx
                    && idx >= 0 && idx < m_session->timelines.size()) {
                const QSignalBlocker blocker(m_timelineNameEdit);
                m_timelineNameEdit->setText(m_session->timelines[idx].name);
            }
        }
    ));
}

void MainWindow::onTimelineFpsChanged(int fps) {
    const int idx = m_session->selectedTimelineIndex;
    if (idx < 0 || idx >= m_session->timelines.size()) return;
    const int oldFps = m_session->timelines[idx].fps;
    if (oldFps == fps) return;
    m_session->timelines[idx].fps = fps;
    if (m_animPlaying) {
#ifndef Q_OS_WASM
        m_animTimer->setInterval(1000 / fps);
#endif
        m_animElapsed.restart();
    }
    refreshTimelineList();
    refreshAnimationTest();
    m_undoStack->push(new SetTimelineFpsCommand(
        &m_session->timelines, idx, oldFps, fps,
        [this, idx]() {
            if (m_session->selectedTimelineIndex == idx
                    && idx >= 0 && idx < m_session->timelines.size()) {
                const int curFps = m_session->timelines[idx].fps;
                {
                    const QSignalBlocker blocker(m_timelineFpsSpin);
                    m_timelineFpsSpin->setValue(curFps);
                }
                if (m_animPlaying) {
#ifndef Q_OS_WASM
                    m_animTimer->setInterval(1000 / curFps);
#endif
                    m_animElapsed.restart();
                }
            }
            refreshTimelineList();
            refreshAnimationTest();
        }
    ));
}

void MainWindow::onTimelineCreateAlias() {
    const int idx = m_session->selectedTimelineIndex;
    if (idx < 0 || idx >= m_session->timelines.size()) return;
    const auto& source = m_session->timelines[idx];
    if (!source.aliasOf.isEmpty()) return; // Cannot alias an alias

    const QString sourceName = source.name;

    AnimationTimeline alias;
    alias.name = sourceName + "_alias";
    alias.aliasOf = sourceName;
    alias.fps = source.fps;

    m_session->timelines.append(alias);
    m_session->selectedTimelineIndex = m_session->timelines.size() - 1;

    refreshTimelineList();
    m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));

    m_undoStack->push(new TimelineAddCommand(
        &m_session->timelines,
        alias,
        &m_session->selectedTimelineIndex,
        [this]() {
            refreshTimelineList();
            if (m_session->selectedTimelineIndex >= 0) {
                m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));
            } else {
                onTimelineSelectionChanged();
            }
            refreshTimelineFrames();
            refreshAnimationTest();
        }
    ));
}

void MainWindow::onTimelineFlipChanged(int index) {
    const int idx = m_session->selectedTimelineIndex;
    if (idx < 0 || idx >= m_session->timelines.size()) return;
    auto& tl = m_session->timelines[idx];
    const bool oldH = tl.hFlip;
    const bool oldV = tl.vFlip;
    const bool newH = (index == 1 || index == 3);
    const bool newV = (index == 2 || index == 3);
    if (oldH == newH && oldV == newV) return;
    tl.hFlip = newH;
    tl.vFlip = newV;
    refreshAnimationTest();
    m_undoStack->push(new SetTimelineFlipCommand(
        &m_session->timelines, idx, oldH, oldV, newH, newV,
        [this]() { refreshAnimationTest(); }
    ));
}

// ---------------------------------------------------------------------------
// Helper: compute the folder path for a folder node item by walking its
// parent chain and collecting folder node text segments.
// ---------------------------------------------------------------------------
QString MainWindow::timelineItemFolderPath(QTreeWidgetItem* item) const
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
// Helper: collect indices of all checked leaf timeline items.
// ---------------------------------------------------------------------------
QVector<int> MainWindow::collectCheckedTimelineIndices() const
{
    QVector<int> result;
    QTreeWidgetItemIterator it(m_timelineList);
    while (*it) {
        const QVariant v = (*it)->data(0, Qt::UserRole);
        if (v.isValid() && (*it)->checkState(0) == Qt::Checked)
            result.append(v.toInt());
        ++it;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Checkbox state management helpers
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
    if (parent->data(0, Qt::UserRole).isValid()) return; // leaf — skip

    int checked = 0, unchecked = 0;
    for (int i = 0; i < parent->childCount(); ++i) {
        Qt::CheckState cs = parent->child(i)->checkState(0);
        if (cs == Qt::Checked)        ++checked;
        else if (cs == Qt::Unchecked) ++unchecked;
        else { ++checked; ++unchecked; } // PartiallyChecked counts as mixed
    }

    Qt::CheckState newState;
    if (checked == 0)
        newState = Qt::Unchecked;
    else if (unchecked == 0)
        newState = Qt::Checked;
    else
        newState = Qt::PartiallyChecked;

    parent->setCheckState(0, newState);
    updateParentCheckState(parent->parent());
}

void MainWindow::onTimelineItemChanged(QTreeWidgetItem* item, int column)
{
    if (column != 0) return;
    QSignalBlocker blocker(m_timelineList);
    if (!item->data(0, Qt::UserRole).isValid()) {
        // Folder node: propagate to children unless PartiallyChecked
        Qt::CheckState state = item->checkState(0);
        if (state != Qt::PartiallyChecked)
            setCheckStateRecursive(item, state);
    } else {
        // Leaf node: update parent tristate
        updateParentCheckState(item->parent());
    }
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------
void MainWindow::onTimelineContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_timelineList->itemAt(pos);
    const QVector<int> checkedIndices = collectCheckedTimelineIndices();
    const bool hasChecked = !checkedIndices.isEmpty();

    auto refreshAndSelect = [this]() {
        refreshTimelineList();
        if (m_session->selectedTimelineIndex >= 0)
            m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));
        else
            onTimelineSelectionChanged();
        refreshTimelineFrames();
        refreshAnimationTest();
    };

    QMenu menu(this);
    bool hasItems = false;

    if (item && !item->data(0, Qt::UserRole).isValid()) {
        // Folder node
        const QString folderPath = timelineItemFolderPath(item);
        const int lastSlash = folderPath.lastIndexOf('/');
        const bool hasParent = lastSlash >= 0;

        // ── Ungroup: move this folder to its grandparent ──────────────────
        if (hasParent) {
            menu.addAction(tr("Ungroup"), this, [this, folderPath, refreshAndSelect]() {
                const QVector<AnimationTimeline> oldTimelines = m_session->timelines;
                const int oldSel = m_session->selectedTimelineIndex;

                const int ls = folderPath.lastIndexOf('/');
                const QString folderName     = folderPath.mid(ls + 1);
                const QString parentPath     = folderPath.left(ls);
                const int grandparentSlash   = parentPath.lastIndexOf('/');
                const QString grandparentPath = (grandparentSlash < 0) ? QString()
                                                : parentPath.left(grandparentSlash);
                const QString oldPrefix = folderPath + "/";
                const QString newPrefix = (grandparentPath.isEmpty() ? QString()
                                                                      : grandparentPath + "/")
                                          + folderName + "/";

                // Collision check
                for (const auto& tl : m_session->timelines) {
                    if (tl.name.startsWith(oldPrefix)) {
                        const QString newName = newPrefix + tl.name.mid(oldPrefix.length());
                        for (const auto& other : m_session->timelines) {
                            if (other.name == newName && !other.name.startsWith(oldPrefix)) {
                                MessageDialog::warning(this, tr("Ungroup"),
                                    tr("Cannot ungroup: \"%1\" would conflict with an existing timeline.").arg(newName));
                                return;
                            }
                        }
                    }
                }

                for (auto& tl : m_session->timelines) {
                    if (tl.name.startsWith(oldPrefix)) {
                        const QString oldName = tl.name;
                        tl.name = newPrefix + tl.name.mid(oldPrefix.length());
                        for (auto& other : m_session->timelines) {
                            if (other.aliasOf == oldName)
                                other.aliasOf = tl.name;
                        }
                    }
                }

                m_undoStack->push(new TimelinesUpdateCommand(
                    &m_session->timelines, oldTimelines, m_session->timelines,
                    oldSel, m_session->selectedTimelineIndex,
                    &m_session->selectedTimelineIndex,
                    refreshAndSelect, tr("Ungroup")));
                refreshAndSelect();
            });
        }

        // ── Remove... submenu ─────────────────────────────────────────────
        QMenu* removeMenu = menu.addMenu(tr("Remove..."));

        removeMenu->addAction(tr("and descendants"), this, [this, folderPath, refreshAndSelect]() {
            const QVector<AnimationTimeline> oldTimelines = m_session->timelines;
            const int oldSel = m_session->selectedTimelineIndex;

            const QString prefix = folderPath + "/";
            QVector<QString> toRemove;
            for (const auto& tl : m_session->timelines) {
                if (tl.name.startsWith(prefix))
                    toRemove.append(tl.name);
            }
            const QSet<QString> removeSet(toRemove.begin(), toRemove.end());
            m_session->timelines.erase(
                std::remove_if(m_session->timelines.begin(), m_session->timelines.end(),
                    [&removeSet](const AnimationTimeline& tl) {
                        return removeSet.contains(tl.name) || removeSet.contains(tl.aliasOf);
                    }),
                m_session->timelines.end());
            m_session->selectedTimelineIndex = qMin(oldSel, m_session->timelines.size() - 1);

            m_undoStack->push(new TimelinesUpdateCommand(
                &m_session->timelines, oldTimelines, m_session->timelines,
                oldSel, m_session->selectedTimelineIndex,
                &m_session->selectedTimelineIndex,
                refreshAndSelect, tr("Remove Group")));
            refreshAndSelect();
        });

        removeMenu->addAction(tr("keep descendants"), this, [this, folderPath, refreshAndSelect]() {
            const QVector<AnimationTimeline> oldTimelines = m_session->timelines;
            const int oldSel = m_session->selectedTimelineIndex;

            const QString oldPrefix = folderPath + "/";
            const int ls2 = folderPath.lastIndexOf('/');
            const QString parentPath2 = (ls2 < 0) ? QString() : folderPath.left(ls2);
            const QString newPrefix = parentPath2.isEmpty() ? QString() : parentPath2 + "/";

            // Collision check
            for (const auto& tl : m_session->timelines) {
                if (tl.name.startsWith(oldPrefix)) {
                    const QString newName = newPrefix + tl.name.mid(oldPrefix.length());
                    for (const auto& other : m_session->timelines) {
                        if (other.name == newName && !other.name.startsWith(oldPrefix)) {
                            MessageDialog::warning(this, tr("Remove Group"),
                                tr("Cannot dissolve: \"%1\" would conflict with an existing timeline.").arg(newName));
                            return;
                        }
                    }
                }
            }

            for (auto& tl : m_session->timelines) {
                if (tl.name.startsWith(oldPrefix)) {
                    const QString oldName = tl.name;
                    tl.name = newPrefix + tl.name.mid(oldPrefix.length());
                    for (auto& other : m_session->timelines) {
                        if (other.aliasOf == oldName)
                            other.aliasOf = tl.name;
                    }
                }
            }

            m_undoStack->push(new TimelinesUpdateCommand(
                &m_session->timelines, oldTimelines, m_session->timelines,
                oldSel, m_session->selectedTimelineIndex,
                &m_session->selectedTimelineIndex,
                refreshAndSelect, tr("Remove Group (keep descendants)")));
            refreshAndSelect();
        });

        menu.addSeparator();

        menu.addAction(tr("Add Timeline Into Group"), this, [this, folderPath, refreshAndSelect]() {
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
            m_session->timelines.append(tl);
            m_session->selectedTimelineIndex = m_session->timelines.size() - 1;

            m_undoStack->push(new TimelineAddCommand(
                &m_session->timelines, tl,
                &m_session->selectedTimelineIndex,
                refreshAndSelect));
            refreshAndSelect();
        });

        hasItems = true;
    } else if (item && item->data(0, Qt::UserRole).isValid()) {
        // Leaf node
        const int idx = item->data(0, Qt::UserRole).toInt();
        m_timelineList->setCurrentItem(item);

        menu.addAction(tr("Remove Timeline"), this, [this, idx, refreshAndSelect]() {
            if (idx < 0 || idx >= m_session->timelines.size()) return;
            const QVector<AnimationTimeline> oldTimelines = m_session->timelines;
            const int oldSel = m_session->selectedTimelineIndex;
            const QString name = m_session->timelines[idx].name;

            m_session->timelines.erase(
                std::remove_if(m_session->timelines.begin(), m_session->timelines.end(),
                    [&name](const AnimationTimeline& tl) {
                        return tl.name == name || tl.aliasOf == name;
                    }),
                m_session->timelines.end());
            m_session->selectedTimelineIndex = qMin(oldSel, m_session->timelines.size() - 1);

            m_undoStack->push(new TimelinesUpdateCommand(
                &m_session->timelines, oldTimelines, m_session->timelines,
                oldSel, m_session->selectedTimelineIndex,
                &m_session->selectedTimelineIndex,
                refreshAndSelect, tr("Remove Timeline")));
            refreshAndSelect();
        });

        menu.addAction(tr("Create Alias"), this, &MainWindow::onTimelineCreateAlias);
        hasItems = true;
    }

    if (hasChecked) {
        if (hasItems) menu.addSeparator();
        menu.addAction(tr("Remove Selected"), this, [this, checkedIndices, refreshAndSelect]() {
            const QVector<AnimationTimeline> oldTimelines = m_session->timelines;
            const int oldSel = m_session->selectedTimelineIndex;

            // Sort descending to remove by index safely
            QVector<int> sorted = checkedIndices;
            std::sort(sorted.begin(), sorted.end(), std::greater<int>());

            QSet<QString> removedNames;
            for (int i : sorted) {
                if (i >= 0 && i < m_session->timelines.size()) {
                    removedNames.insert(m_session->timelines[i].name);
                }
            }
            m_session->timelines.erase(
                std::remove_if(m_session->timelines.begin(), m_session->timelines.end(),
                    [&removedNames](const AnimationTimeline& tl) {
                        return removedNames.contains(tl.name) || removedNames.contains(tl.aliasOf);
                    }),
                m_session->timelines.end());
            m_session->selectedTimelineIndex = qMin(oldSel, m_session->timelines.size() - 1);

            m_undoStack->push(new TimelinesUpdateCommand(
                &m_session->timelines, oldTimelines, m_session->timelines,
                oldSel, m_session->selectedTimelineIndex,
                &m_session->selectedTimelineIndex,
                refreshAndSelect, tr("Remove Selected Timelines")));
            refreshAndSelect();
        });
    }

    if (!item && !hasChecked) {
        QMenu menu(this);
        menu.addAction(tr("Add Timeline"), this, &MainWindow::onTimelineAddClicked);
        menu.exec(m_timelineList->mapToGlobal(pos));
        return;
    }

    if (!menu.isEmpty())
        menu.exec(m_timelineList->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// DEL key on timeline tree
// ---------------------------------------------------------------------------
void MainWindow::onTimelineDeleteKey()
{
    QTreeWidgetItem* item = m_timelineList->currentItem();
    if (!item) return;

    auto refreshAndSelect = [this]() {
        refreshTimelineList();
        if (m_session->selectedTimelineIndex >= 0)
            m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));
        else
            onTimelineSelectionChanged();
        refreshTimelineFrames();
        refreshAnimationTest();
    };

    if (!item->data(0, Qt::UserRole).isValid()) {
        // Folder node
        const QString folderPath = timelineItemFolderPath(item);
        const QVector<AnimationTimeline> oldTimelines = m_session->timelines;
        const int oldSel = m_session->selectedTimelineIndex;

        const QString prefix = folderPath + "/";
        QSet<QString> removedNames;
        for (const auto& tl : m_session->timelines) {
            if (tl.name.startsWith(prefix))
                removedNames.insert(tl.name);
        }
        m_session->timelines.erase(
            std::remove_if(m_session->timelines.begin(), m_session->timelines.end(),
                [&removedNames](const AnimationTimeline& tl) {
                    return removedNames.contains(tl.name) || removedNames.contains(tl.aliasOf);
                }),
            m_session->timelines.end());
        m_session->selectedTimelineIndex = qMin(oldSel, m_session->timelines.size() - 1);

        m_undoStack->push(new TimelinesUpdateCommand(
            &m_session->timelines, oldTimelines, m_session->timelines,
            oldSel, m_session->selectedTimelineIndex,
            &m_session->selectedTimelineIndex,
            refreshAndSelect, tr("Remove Group")));
        refreshAndSelect();
    } else {
        // Leaf node
        const int idx = item->data(0, Qt::UserRole).toInt();
        if (idx < 0 || idx >= m_session->timelines.size()) return;

        const QVector<AnimationTimeline> oldTimelines = m_session->timelines;
        const int oldSel = m_session->selectedTimelineIndex;
        const QString name = m_session->timelines[idx].name;

        m_session->timelines.erase(
            std::remove_if(m_session->timelines.begin(), m_session->timelines.end(),
                [&name](const AnimationTimeline& tl) {
                    return tl.name == name || tl.aliasOf == name;
                }),
            m_session->timelines.end());
        m_session->selectedTimelineIndex = qMin(oldSel, m_session->timelines.size() - 1);

        m_undoStack->push(new TimelinesUpdateCommand(
            &m_session->timelines, oldTimelines, m_session->timelines,
            oldSel, m_session->selectedTimelineIndex,
            &m_session->selectedTimelineIndex,
            refreshAndSelect, tr("Remove Timeline")));
        refreshAndSelect();
    }
}

// ---------------------------------------------------------------------------
// Drag & drop in timeline tree
// ---------------------------------------------------------------------------
void MainWindow::onTimelineTreeDropCompleted(int draggedIndex,
                                              const QString& draggedFolder,
                                              const QString& targetFolder)
{
    auto refreshAndSelect = [this]() {
        refreshTimelineList();
        if (m_session->selectedTimelineIndex >= 0)
            m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));
        else
            onTimelineSelectionChanged();
        refreshTimelineFrames();
        refreshAnimationTest();
    };

    const QVector<AnimationTimeline> oldTimelines = m_session->timelines;
    const int oldSel = m_session->selectedTimelineIndex;

    if (draggedIndex >= 0) {
        // Leaf drag
        if (draggedIndex >= m_session->timelines.size()) return;
        const QString oldName = m_session->timelines[draggedIndex].name;
        const QString leafName = oldName.split('/').last();
        const QString newName = targetFolder.isEmpty() ? leafName : targetFolder + "/" + leafName;
        if (newName == oldName) return;

        // Collision check
        for (const auto& tl : m_session->timelines) {
            if (tl.name == newName) {
                MessageDialog::warning(this, tr("Move Timeline"),
                    tr("A timeline named \"%1\" already exists.").arg(newName));
                return;
            }
        }

        m_session->timelines[draggedIndex].name = newName;
        for (auto& tl : m_session->timelines) {
            if (tl.aliasOf == oldName)
                tl.aliasOf = newName;
        }
    } else {
        // Folder drag
        const QString lastSegment = draggedFolder.split('/').last();
        const QString oldPrefix = draggedFolder + "/";
        const QString newPrefix = targetFolder.isEmpty()
            ? lastSegment + "/"
            : targetFolder + "/" + lastSegment + "/";
        if (newPrefix == oldPrefix) return;

        // Collision check
        for (const auto& tl : m_session->timelines) {
            if (tl.name.startsWith(oldPrefix)) {
                const QString newName = newPrefix + tl.name.mid(oldPrefix.length());
                for (const auto& other : m_session->timelines) {
                    if (other.name == newName && !other.name.startsWith(oldPrefix)) {
                        MessageDialog::warning(this, tr("Move Timeline"),
                            tr("Cannot move: \"%1\" would conflict with an existing timeline.").arg(newName));
                        return;
                    }
                }
            }
        }

        for (auto& tl : m_session->timelines) {
            if (tl.name.startsWith(oldPrefix)) {
                const QString oldName = tl.name;
                tl.name = newPrefix + tl.name.mid(oldPrefix.length());
                for (auto& other : m_session->timelines) {
                    if (other.aliasOf == oldName)
                        other.aliasOf = tl.name;
                }
            }
        }
    }

    m_undoStack->push(new TimelinesUpdateCommand(
        &m_session->timelines, oldTimelines, m_session->timelines,
        oldSel, m_session->selectedTimelineIndex,
        &m_session->selectedTimelineIndex,
        refreshAndSelect, tr("Move Timeline")));
    refreshAndSelect();
}
