#include "MainWindow.h"
#include "MessageDialog.h"
#include "NavigatorTreeWidget.h"
#include "AnimationTimelineOps.h"
#include "AnimationPreviewService.h"
#include "FolderSyncService.h"
#include "UndoCommands.h"
#include "TimelineGenerationService.h"
#include "TimelineUi.h"

#include <functional>
#include <QDesktopServices>
#include <QDir>
#include <QLabel>
#include <QSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>

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
    // A source node stores its index in UserRole+1 (set in refreshSpriteTree).
    const bool clickedIsSourceNode = clickedItem
                                     && clickedItem->data(0, Qt::UserRole + 1).isValid()
                                     && !clickedItem->data(0, Qt::UserRole).isValid();
    const int  clickedSourceIndex  = clickedIsSourceNode
                                     ? clickedItem->data(0, Qt::UserRole + 1).toInt() : -1;
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

    // Helper: find which smart folder (if any) contains the given absolute path.
    // Returns the index into m_session->smartFolders, or -1 if not in any smart folder.
    auto smartFolderIndexFor = [this](const QString& absPath) -> int {
        for (int i = 0; i < m_session->smartFolders.size(); ++i) {
            const QString& sfPath = m_session->smartFolders[i].path;
            if (!sfPath.isEmpty() && absPath.startsWith(sfPath + "/")) {
                return i;
            }
        }
        return -1;
    };

    // Determine if the clicked leaf is inside a smart folder (for single-frame Exclude action).
    const QString clickedLeafPath = (clickedIsLeaf && !clickedPaths.isEmpty()) ? clickedPaths.first() : QString();
    const int clickedLeafSmartFolderIdx = clickedLeafPath.isEmpty() ? -1 : smartFolderIndexFor(clickedLeafPath);
    const bool clickedLeafIsSmartFolder = clickedLeafSmartFolderIdx >= 0;

    QMenu menu(this);
    // Helper: insert a separator only when there are preceding items.
    bool hadItems = false;
    auto addSep = [&]() { if (hadItems) { menu.addSeparator(); hadItems = false; } };

    // ── Section 0: source-specific actions (top-level source nodes only) ───
    QAction* openFolderAction   = nullptr;
    QAction* syncSourceAction   = nullptr;
    QAction* removeSourceAction = nullptr;
    if (clickedIsSourceNode) {
        // Open the cached folder where the source's files live (so the user can edit them).
        QString openFolderPath;
        if (clickedSourceIndex >= 0 && clickedSourceIndex < m_session->sources.size()) {
            const ProjectSource& src = m_session->sources[clickedSourceIndex];
            if (!src.cachedFolderPath.isEmpty() && QDir(src.cachedFolderPath).exists())
                openFolderPath = src.cachedFolderPath;
        }
        if (!openFolderPath.isEmpty()) {
            openFolderAction = menu.addAction(tr("Open Folder"));
            openFolderAction->setData(openFolderPath);
            hadItems = true;
        }
        syncSourceAction   = menu.addAction(tr("Sync \"%1\"").arg(clickedItem->text(0)));
        removeSourceAction = menu.addAction(tr("Remove \"%1\"").arg(clickedItem->text(0)));
        hadItems = true;
        addSep();
    }

    // ── Section 1: remove / exclude from layout ────────────────────────────
    QAction* deleteFrameAction    = nullptr;
    QAction* excludeFrameAction   = nullptr;
    QAction* deleteGroupAction    = nullptr;
    QAction* deleteSelectedAction = nullptr;

    if (clickedIsLeaf) {
        if (clickedLeafIsSmartFolder) {
            excludeFrameAction = menu.addAction(tr("Exclude from Layout"));
        } else {
            deleteFrameAction = menu.addAction(tr("Remove from Layout"));
        }
        hadItems = true;
    }
    if (clickedIsGroup && !clickedIsSourceNode) { deleteGroupAction    = menu.addAction(tr("Remove group from Layout"));    hadItems = true; }
    if (hasChecked)                             { deleteSelectedAction = menu.addAction(tr("Remove selected from Layout")); hadItems = true; }

    // ── Section 2: add frames (not applicable to source nodes) ────────────
    QAction* addFramesAction = nullptr;
    if (!clickedIsSourceNode) {
        addSep();
        const QString addLabel = clickedIsGroup
            ? tr("Add frames into '%1'...").arg(clickedItem->text(0))
            : tr("Add frames...");
        addFramesAction = menu.addAction(addLabel);
        hadItems = true;
    }

    // ── Section 3: timelines ───────────────────────────────────────────────
    QAction* createTimelineFromGroupAction    = nullptr;
    QAction* autoCreateTimelinesAction        = nullptr;
    QAction* createTimelineFromSelectedAction = nullptr;
    QAction* addToTimelineAction              = nullptr;

    if (clickedIsGroup || hasChecked) {
        addSep();
        // "Create timeline from group" doesn't apply to source nodes; use Auto-create instead.
        if (clickedIsGroup && !clickedIsSourceNode) { createTimelineFromGroupAction    = menu.addAction(tr("Create timeline from group"));          hadItems = true; }
        // For source nodes always offer Auto-create (name-pattern grouping works on flat structures too).
        if (clickedGroupHasGroups || clickedIsSourceNode) { autoCreateTimelinesAction  = menu.addAction(tr("Auto-create timelines"));               hadItems = true; }
        if (hasChecked)                             { createTimelineFromSelectedAction = menu.addAction(tr("Create timeline from selected frames")); hadItems = true; }
        if (hasChecked && hasTimeline)              { addToTimelineAction              = menu.addAction(tr("Add selected to current timeline"));    hadItems = true; }
    }

    // ── Section 4: grouping (ungroup not applicable to source nodes) ───────
    QAction* groupSelectedAction = nullptr;
    QAction* ungroupAction       = nullptr;

    if (hasChecked || (clickedIsGroup && !clickedIsSourceNode)) {
        addSep();
        if (hasChecked)                             { groupSelectedAction = menu.addAction(tr("Group selected frames...")); hadItems = true; }
        if (clickedIsGroup && !clickedIsSourceNode) { ungroupAction       = menu.addAction(tr("Ungroup (move up)"));        hadItems = true; }
    }

    // ── Section 5: sync ────────────────────────────────────────────────────
    // Build path → SpritePtr map and determine reference frame early (needed to
    // populate sub-menu items before menu.exec() blocks).
    QMap<QString, SpritePtr> syncByPath;
    SpritePtr syncRefSprite;
    QMenu*            syncSubMenu       = nullptr;
    QAction*          syncPivotAction   = nullptr;
    QAction*          syncAllAction     = nullptr;
    QVector<QAction*> syncMarkerActions;

    if (clickedIsGroup && clickedPaths.size() >= 2) {
        for (const auto& model : m_session->layoutModels)
            for (const auto& s : model.sprites)
                if (s && clickedPaths.contains(s->path))
                    syncByPath[s->path] = s;

        if (syncByPath.size() >= 2) {
            // Prefer the currently selected frame if it belongs to the group.
            if (m_session->selectedSprite && syncByPath.contains(m_session->selectedSprite->path))
                syncRefSprite = m_session->selectedSprite;
            else
                syncRefSprite = syncByPath.value(clickedPaths.first());

            if (syncRefSprite) {
                const QString refLabel = syncRefSprite->name.isEmpty()
                    ? QFileInfo(syncRefSprite->path).baseName()
                    : syncRefSprite->name;
                addSep();
                syncSubMenu = menu.addMenu(tr("Sync from \"%1\"").arg(refLabel));
                hadItems = true;

                syncPivotAction = syncSubMenu->addAction(tr("Pivot"));
                for (const auto& pt : syncRefSprite->points) {
                    auto* act = syncSubMenu->addAction(pt.name);
                    act->setData(pt.name);
                    syncMarkerActions.append(act);
                }
                if (!syncRefSprite->points.isEmpty())
                    syncSubMenu->addSeparator();
                syncAllAction = syncSubMenu->addAction(tr("All properties"));
            }
        }
    }

    // ── Dispatch ───────────────────────────────────────────────────────────
    QAction* chosen = menu.exec(m_spriteTree->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if      (chosen == openFolderAction)                                  QDesktopServices::openUrl(QUrl::fromLocalFile(chosen->data().toString()));
    else if (chosen == syncSourceAction)                                  onRunLayout(true);
    else if (chosen == removeSourceAction)                                removeSource(clickedSourceIndex);
    else if (chosen == excludeFrameAction)                                onNavigatorExcludeFromSmartFolder(clickedLeafPath, clickedLeafSmartFolderIdx);
    else if (chosen == deleteFrameAction || chosen == deleteGroupAction)  onNavigatorDeleteFrames(clickedPaths);
    else if (chosen == deleteSelectedAction)                              onNavigatorDeleteFrames(checkedPaths);
    else if (chosen == addFramesAction)                                   onNavigatorAddFrames(subfolder);
    else if (chosen == createTimelineFromGroupAction)                     onNavigatorCreateTimeline(clickedPaths, clickedItem);
    else if (chosen == autoCreateTimelinesAction) {
        if (clickedIsSourceNode)
            onNavigatorAutoCreateTimelinesForSource(clickedSourceIndex);
        else
            onNavigatorAutoCreateTimelines(clickedItem);
    }
    else if (chosen == createTimelineFromSelectedAction)                  onNavigatorCreateTimeline(checkedPaths, nullptr);
    else if (chosen == addToTimelineAction)                               onNavigatorAddToTimeline(checkedPaths);
    else if (chosen == groupSelectedAction)                               onNavigatorCreateGroup(checkedPaths, subfolder);
    else if (chosen == ungroupAction)                                     onNavigatorUngroup(clickedItem);
    else if (syncRefSprite && (chosen == syncPivotAction || chosen == syncAllAction
                               || syncMarkerActions.contains(chosen))) {
        const bool doAll   = (chosen == syncAllAction);
        const bool doPivot = doAll || (chosen == syncPivotAction);
        QStringList markerNames;
        if (doAll) {
            for (const auto& pt : syncRefSprite->points)
                markerNames << pt.name;
        } else if (chosen != syncPivotAction) {
            markerNames << chosen->data().toString();
        }
        onNavigatorSyncGroup(syncByPath, syncRefSprite, clickedPaths, doPivot, markerNames);
    }
}

