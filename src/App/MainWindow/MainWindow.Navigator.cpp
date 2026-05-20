#include "MainWindow.h"
#include "NavigatorTreeWidget.h"
#include "AnimationTimelineOps.h"
#include "FolderSyncService.h"
#include "UndoCommands.h"

#include <functional>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>

// ---------------------------------------------------------------------------
// Helper: collect sprite paths from all checked (Qt::Checked) leaf items.
// Falls back to the single current/selected item if nothing is checked.
// ---------------------------------------------------------------------------
QStringList MainWindow::collectCheckedSpritePaths() const
{
    QStringList paths;
    std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* item) {
        if (item->childCount() == 0) {
            if (item->checkState(0) == Qt::Checked) {
                auto sprite = item->data(0, Qt::UserRole).value<SpritePtr>();
                if (sprite && !sprite->path.isEmpty())
                    paths.append(sprite->path);
            }
        } else {
            for (int i = 0; i < item->childCount(); ++i)
                walk(item->child(i));
        }
    };
    for (int i = 0; i < m_spriteTree->topLevelItemCount(); ++i)
        walk(m_spriteTree->topLevelItem(i));

    if (paths.isEmpty()) {
        QTreeWidgetItem* cur = m_spriteTree->currentItem();
        if (cur) {
            auto sprite = cur->data(0, Qt::UserRole).value<SpritePtr>();
            if (sprite && !sprite->path.isEmpty())
                paths.append(sprite->path);
        }
    }
    return paths;
}

// ---------------------------------------------------------------------------
// Helper: recursively collect all leaf sprite paths under a group node.
// ---------------------------------------------------------------------------
QStringList MainWindow::collectDescendantSpritePaths(QTreeWidgetItem* item) const
{
    QStringList paths;
    if (!item) return paths;
    if (item->childCount() == 0) {
        auto sprite = item->data(0, Qt::UserRole).value<SpritePtr>();
        if (sprite && !sprite->path.isEmpty())
            paths.append(sprite->path);
    } else {
        for (int i = 0; i < item->childCount(); ++i)
            paths.append(collectDescendantSpritePaths(item->child(i)));
    }
    return paths;
}

// ---------------------------------------------------------------------------
// Helper: walk parent chain to build the subfolder path relative to sourceFolder.
// Skips animation-group nodes (nodes whose children are all leaves).
// ---------------------------------------------------------------------------
QString MainWindow::folderPathForTreeItem(QTreeWidgetItem* item) const
{
    if (!item) return {};

    // If this item is a leaf, start from its parent
    QTreeWidgetItem* node = (item->childCount() == 0) ? item->parent() : item;

    QStringList parts;
    while (node) {
        // Skip animation-group nodes: all children are leaves
        bool allLeaves = true;
        for (int i = 0; i < node->childCount(); ++i) {
            if (node->child(i)->childCount() > 0) { allLeaves = false; break; }
        }
        if (!allLeaves || !node->parent()) {
            // This is a real folder node (or top-level atlas node)
            // Only include if it doesn't have UserRole data (not a sprite leaf)
            if (!node->data(0, Qt::UserRole).isValid())
                parts.prepend(node->text(0));
        }
        node = node->parent();
    }
    return parts.join('/');
}

