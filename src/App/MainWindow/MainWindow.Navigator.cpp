#include "MainWindow.h"
#include "NavigatorTreeWidget.h"
#include "AnimationTimelineOps.h"
#include "FolderSyncService.h"

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

    // Collect paths from selected items (including descendants of groups)
    QStringList selectedPaths;
    std::function<void(QTreeWidgetItem*)> collectPaths = [&](QTreeWidgetItem* item) {
        QVariant v = item->data(0, Qt::UserRole);
        if (v.isValid()) {
            auto sprite = v.value<SpritePtr>();
            if (sprite && !sprite->path.isEmpty())
                selectedPaths.append(sprite->path);
        } else if (item->childCount() > 0) {
            for (int i = 0; i < item->childCount(); ++i)
                collectPaths(item->child(i));
        }
    };

    const auto selectedItems = m_spriteTree->selectedItems();
    for (QTreeWidgetItem* item : selectedItems) {
        collectPaths(item);
    }

    const bool hasSelected = !selectedPaths.isEmpty();
    const bool clickedIsGroup = clickedItem && clickedItem->childCount() > 0;
    const bool hasTimeline = m_session->selectedTimelineIndex >= 0
                             && m_session->selectedTimelineIndex < m_session->timelines.size();

    // Check if clicked item is a group containing other groups
    bool clickedGroupHasGroups = false;
    if (clickedIsGroup) {
        for (int i = 0; i < clickedItem->childCount(); ++i) {
            if (clickedItem->child(i)->childCount() > 0) {
                clickedGroupHasGroups = true;
                break;
            }
        }
    }

    QMenu menu(this);

    // Delete
    QAction* deleteAction = menu.addAction(tr("Delete"));
    deleteAction->setEnabled(hasSelected);

    menu.addSeparator();

    // Add frames
    QString subfolder;
    if (clickedItem) {
        subfolder = folderPathForTreeItem(clickedItem);
    }
    QString addLabel = subfolder.isEmpty()
                       ? tr("Add frames...")
                       : tr("Add frames into '%1'...").arg(subfolder);
    QAction* addFramesAction = menu.addAction(addLabel);

    menu.addSeparator();

    // Timeline actions
    QAction* addToTimelineAction = menu.addAction(tr("Add to current timeline"));
    addToTimelineAction->setEnabled(hasSelected && hasTimeline);

    QAction* createTimelineAction = menu.addAction(tr("Create timeline"));
    createTimelineAction->setEnabled(hasSelected);

    QAction* autoCreateTimelinesAction = nullptr;
    if (clickedGroupHasGroups) {
        autoCreateTimelinesAction = menu.addAction(tr("Auto-create timelines"));
    }

    menu.addSeparator();

    // Group actions
    QAction* createGroupAction = menu.addAction(tr("Create group from selection..."));
    createGroupAction->setEnabled(hasSelected);

    QAction* ungroupAction = menu.addAction(tr("Ungroup (move up)"));
    ungroupAction->setEnabled(clickedIsGroup);

    // Execute
    QAction* chosen = menu.exec(m_spriteTree->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == deleteAction) {
        onNavigatorDeleteFrames(selectedPaths);
    } else if (chosen == addFramesAction) {
        onNavigatorAddFrames(subfolder);
    } else if (chosen == addToTimelineAction) {
        onNavigatorAddToTimeline(selectedPaths);
    } else if (chosen == createTimelineAction) {
        onNavigatorCreateTimeline(selectedPaths, clickedItem);
    } else if (chosen == autoCreateTimelinesAction) {
        onNavigatorAutoCreateTimelines(clickedItem);
    } else if (chosen == createGroupAction) {
        QString parentFolder;
        if (clickedItem)
            parentFolder = folderPathForTreeItem(clickedItem);
        onNavigatorCreateGroup(selectedPaths, parentFolder);
    } else if (chosen == ungroupAction) {
        onNavigatorUngroup(clickedItem);
    }
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
    if (m_session->selectedTimelineIndex < 0
        || m_session->selectedTimelineIndex >= m_session->timelines.size())
        return;

    for (const QString& path : paths) {
        AnimationTimelineOps::dropFrame(m_session->timelines,
                                        m_session->selectedTimelineIndex,
                                        path, -1);
    }
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

    refreshTimelineList();
    m_session->selectedTimelineIndex = m_session->timelines.size() - 1;
    m_timelineList->setCurrentRow(m_session->selectedTimelineIndex);
    refreshTimelineFrames();
    refreshAnimationTest();
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

    for (const QString& oldPath : paths) {
        QFileInfo fi(oldPath);
        QString newPath = targetDir + '/' + fi.fileName();
        if (QFile::rename(oldPath, newPath)) {
            int idx = m_session->activeFramePaths.indexOf(oldPath);
            if (idx >= 0)
                m_session->activeFramePaths[idx] = newPath;
        }
    }

    if (!ensureFrameListInput()) {
        QMessageBox::warning(this, tr("Create Group"), tr("Could not create temporary frame list."));
        return;
    }
    scheduleLayoutRebuild();
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
        }
    }

    // Try to remove the now-empty subfolder
    // Gather unique directories from the original paths
    QSet<QString> dirs;
    for (const QString& p : paths)
        dirs.insert(QFileInfo(p).absolutePath());
    for (const QString& d : dirs)
        QDir().rmdir(d);

    if (!ensureFrameListInput()) {
        QMessageBox::warning(this, tr("Ungroup"), tr("Could not create temporary frame list."));
        return;
    }
    scheduleLayoutRebuild();
}

// ---------------------------------------------------------------------------
// Action: Auto-create timelines from child groups
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorAutoCreateTimelines(QTreeWidgetItem* parentGroup)
{
    if (!parentGroup) return;

    // Get the folder path for the parent group
    QString parentFolderPath = folderPathForTreeItem(parentGroup);

    // Iterate through all child groups and create a timeline from each
    int created = 0;
    for (int i = 0; i < parentGroup->childCount(); ++i) {
        QTreeWidgetItem* childItem = parentGroup->child(i);
        QStringList paths = collectDescendantSpritePaths(childItem);

        if (paths.isEmpty()) continue;

        // Create a unique timeline name with path and collision detection
        QString childName = childItem->text(0);
        QString uniqueName = getUniqueTimelineName(childName, parentFolderPath);

        AnimationTimeline timeline;
        timeline.name = uniqueName;
        timeline.fps = 8;
        timeline.frames = paths;
        m_session->timelines.append(timeline);
        ++created;
    }

    if (created == 0) return;

    refreshTimelineList();
    m_session->selectedTimelineIndex = m_session->timelines.size() - 1;
    m_timelineList->setCurrentRow(m_session->selectedTimelineIndex);
    refreshTimelineFrames();
    refreshAnimationTest();
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