// ---------------------------------------------------------------------------
// Action: Exclude sprite from smart folder (adds to excludedFiles, removes from layout)
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorExcludeFromSmartFolder(const QString& absolutePath, int smartFolderIndex)
{
    if (!m_session || absolutePath.isEmpty()) return;
    if (smartFolderIndex < 0 || smartFolderIndex >= m_session->smartFolders.size()) return;

    const SmartFolder& sf = m_session->smartFolders[smartFolderIndex];
    const QString relPath = QDir(sf.path).relativeFilePath(absolutePath);
    if (relPath.startsWith("..")) {
        qWarning() << "ExcludeFromSmartFolder: path escapes smart folder:" << absolutePath;
        return;
    }

    const QStringList savedActivePaths = m_session->activeFramePaths;
    const QVector<AnimationTimeline> savedTimelines = m_session->timelines;
    const int savedTimelineIdx = m_session->selectedTimelineIndex;
    const QVector<LayoutModel> savedLayoutModels = m_session->layoutModels;

    auto postExecuteRedo = [this, absolutePath]() {
        if (m_canvas) m_canvas->removeSprites({absolutePath});
        m_statusLabel->setText(tr("Excluded from layout (file kept on disk)"));
        refreshTimelineFrames();
        refreshTimelineList();
        refreshAnimationTest();
        if (m_session->activeFramePaths.isEmpty()) {
            m_session->layoutModels.clear();
            if (m_canvas) m_canvas->clearCanvas();
            updateMainContentView();
        } else {
            scheduleLayoutRebuild(true, true);
        }
        updateUiState();
        refreshSpriteTree();
    };

    auto postExecuteUndo = [this]() {
        refreshSpriteTree();
        refreshTimelineFrames();
        refreshTimelineList();
        refreshAnimationTest();
        scheduleLayoutRebuild(true);
    };

    m_undoStack->push(new ExcludeSpriteCommand(
        &m_session->smartFolders,
        smartFolderIndex,
        relPath,
        &m_session->activeFramePaths,
        &m_session->timelines,
        &m_session->selectedTimelineIndex,
        &m_session->layoutModels,
        absolutePath,
        savedActivePaths,
        savedTimelines,
        savedTimelineIdx,
        savedLayoutModels,
        [this]() { return ensureFrameListInput(); },
        postExecuteRedo,
        postExecuteUndo
    ));
}