// ---------------------------------------------------------------------------
// Context menu slot
// ---------------------------------------------------------------------------
void MainWindow::onSpriteTreeContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* clickedItem = m_spriteTree->itemAt(pos);

    // Paths for the right-clicked item and all its descendants.
    QStringList clickedPaths;
    {
        std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* item) {
            QVariant v = item->data(0, Qt::UserRole);
            if (v.isValid()) {
                auto sprite = v.value<SpritePtr>();
                if (sprite && !sprite->path.isEmpty())
                    clickedPaths.append(sprite->path);
            } else {
                for (int i = 0; i < item->childCount(); ++i)
                    walk(item->child(i));
            }
        };
        if (clickedItem) walk(clickedItem);
    }

    // Paths from checkbox-checked leaf sprites (no fallback).
    QStringList checkedPaths;
    {
        std::function<void(QTreeWidgetItem*)> walkChecked = [&](QTreeWidgetItem* item) {
            if (item->childCount() == 0) {
                if (item->checkState(0) == Qt::Checked) {
                    auto sprite = item->data(0, Qt::UserRole).value<SpritePtr>();
                    if (sprite && !sprite->path.isEmpty())
                        checkedPaths.append(sprite->path);
                }
            } else {
                for (int i = 0; i < item->childCount(); ++i)
                    walkChecked(item->child(i));
            }
        };
        for (int i = 0; i < m_spriteTree->topLevelItemCount(); ++i)
            walkChecked(m_spriteTree->topLevelItem(i));
    }

    const bool clickedIsLeaf  = clickedItem && clickedItem->childCount() == 0
                                && clickedItem->data(0, Qt::UserRole).isValid();
    const bool clickedIsGroup = clickedItem && clickedItem->childCount() > 0;
    const bool hasChecked     = !checkedPaths.isEmpty();
    const bool hasTimeline    = m_session->selectedTimelineIndex >= 0
                                && m_session->selectedTimelineIndex < m_session->timelines.size();

    bool clickedGroupHasGroups = false;
    if (clickedIsGroup) {
        for (int i = 0; i < clickedItem->childCount(); ++i) {
            if (clickedItem->child(i)->childCount() > 0) {
                clickedGroupHasGroups = true;
                break;
            }
        }
    }

    const QString subfolder = clickedItem ? folderPathForTreeItem(clickedItem) : QString();

    QMenu menu(this);
    // Helper: insert a separator only when there are preceding items.
    bool hadItems = false;
    auto addSep = [&]() { if (hadItems) { menu.addSeparator(); hadItems = false; } };

    // ── Section 1: delete ──────────────────────────────────────────────────
    QAction* deleteFrameAction    = nullptr;
    QAction* deleteGroupAction    = nullptr;
    QAction* deleteSelectedAction = nullptr;

    if (clickedIsLeaf)  { deleteFrameAction    = menu.addAction(tr("Delete frame"));    hadItems = true; }
    if (clickedIsGroup) { deleteGroupAction    = menu.addAction(tr("Delete group"));    hadItems = true; }
    if (hasChecked)     { deleteSelectedAction = menu.addAction(tr("Delete selected")); hadItems = true; }

    // ── Section 2: add frames ──────────────────────────────────────────────
    addSep();
    const QString addLabel = clickedIsGroup
        ? tr("Add frames into '%1'...").arg(clickedItem->text(0))
        : tr("Add frames...");
    QAction* addFramesAction = menu.addAction(addLabel);
    hadItems = true;

    // ── Section 3: timelines ───────────────────────────────────────────────
    QAction* createTimelineFromGroupAction    = nullptr;
    QAction* autoCreateTimelinesAction        = nullptr;
    QAction* createTimelineFromSelectedAction = nullptr;
    QAction* addToTimelineAction              = nullptr;

    if (clickedIsGroup || hasChecked) {
        addSep();
        if (clickedIsGroup)              { createTimelineFromGroupAction    = menu.addAction(tr("Create timeline from group"));          hadItems = true; }
        if (clickedGroupHasGroups)       { autoCreateTimelinesAction        = menu.addAction(tr("Auto-create timelines"));               hadItems = true; }
        if (hasChecked)                  { createTimelineFromSelectedAction = menu.addAction(tr("Create timeline from selected frames")); hadItems = true; }
        if (hasChecked && hasTimeline)   { addToTimelineAction              = menu.addAction(tr("Add selected to current timeline"));    hadItems = true; }
    }

    // ── Section 4: grouping ────────────────────────────────────────────────
    QAction* groupSelectedAction = nullptr;
    QAction* ungroupAction       = nullptr;

    if (hasChecked || clickedIsGroup) {
        addSep();
        if (hasChecked)     { groupSelectedAction = menu.addAction(tr("Group selected frames...")); hadItems = true; }
        if (clickedIsGroup) { ungroupAction       = menu.addAction(tr("Ungroup (move up)"));        hadItems = true; }
    }

    // ── Dispatch ───────────────────────────────────────────────────────────
    QAction* chosen = menu.exec(m_spriteTree->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if      (chosen == deleteFrameAction || chosen == deleteGroupAction)  onNavigatorDeleteFrames(clickedPaths);
    else if (chosen == deleteSelectedAction)                              onNavigatorDeleteFrames(checkedPaths);
    else if (chosen == addFramesAction)                                   onNavigatorAddFrames(subfolder);
    else if (chosen == createTimelineFromGroupAction)                     onNavigatorCreateTimeline(clickedPaths, clickedItem);
    else if (chosen == autoCreateTimelinesAction)                         onNavigatorAutoCreateTimelines(clickedItem);
    else if (chosen == createTimelineFromSelectedAction)                  onNavigatorCreateTimeline(checkedPaths, nullptr);
    else if (chosen == addToTimelineAction)                               onNavigatorAddToTimeline(checkedPaths);
    else if (chosen == groupSelectedAction)                               onNavigatorCreateGroup(checkedPaths, subfolder);
    else if (chosen == ungroupAction)                                     onNavigatorUngroup(clickedItem);
}

// ---------------------------------------------------------------------------
// Action: Delete selected frames
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorDeleteFrames(const QStringList& paths)
{
    onRemoveFramesRequested(paths);
}

// ---------------------------------------------------------------------------
// Action: Add frames (optionally into a subfolder)
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorAddFrames(const QString& subfolder)
{
    QString startDir = m_session->sourceFolder;
    if (startDir.isEmpty())
        startDir = m_session->currentFolder;

    QString filter = tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tga *.dds)");
    QStringList files = QFileDialog::getOpenFileNames(this, tr("Add Frames"), startDir, filter);
    if (files.isEmpty()) return;

    // Determine target directory
    QString targetDir = m_session->sourceFolder;
    if (targetDir.isEmpty()) {
        targetDir = m_session->currentFolder;
    }
    if (!subfolder.isEmpty()) {
        targetDir = targetDir + '/' + subfolder;
    }
    QDir().mkpath(targetDir);

    QSet<QString> existing(m_session->activeFramePaths.begin(), m_session->activeFramePaths.end());
    QStringList added;
    for (const QString& file : files) {
        QFileInfo info(file);
        if (!info.exists() || info.isDir()) continue;

        QString destPath = targetDir + '/' + info.fileName();
        if (destPath != info.absoluteFilePath()) {
            if (QFile::exists(destPath))
                QFile::remove(destPath);
            QFile::copy(info.absoluteFilePath(), destPath);
        }
        if (!existing.contains(destPath)) {
            existing.insert(destPath);
            added.append(destPath);
        }
    }
    if (added.isEmpty()) {
        QMessageBox::information(this, tr("Add Frames"), tr("All selected frames are already loaded."));
        return;
    }

    // Organize files by pattern before adding to session
    added = FolderSyncService::organizeNewImagesByPattern(added);

    const QStringList previousFramePaths = m_session->activeFramePaths;
    m_session->activeFramePaths.append(added);
    m_statusLabel->setText(QString(tr("Adding %1 frame(s)...")).arg(added.size()));
    if (!ensureFrameListInput()) {
        m_session->activeFramePaths = previousFramePaths;
        QMessageBox::warning(this, tr("Add Frames"), tr("Could not create temporary frame list."));
        return;
    }
    scheduleLayoutRebuild();
}

// ---------------------------------------------------------------------------
// Action: Add checked sprites to the current timeline
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorAddToTimeline(const QStringList& paths)
{
    if (paths.isEmpty()) return;
    const int tlIdx = m_session->selectedTimelineIndex;
    if (tlIdx < 0 || tlIdx >= m_session->timelines.size()) return;

    auto postExecute = [this]() {
        refreshTimelineFrames();
        refreshTimelineList();
        refreshAnimationTest();
    };

    const bool useMacro = paths.size() > 1;
    if (useMacro) m_undoStack->beginMacro(tr("Add Frames to Timeline"));
    for (const QString& path : paths) {
        const int insertIdx = m_session->timelines[tlIdx].frames.size();
        AnimationTimelineOps::dropFrame(m_session->timelines, tlIdx, path, -1);
        m_undoStack->push(new TimelineFrameDropCommand(
            &m_session->timelines, tlIdx, path, insertIdx, postExecute));
    }
    if (useMacro) m_undoStack->endMacro();

    refreshTimelineFrames();
    refreshTimelineList();
    refreshAnimationTest();
}