// ---------------------------------------------------------------------------
// Action: Add a new smart folder (via folder picker dialog)
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorAddSmartFolder()
{
    if (!m_session) return;

    const QString startDir = m_session->sourceFolder.isEmpty()
        ? QDir::homePath()
        : m_session->sourceFolder;

    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Add Source Folder"), startDir);
    if (folder.isEmpty()) return;

    const QString absFolder = QDir(folder).absolutePath();

    // Avoid duplicates
    for (const auto& sf : m_session->smartFolders) {
        if (sf.path == absFolder) return;
    }

    SmartFolder sf;
    sf.path = absFolder;
    m_session->smartFolders.append(sf);

    // Update primary sourceFolder if not already set
    if (m_session->sourceFolder.isEmpty()) {
        m_session->sourceFolder = absFolder;
    }

    initializeSourceFolderWatcher();
    m_statusLabel->setText(tr("Added source folder: %1").arg(QDir(absFolder).dirName()));

    // Trigger a sync to import the new folder's images into the layout
    if (!m_session->layoutModels.isEmpty()) {
        performFolderSync();
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
        MessageDialog::information(this, tr("Add Frames"), tr("All selected frames are already loaded."));
        return;
    }

    // Organize files by pattern before adding to session
    added = FolderSyncService::organizeNewImagesByPattern(added);

    const QStringList oldFramePaths = m_session->activeFramePaths;
    QStringList newFramePaths = oldFramePaths;
    newFramePaths.append(added);

    m_undoStack->push(new AddFramesCommand(
        &m_session->activeFramePaths,
        added,
        oldFramePaths,
        [this]() { return ensureFrameListInput(); },
        [this]() { 
            updateManualFrameLabel();
            scheduleLayoutRebuild(); 
        }
    ));

    m_statusLabel->setText(QString(tr("Adding %1 frame(s)...")).arg(added.size()));
    if (!ensureFrameListInput()) {
        m_session->activeFramePaths = oldFramePaths;
        MessageDialog::warning(this, tr("Add Frames"), tr("Could not create temporary frame list."));
        return;
    }
    updateManualFrameLabel();
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
            MessageDialog::warning(this, tr("Create Timeline"),
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
    m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));
    refreshTimelineFrames();
    refreshAnimationTest();

    m_undoStack->push(new TimelineAddCommand(
        &m_session->timelines,
        timeline,
        &m_session->selectedTimelineIndex,
        [this]() {
            refreshTimelineList();
            if (m_session->selectedTimelineIndex >= 0)
                m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));
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
        MessageDialog::warning(this, tr("Create Group"), tr("Could not create temporary frame list."));
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
        MessageDialog::warning(this, tr("Ungroup"), tr("Could not create temporary frame list."));
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
            m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));
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
        m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));
        refreshTimelineFrames();
        refreshAnimationTest();
    }
}