// ---------------------------------------------------------------------------
// Action: Create a new timeline from selected sprites
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorCreateTimeline(const QStringList& paths, QTreeWidgetItem* contextItem)
{
    if (paths.isEmpty()) return;

    // Determine the folder path and default name from the context item
    QString folderPath;
    QString defaultName = tr("animation");

    if (contextItem) {
        folderPath = folderPathForTreeItem(contextItem);
        // Use the context item's name as default (if it's a group)
        if (contextItem->childCount() > 0) {
            defaultName = contextItem->text(0);
        }
    }

    // Build the candidate name first
    QString candidateName = folderPath.isEmpty() ? defaultName : folderPath + "/" + defaultName;

    // Only ask for a name if there is a collision
    if (hasDuplicateTimelineName(candidateName)) {
        bool ok = false;
        QString promptName = QInputDialog::getText(this, tr("Create Timeline"),
                                                   tr("Timeline name (collision detected):"),
                                                   QLineEdit::Normal, defaultName, &ok);
        if (!ok || promptName.trimmed().isEmpty()) return;
        candidateName = getUniqueTimelineName(promptName.trimmed(), folderPath);

        if (hasDuplicateTimelineName(candidateName)) {
            QMessageBox::warning(this, tr("Create Timeline"),
                                 tr("A timeline with the name '%1' already exists.").arg(candidateName));
            return;
        }
    }

    const QString uniqueName = candidateName;

    AnimationTimeline timeline;
    timeline.name = uniqueName;
    timeline.fps = 8;
    timeline.frames = paths;
    m_session->timelines.append(timeline);
    m_session->selectedTimelineIndex = m_session->timelines.size() - 1;

    refreshTimelineList();
    m_timelineList->setCurrentRow(m_session->selectedTimelineIndex);
    refreshTimelineFrames();
    refreshAnimationTest();

    m_undoStack->push(new TimelineAddCommand(
        &m_session->timelines,
        timeline,
        &m_session->selectedTimelineIndex,
        [this]() {
            refreshTimelineList();
            if (m_session->selectedTimelineIndex >= 0)
                m_timelineList->setCurrentRow(m_session->selectedTimelineIndex);
            else
                onTimelineSelectionChanged();
            refreshTimelineFrames();
            refreshAnimationTest();
        }
    ));
}

// ---------------------------------------------------------------------------
// Action: Create group from selection (move files into subfolder)
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorCreateGroup(const QStringList& paths, const QString& parentFolder)
{
    if (paths.isEmpty()) return;

    bool ok = false;
    QString groupName = QInputDialog::getText(this, tr("Create Group"),
                                              tr("Group name:"), QLineEdit::Normal,
                                              QString(), &ok);
    if (!ok || groupName.trimmed().isEmpty()) return;
    groupName = groupName.trimmed();

    QString baseDir = m_session->sourceFolder;
    if (baseDir.isEmpty())
        baseDir = m_session->currentFolder;

    QString targetDir = baseDir;
    if (!parentFolder.isEmpty())
        targetDir += '/' + parentFolder;
    targetDir += '/' + groupName;
    QDir().mkpath(targetDir);

    const QStringList savedActivePaths = m_session->activeFramePaths;

    QVector<QPair<QString,QString>> moves;
    for (const QString& oldPath : paths) {
        QFileInfo fi(oldPath);
        QString newPath = targetDir + '/' + fi.fileName();
        if (QFile::rename(oldPath, newPath)) {
            int idx = m_session->activeFramePaths.indexOf(oldPath);
            if (idx >= 0)
                m_session->activeFramePaths[idx] = newPath;
            moves.append({oldPath, newPath});
        }
    }

    if (!ensureFrameListInput()) {
        QMessageBox::warning(this, tr("Create Group"), tr("Could not create temporary frame list."));
        return;
    }

    const QStringList newActivePaths = m_session->activeFramePaths;
    scheduleLayoutRebuild();

    m_undoStack->push(new CreateGroupCommand(
        &m_session->activeFramePaths,
        moves,
        targetDir,
        savedActivePaths,
        newActivePaths,
        [this]() { return ensureFrameListInput(); },
        [this]() { scheduleLayoutRebuild(false); }
    ));
}

// ---------------------------------------------------------------------------
// Action: Delete group + all descendant files
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorDeleteGroup(QTreeWidgetItem* groupItem)
{
    if (!groupItem) return;
    QStringList paths = collectDescendantSpritePaths(groupItem);
    if (paths.isEmpty()) return;

    onRemoveFramesRequested(paths);
}

// ---------------------------------------------------------------------------
// Action: Ungroup — move descendant files up one level
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorUngroup(QTreeWidgetItem* groupItem)
{
    if (!groupItem) return;

    QStringList paths = collectDescendantSpritePaths(groupItem);
    if (paths.isEmpty()) return;

    const QStringList savedActivePaths = m_session->activeFramePaths;

    QVector<QPair<QString,QString>> moves;
    QSet<QString> removedDirs;

    for (const QString& oldPath : paths) {
        QFileInfo fi(oldPath);
        QDir parentDir = fi.dir();
        parentDir.cdUp();
        QString newPath = parentDir.absoluteFilePath(fi.fileName());
        if (newPath == oldPath) continue;
        if (QFile::rename(oldPath, newPath)) {
            int idx = m_session->activeFramePaths.indexOf(oldPath);
            if (idx >= 0)
                m_session->activeFramePaths[idx] = newPath;
            moves.append({oldPath, newPath});
        }
        removedDirs.insert(fi.absolutePath());
    }

    // Try to remove the now-empty subfolders
    for (const QString& d : removedDirs)
        QDir().rmdir(d);

    if (!ensureFrameListInput()) {
        QMessageBox::warning(this, tr("Ungroup"), tr("Could not create temporary frame list."));
        return;
    }

    const QStringList newActivePaths = m_session->activeFramePaths;
    scheduleLayoutRebuild();

    if (!moves.isEmpty()) {
        m_undoStack->push(new UngroupCommand(
            &m_session->activeFramePaths,
            moves,
            removedDirs,
            savedActivePaths,
            newActivePaths,
            [this]() { return ensureFrameListInput(); },
            [this]() { scheduleLayoutRebuild(false); }
        ));
    }
}

// ---------------------------------------------------------------------------
// Action: Auto-create timelines from child groups
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorAutoCreateTimelines(QTreeWidgetItem* parentGroup)
{
    if (!parentGroup) return;

    const QString parentFolderPath = folderPathForTreeItem(parentGroup);

    auto postExecute = [this]() {
        refreshTimelineList();
        if (m_session->selectedTimelineIndex >= 0)
            m_timelineList->setCurrentRow(m_session->selectedTimelineIndex);
        else
            onTimelineSelectionChanged();
        refreshTimelineFrames();
        refreshAnimationTest();
    };

    bool useMacro = parentGroup->childCount() > 1;
    if (useMacro) {
        m_undoStack->beginMacro(tr("Auto-create Timelines"));
    }

    int addedCount = 0;
    for (int i = 0; i < parentGroup->childCount(); ++i) {
        QTreeWidgetItem* childItem = parentGroup->child(i);
        QStringList paths = collectDescendantSpritePaths(childItem);
        if (paths.isEmpty()) continue;

        AnimationTimeline timeline;
        timeline.name = getUniqueTimelineName(childItem->text(0), parentFolderPath);
        timeline.fps = 8;
        timeline.frames = paths;

        m_session->timelines.append(timeline);
        m_session->selectedTimelineIndex = m_session->timelines.size() - 1;
        
        m_undoStack->push(new TimelineAddCommand(
            &m_session->timelines, timeline,
            &m_session->selectedTimelineIndex, postExecute));
        
        addedCount++;
    }

    if (useMacro) {
        m_undoStack->endMacro();
    }

    if (addedCount > 0) {
        refreshTimelineList();
        m_timelineList->setCurrentRow(m_session->selectedTimelineIndex);
        refreshTimelineFrames();
        refreshAnimationTest();
    }
}

// ---------------------------------------------------------------------------
// Navigator Search Filter
// ---------------------------------------------------------------------------
void MainWindow::filterSpriteTree(const QString& text) {
    if (!m_spriteTree) return;

    // Iterate through all items in the tree and apply filter
    QTreeWidgetItemIterator it(m_spriteTree);
    QSet<QTreeWidgetItem*> itemsToShow;

    // First pass: find all items matching the search text
    while (*it) {
        QTreeWidgetItem* item = *it;
        bool matches = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive);

        if (matches) {
            itemsToShow.insert(item);
            // Mark all ancestors as visible (they should show since a descendant matches)
            QTreeWidgetItem* parent = item->parent();
            while (parent) {
                itemsToShow.insert(parent);
                parent = parent->parent();
            }
        }
        ++it;
    }

    // Second pass: apply visibility based on filter results
    QTreeWidgetItemIterator it2(m_spriteTree);
    while (*it2) {
        QTreeWidgetItem* item = *it2;
        item->setHidden(!itemsToShow.contains(item));
        ++it2;
    }
}