// ---------------------------------------------------------------------------
// Action: Auto-create timelines for a source (name-pattern grouping, source-scoped)
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorAutoCreateTimelinesForSource(int sourceIndex)
{
    if (!m_session || sourceIndex < 0 || sourceIndex >= m_session->sources.size()) return;

    const QString cleanedCache = QDir::cleanPath(
        m_session->sources[sourceIndex].cachedFolderPath);

    // Collect sprites that belong to this source.
    // Use names derived relative to cachedFolderPath so grouping is independent
    // of m_session->currentFolder at parse time and matches the navigator display.
    QVector<SpritePtr> sourceSprites;
    for (const auto& model : m_session->layoutModels) {
        for (const auto& sprite : model.sprites) {
            if (!sprite) continue;
            const QString cp = QDir::cleanPath(sprite->path);
            if (!cp.startsWith(cleanedCache + QLatin1Char('/'))) continue;

            const QString rel  = cp.mid(cleanedCache.length() + 1);
            const QString dir  = QFileInfo(rel).path();
            const QString base = QFileInfo(rel).baseName();
            const QString localName = (dir.isEmpty() || dir == QLatin1String("."))
                                      ? base : dir + QLatin1Char('/') + base;

            auto namedSprite = std::make_shared<Sprite>(*sprite);
            namedSprite->name = localName;
            sourceSprites.append(namedSprite);
        }
    }

    if (sourceSprites.isEmpty()) {
        MessageDialog::information(this, tr("Auto-create Timelines"),
            tr("No frames found for source \"%1\".")
                .arg(m_session->sources[sourceIndex].name));
        return;
    }

    const QVector<AnimationTimeline> oldTimelines = m_session->timelines;
    const int oldSelection = m_session->selectedTimelineIndex;

    int focusIndex = -1;
    QString status;
    bool changed = TimelineGenerationService::generateFromSprites(
        sourceSprites,
        m_session->timelines,
        focusIndex,
        [this](const QString& name) {
            return TimelineUi::askTimelineConflictResolution(this, name);
        },
        status);

    if (changed) {
        if (focusIndex >= 0)
            m_session->selectedTimelineIndex = focusIndex;

        auto postExecute = [this]() {
            refreshTimelineList();
            if (m_session->selectedTimelineIndex >= 0)
                m_timelineList->setCurrentItem(
                    timelineItemForIndex(m_session->selectedTimelineIndex));
            else
                onTimelineSelectionChanged();
            fitAnimationToViewport();
            refreshAnimationTest();
        };

        m_undoStack->push(new TimelinesUpdateCommand(
            &m_session->timelines,
            oldTimelines,
            m_session->timelines,
            oldSelection,
            m_session->selectedTimelineIndex,
            &m_session->selectedTimelineIndex,
            postExecute,
            tr("Auto-create Timelines")));

        postExecute();
        m_statusLabel->setText(status);
    } else {
        MessageDialog::information(this, tr("Auto-create Timelines"), status);
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
    int visibleLeaves = 0;
    int totalLeaves = 0;
    QTreeWidgetItemIterator it2(m_spriteTree);
    while (*it2) {
        QTreeWidgetItem* item = *it2;
        const bool isLeaf = item->data(0, Qt::UserRole).isValid();
        if (isLeaf) {
            ++totalLeaves;
            if (itemsToShow.contains(item)) ++visibleLeaves;
        }
        item->setHidden(!itemsToShow.contains(item));
        ++it2;
    }

    if (m_spriteFilterResultLabel) {
        const bool filtering = !text.isEmpty();
        m_spriteFilterResultLabel->setVisible(filtering);
        if (filtering) {
            if (visibleLeaves == 0)
                m_spriteFilterResultLabel->setText(tr("No results"));
            else
                m_spriteFilterResultLabel->setText(tr("%1/%2").arg(visibleLeaves).arg(totalLeaves));
        }
    }
}

// ---------------------------------------------------------------------------
// Action: Sync pivot / markers from one frame to all others in the group.
// Called after the user picks an item from the "Sync from …" submenu.
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorSyncGroup(const QMap<QString, SpritePtr>& byPath,
                                      SpritePtr refSprite,
                                      const QStringList& groupPaths,
                                      bool syncPivot,
                                      const QStringList& syncMarkerNames)
{
    if (!m_session || !refSprite) return;
    if (!syncPivot && syncMarkerNames.isEmpty()) return;

    auto postExecute = [this]() {
        AnimationPreviewService::invalidateBounds();
        if (!m_session || !m_session->selectedSprite) return;
        if (m_pivotXSpin) { m_pivotXSpin->blockSignals(true); m_pivotXSpin->setValue(m_session->selectedSprite->pivotX); m_pivotXSpin->blockSignals(false); }
        if (m_pivotYSpin) { m_pivotYSpin->blockSignals(true); m_pivotYSpin->setValue(m_session->selectedSprite->pivotY); m_pivotYSpin->blockSignals(false); }
    };

    const int commandCount = (syncPivot ? 1 : 0) + syncMarkerNames.size();
    const bool useMacro = commandCount > 1;
    if (useMacro) m_undoStack->beginMacro(tr("Sync Group"));

    // ── Pivot ────────────────────────────────────────────────────────────────
    if (syncPivot) {
        QVector<QPair<SpritePtr, QPair<int,int>>> targets;
        for (const QString& path : groupPaths) {
            if (!byPath.contains(path) || byPath[path] == refSprite) continue;
            SpritePtr tgt = byPath[path];
            targets.append({tgt, {tgt->pivotX, tgt->pivotY}});
            tgt->pivotX = refSprite->pivotX;
            tgt->pivotY = refSprite->pivotY;
        }
        if (!targets.isEmpty()) {
            AnimationPreviewService::invalidateBounds();
            if (m_session->selectedSprite) {
                for (const auto& pair : targets) {
                    if (pair.first == m_session->selectedSprite) {
                        if (m_pivotXSpin) { m_pivotXSpin->blockSignals(true); m_pivotXSpin->setValue(refSprite->pivotX); m_pivotXSpin->blockSignals(false); }
                        if (m_pivotYSpin) { m_pivotYSpin->blockSignals(true); m_pivotYSpin->setValue(refSprite->pivotY); m_pivotYSpin->blockSignals(false); }
                        break;
                    }
                }
            }
            m_undoStack->push(new ApplyPivotToFramesCommand(
                targets, refSprite->pivotX, refSprite->pivotY, postExecute));
        }
    }

    // ── Markers ──────────────────────────────────────────────────────────────
    for (const QString& markerName : syncMarkerNames) {
        const NamedPoint* srcPt = nullptr;
        for (const auto& p : refSprite->points)
            if (p.name == markerName) { srcPt = &p; break; }
        if (!srcPt) continue;

        QVector<QPair<SpritePtr, std::optional<NamedPoint>>> targets;
        for (const QString& path : groupPaths) {
            if (!byPath.contains(path) || byPath[path] == refSprite) continue;
            SpritePtr tgt = byPath[path];
            std::optional<NamedPoint> oldPt;
            for (const auto& p : tgt->points)
                if (p.name == markerName) { oldPt = p; break; }
            targets.append({tgt, oldPt});
            // Apply immediately (command skips first redo).
            auto it = std::find_if(tgt->points.begin(), tgt->points.end(),
                [&markerName](const NamedPoint& p){ return p.name == markerName; });
            if (it != tgt->points.end()) *it = *srcPt;
            else tgt->points.append(*srcPt);
        }
        if (!targets.isEmpty())
            m_undoStack->push(new ApplyMarkerToFramesCommand(targets, *srcPt, postExecute));
    }

    AnimationPreviewService::invalidateBounds();
    if (useMacro) m_undoStack->endMacro();
}
