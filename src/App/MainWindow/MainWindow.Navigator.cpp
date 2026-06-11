#include "MainWindow.h"
#include "AtlasesManagementWorkspace.h"
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
// Helper: compute the absolute filesystem path for a navigation tree item.
//   Leaf  → the directory containing the sprite file.
//   Group → base path from source node + all group-node text segments.
// ---------------------------------------------------------------------------
QString MainWindow::absolutePathForNavItem(QTreeWidgetItem* item) const
{
    if (!item || !m_session) return {};

    // Leaf: return the directory that contains the sprite file.
    if (item->childCount() == 0) {
        auto sprite = item->data(0, Qt::UserRole).value<SpritePtr>();
        if (sprite && !sprite->path.isEmpty())
            return QFileInfo(sprite->path).absolutePath();
        return {};
    }

    // Group: walk up the parent chain collecting folder names until we hit a source node.
    QStringList parts;
    QTreeWidgetItem* node = item;
    while (node) {
        // Source node: has UserRole+1 set (source index) and no UserRole (not a sprite).
        if (node->data(0, Qt::UserRole + 1).isValid() && !node->data(0, Qt::UserRole).isValid()) {
            const int sourceIdx = node->data(0, Qt::UserRole + 1).toInt();
            QString base;
            if (sourceIdx >= 0 && sourceIdx < m_session->sources.size())
                base = m_session->sources[sourceIdx].cachedFolderPath;
            if (base.isEmpty())
                base = m_session->sourceFolder;
            if (base.isEmpty()) return {};
            return parts.isEmpty() ? base : base + "/" + parts.join('/');
        }
        // Non-leaf group node: prepend its text segment.
        if (!node->data(0, Qt::UserRole).isValid())
            parts.prepend(node->text(0));
        node = node->parent();
    }

    // Fallback when no source node found in the parent chain.
    const QString base = m_session->sourceFolder;
    if (base.isEmpty()) return {};
    return parts.isEmpty() ? base : base + "/" + parts.join('/');
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
                                && m_session->selectedTimelineIndex < m_session->activeAtlas().timelines.size();

    // Special item types stored in UserRole+2 (int):
    //   1 = hidden-folder placeholder  2 = excluded item
    // UserRole+3 = source index, UserRole+4 = relative path within source.
    const int clickedItemType = clickedItem
        ? clickedItem->data(0, Qt::UserRole + 2).toInt() : 0;
    const bool clickedIsHiddenPlaceholder = (clickedItemType == 1);
    const bool clickedIsExcludedItem      = (clickedItemType == 2);
    const int  clickedSpecialSourceIdx    = (clickedItemType > 0 && clickedItem)
        ? clickedItem->data(0, Qt::UserRole + 3).toInt() : -1;
    const QString clickedSpecialRelPath   = (clickedItemType > 0 && clickedItem)
        ? clickedItem->data(0, Qt::UserRole + 4).toString() : QString();

    // Hidden-placeholder (1), excluded item (2), and excluded-section header (3)
    // each get a minimal single-action menu — no timeline, grouping, or other
    // sections apply to them.
    if (clickedItemType > 0) {
        QMenu menu(this);
        if (clickedItemType == 1) {
            // Hidden-folder placeholder: offer Unhide.
            auto* action = menu.addAction(tr("Unhide \"%1\"").arg(clickedItem->text(0)));
            if (menu.exec(m_spriteTree->viewport()->mapToGlobal(pos)) == action)
                onNavigatorUnhideGroup(clickedSpecialSourceIdx, clickedSpecialRelPath);
        } else if (clickedItemType == 2) {
            // Excluded item: offer Re-include for this entry.
            auto* action = menu.addAction(
                tr("Re-include \"%1\"").arg(QFileInfo(clickedSpecialRelPath).fileName()));
            if (menu.exec(m_spriteTree->viewport()->mapToGlobal(pos)) == action)
                onNavigatorReincludeFromSource(clickedSpecialSourceIdx, clickedSpecialRelPath);
        } else if (clickedItemType == 3) {
            // Excluded-section (trash) header: offer Re-include all.
            // Collect leaf data recursively before any mutation (refreshSpriteTree invalidates items).
            QVector<QPair<int, QString>> toReinclude;
            std::function<void(QTreeWidgetItem*)> collectExcluded = [&](QTreeWidgetItem* node) {
                for (int i = 0; i < node->childCount(); ++i) {
                    QTreeWidgetItem* child = node->child(i);
                    if (child->data(0, Qt::UserRole + 2).toInt() == 2) {
                        toReinclude.append({child->data(0, Qt::UserRole + 3).toInt(),
                                            child->data(0, Qt::UserRole + 4).toString()});
                    } else {
                        collectExcluded(child); // recurse into folder nodes
                    }
                }
            };
            collectExcluded(clickedItem);
            if (!toReinclude.isEmpty()) {
                auto* action = menu.addAction(tr("Re-include all (%1)").arg(toReinclude.size()));
                if (menu.exec(m_spriteTree->viewport()->mapToGlobal(pos)) == action) {
                    m_undoStack->beginMacro(tr("Re-include All Excluded"));
                    for (const auto& [srcIdx, rel] : toReinclude)
                        onNavigatorReincludeFromSource(srcIdx, rel);
                    m_undoStack->endMacro();
                }
            }
        }
        return;
    }

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
    QAction* syncLayoutAction   = nullptr;
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
        if (clickedSourceIndex >= 0 && clickedSourceIndex < m_session->sources.size()) {
            const ProjectSource& src = m_session->sources[clickedSourceIndex];
            const bool isUrl    = (src.type == SourceType::Url);
            const bool hasCopy  = !src.cachedFolderPath.isEmpty();
            const bool isFolder = (src.type == SourceType::Folder);
            syncSourceAction = menu.addAction(tr("Sync Source to Layout"));
            syncSourceAction->setEnabled(!isUrl && (isFolder || hasCopy));
            syncLayoutAction = menu.addAction(tr("Sync Layout to Source"));
            syncLayoutAction->setEnabled(!isUrl && hasCopy);
        }
        removeSourceAction = menu.addAction(tr("Remove \"%1\"").arg(clickedItem->text(0)));
        hadItems = true;
        addSep();
    }

    // ── Section 1: remove / exclude from layout ────────────────────────────
    QAction* deleteFrameAction    = nullptr;
    QAction* excludeFrameAction   = nullptr;
    QMenu*   hideGroupMenu              = nullptr;
    QAction* hideGroupWithDescAction    = nullptr;
    QAction* hideGroupOnlyAction        = nullptr;
    QAction* deleteSelectedAction = nullptr;

    if (clickedIsLeaf) {
        if (clickedLeafIsSmartFolder) {
            excludeFrameAction = menu.addAction(tr("Exclude from Layout"));
        } else {
            deleteFrameAction = menu.addAction(tr("Exclude from Layout"));
        }
        hadItems = true;
    }
    if (clickedIsGroup && !clickedIsSourceNode) {
        hideGroupMenu           = menu.addMenu(tr("Hide..."));
        hideGroupWithDescAction = hideGroupMenu->addAction(tr("with descendants"));
        hideGroupOnlyAction     = hideGroupMenu->addAction(tr("group only"));
        hadItems = true;
    }
    if (hasChecked)                             { deleteSelectedAction = menu.addAction(tr("Exclude selected from Layout")); hadItems = true; }

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

    if (m_activeWorkspace == Workspace::FrameAnimation && (clickedIsGroup || hasChecked)) {
        addSep();
        // "Create timeline from group" doesn't apply to source nodes; use Auto-create instead.
        if (clickedIsGroup && !clickedIsSourceNode) { createTimelineFromGroupAction    = menu.addAction(tr("Create timeline from group"));          hadItems = true; }
        // For source nodes always offer Auto-create (name-pattern grouping works on flat structures too).
        if (clickedGroupHasGroups || clickedIsSourceNode) { autoCreateTimelinesAction  = menu.addAction(tr("Auto-create timelines"));               hadItems = true; }
        if (hasChecked)                             { createTimelineFromSelectedAction = menu.addAction(tr("Create timeline from selected frames")); hadItems = true; }
        if (hasChecked && hasTimeline)              { addToTimelineAction              = menu.addAction(tr("Add selected to current timeline"));    hadItems = true; }
    }

    // ── Add Source submenu (always present, bottom of menu) ───────────────
    QAction* addSourceFolderAction  = nullptr;
    QAction* addSourceImageAction   = nullptr;
    QAction* addSourceArchiveAction = nullptr;
    QAction* addSourceUrlAction     = nullptr;
    {
        addSep();
        QMenu* addSourceMenu   = menu.addMenu(tr("Add Source"));
        addSourceFolderAction  = addSourceMenu->addAction(tr("Folder..."));
        addSourceImageAction   = addSourceMenu->addAction(tr("Image..."));
        addSourceArchiveAction = addSourceMenu->addAction(tr("Archive..."));
        addSourceUrlAction     = addSourceMenu->addAction(tr("URL..."));
        hadItems = true;
    }

    // ── Dispatch ──────────────────────────────────────────────────────────
    QAction* chosen = menu.exec(m_spriteTree->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if      (chosen == openFolderAction)                                  QDesktopServices::openUrl(QUrl::fromLocalFile(chosen->data().toString()));
    else if (chosen == syncSourceAction)                                  onSyncSourceRequested(clickedSourceIndex);
    else if (chosen == syncLayoutAction)                                  onSyncLayoutRequested(clickedSourceIndex);
    else if (chosen == removeSourceAction)                                removeSource(clickedSourceIndex);
    else if (chosen == excludeFrameAction)                                onNavigatorExcludeFromSmartFolder(clickedLeafPath, clickedLeafSmartFolderIdx);
    else if (chosen == deleteFrameAction)                                 onNavigatorDeleteFrames(clickedPaths);
    else if (chosen == hideGroupWithDescAction)                           onNavigatorExcludeGroup(clickedItem);
    else if (chosen == hideGroupOnlyAction)                               onNavigatorHideGroupOnly(clickedItem);
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
    else if (chosen == addSourceFolderAction)  onLoadFolder();
    else if (chosen == addSourceImageAction) {
        const QString filter = tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tga *.dds)");
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Add Image"), m_session ? m_session->currentFolder : QString(), filter);
        if (!path.isEmpty()) {
            const DropAction action = confirmDropAction(path);
            if (action != DropAction::Cancel) loadImageWithFrameDetection(path, action);
        }
    }
    else if (chosen == addSourceArchiveAction) {
        const QString filter = tr("Archives (*.zip *.tar *.tar.gz *.tar.bz2 *.tar.xz)");
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Add Archive"), m_session ? m_session->currentFolder : QString(), filter);
        if (!path.isEmpty()) {
            const DropAction action = confirmDropAction(path);
            if (action != DropAction::Cancel) loadProject(path, action);
        }
    }
    else if (chosen == addSourceUrlAction) onLoadFromUrl();
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
    const QVector<AnimationTimeline> savedTimelines = m_session->activeAtlas().timelines;
    const int savedTimelineIdx = m_session->selectedTimelineIndex;
    const QVector<LayoutModel> savedLayoutModels = m_session->activeAtlas().layoutModels;

    auto postExecuteRedo = [this, absolutePath]() {
        if (m_canvas) m_canvas->removeSprites({absolutePath});
        m_statusLabel->setText(tr("Excluded from layout (file kept on disk)"));
        refreshTimelineFrames();
        refreshTimelineList();
        refreshAnimationTest();
        if (m_session->activeFramePaths.isEmpty()) {
            m_session->activeAtlas().layoutModels.clear();
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
        &m_session->activeAtlas().timelines,
        &m_session->selectedTimelineIndex,
        &m_session->activeAtlas().layoutModels,
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
    if (!m_session->activeAtlas().layoutModels.isEmpty()) {
        performFolderSync();
    }
}

// ---------------------------------------------------------------------------
// Action: DEL key — mirrors the context menu "Exclude from Layout" / "Hide"
// logic so the keyboard shortcut behaves identically to right-click actions.
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorExcludeKey(QTreeWidgetItem* item)
{
    if (!item || !m_session) return;

    // Ignore special items (hidden-folder placeholders, excluded items, etc.)
    if (item->data(0, Qt::UserRole + 2).toInt() != 0) return;

    // Source nodes (top-level folder roots) store their index in UserRole+1
    // but have no UserRole sprite data. DEL has no defined action for them.
    const bool isSourceNode = item->data(0, Qt::UserRole + 1).isValid()
                              && !item->data(0, Qt::UserRole).isValid();
    if (isSourceNode) return;

    const bool isLeaf  = item->childCount() == 0 && item->data(0, Qt::UserRole).isValid();
    const bool isGroup = item->childCount() > 0;

    if (isGroup) {
        // Same as context menu "Hide with descendants"
        onNavigatorExcludeGroup(item);
    } else if (isLeaf) {
        auto sprite = item->data(0, Qt::UserRole).value<SpritePtr>();
        if (!sprite || sprite->path.isEmpty()) return;

        // Check if this leaf belongs to a smart folder
        int sfIdx = -1;
        for (int i = 0; i < m_session->smartFolders.size(); ++i) {
            const QString& sfPath = m_session->smartFolders[i].path;
            if (!sfPath.isEmpty() && sprite->path.startsWith(sfPath + "/")) {
                sfIdx = i;
                break;
            }
        }

        if (sfIdx >= 0) {
            // Same as context menu "Exclude from Layout" on a smart-folder leaf
            onNavigatorExcludeFromSmartFolder(sprite->path, sfIdx);
        } else {
            // Same as context menu "Exclude from Layout" on a regular leaf
            onNavigatorDeleteFrames({sprite->path});
        }
    }
}

// ---------------------------------------------------------------------------
// Action: Delete selected frames (persists to excludedFiles when source found)
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorDeleteFrames(const QStringList& paths)
{
    if (m_session) {
        for (const QString& p : paths)
            addToExcludedFiles(p);
        syncExcludedAtlas();
        if (m_atlasesManagementWorkspace && m_atlasesManagementWorkspaceActive)
            m_atlasesManagementWorkspace->refreshSpriteList(m_session->atlases);
    }
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
    if (tlIdx < 0 || tlIdx >= m_session->activeAtlas().timelines.size()) return;

    auto postExecute = [this]() {
        refreshTimelineFrames();
        refreshTimelineList();
        refreshAnimationTest();
    };

    const bool useMacro = paths.size() > 1;
    if (useMacro) m_undoStack->beginMacro(tr("Add Frames to Timeline"));
    for (const QString& path : paths) {
        const int insertIdx = m_session->activeAtlas().timelines[tlIdx].frames.size();
        AnimationTimelineOps::dropFrame(m_session->activeAtlas().timelines, tlIdx, path, -1);
        m_undoStack->push(new TimelineFrameDropCommand(
            &m_session->activeAtlas().timelines, tlIdx, path, insertIdx, postExecute));
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
    m_session->activeAtlas().timelines.append(timeline);
    m_session->selectedTimelineIndex = m_session->activeAtlas().timelines.size() - 1;

    refreshTimelineList();
    m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));
    refreshTimelineFrames();
    refreshAnimationTest();

    m_undoStack->push(new TimelineAddCommand(
        &m_session->activeAtlas().timelines,
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
// Action: Hide group only — remove this folder level from the navigator view.
// The folder's children appear directly under the parent node.
// No disk changes.  Undo restores the folder node.
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorHideGroupOnly(QTreeWidgetItem* groupItem)
{
    if (!groupItem || groupItem->childCount() == 0 || !m_session) return;

    const QString groupAbsPath = absolutePathForNavItem(groupItem);
    if (groupAbsPath.isEmpty()) return;

    if (!QDir(groupAbsPath).exists()) {
        MessageDialog::warning(this, tr("Hide Group"),
            tr("\"%1\" is a virtual animation group and cannot be hidden separately.")
                .arg(groupItem->text(0)));
        return;
    }

    // Find which source this folder belongs to.
    int sourceIdx = -1;
    QString relPath;
    for (int i = 0; i < m_session->sources.size(); ++i) {
        const QString cleanedCache = QDir::cleanPath(
            m_session->sources[i].cachedFolderPath.isEmpty()
                ? m_session->sources[i].originalPath
                : m_session->sources[i].cachedFolderPath);
        if (groupAbsPath.startsWith(cleanedCache + '/')) {
            relPath   = groupAbsPath.mid(cleanedCache.length() + 1);
            sourceIdx = i;
            break;
        }
    }
    if (sourceIdx < 0) {
        MessageDialog::warning(this, tr("Hide Group"),
            tr("Could not determine source for \"%1\".").arg(groupItem->text(0)));
        return;
    }

    auto& src = m_session->sources[sourceIdx];
    if (src.hiddenFolders.contains(relPath)) {
        MessageDialog::information(this, tr("Hide Group"),
            tr("\"%1\" is already hidden. Use Undo to restore it.").arg(groupItem->text(0)));
        return;
    }

    src.hiddenFolders.append(relPath);
    refreshSpriteTree();

    m_undoStack->push(new NavigatorHideFolderCommand(
        &src.hiddenFolders, relPath, true,
        [this]() { refreshSpriteTree(); }
    ));
}

// ---------------------------------------------------------------------------
// Action: Unhide group — remove the relative path from the source's
// hiddenFolders so the folder reappears as a normal node.  Undoable.
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorUnhideGroup(int sourceIdx, const QString& relPath)
{
    if (!m_session || sourceIdx < 0 || sourceIdx >= m_session->sources.size()) return;
    auto& src = m_session->sources[sourceIdx];
    if (!src.hiddenFolders.contains(relPath)) return;

    src.hiddenFolders.removeAll(relPath);
    refreshSpriteTree();

    m_undoStack->push(new NavigatorHideFolderCommand(
        &src.hiddenFolders, relPath, false,
        [this]() { refreshSpriteTree(); }
    ));
}

// ---------------------------------------------------------------------------
// Action: Exclude group with descendants — add all descendant sprite paths
// to the source's excludedFiles list (persistent, survives project reload)
// and remove them from the current layout.  Undoable.
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorExcludeGroup(QTreeWidgetItem* groupItem)
{
    if (!groupItem || !m_session) return;

    const QString groupAbsPath = absolutePathForNavItem(groupItem);
    if (groupAbsPath.isEmpty()) return;

    // Find which source this group belongs to.
    int sourceIdx = -1;
    QString cleanedCache;
    for (int i = 0; i < m_session->sources.size(); ++i) {
        cleanedCache = QDir::cleanPath(
            m_session->sources[i].cachedFolderPath.isEmpty()
                ? m_session->sources[i].originalPath
                : m_session->sources[i].cachedFolderPath);
        if (groupAbsPath.startsWith(cleanedCache + '/') || groupAbsPath == cleanedCache) {
            sourceIdx = i;
            break;
        }
    }
    if (sourceIdx < 0) {
        MessageDialog::warning(this, tr("Hide"),
            tr("Could not determine source for \"%1\".").arg(groupItem->text(0)));
        return;
    }

    const QStringList absPaths = collectDescendantSpritePaths(groupItem);
    if (absPaths.isEmpty()) return;

    // Compute relative paths within the source.
    QStringList relPaths;
    for (const QString& absPath : absPaths) {
        const QString cleaned = QDir::cleanPath(absPath);
        if (cleaned.startsWith(cleanedCache + '/'))
            relPaths.append(cleaned.mid(cleanedCache.length() + 1));
    }
    if (relPaths.isEmpty()) return;

    auto& src        = m_session->sources[sourceIdx];
    const QStringList savedActivePaths = m_session->activeFramePaths;

    // Add to excludedFiles.
    for (const QString& rel : relPaths)
        if (!src.excludedFiles.contains(rel)) src.excludedFiles.append(rel);

    // Remove from activeFramePaths.
    const QSet<QString> toRemove(absPaths.begin(), absPaths.end());
    QStringList newActivePaths;
    for (const QString& p : m_session->activeFramePaths)
        if (!toRemove.contains(p)) newActivePaths.append(p);
    m_session->activeFramePaths = newActivePaths;

    if (!ensureFrameListInput()) {
        for (const QString& rel : relPaths) src.excludedFiles.removeAll(rel);
        m_session->activeFramePaths = savedActivePaths;
        MessageDialog::warning(this, tr("Hide"), tr("Could not create temporary frame list."));
        return;
    }
    scheduleLayoutRebuild();
    refreshSpriteTree();

    m_undoStack->push(new ExcludeGroupFromSourceCommand(
        &src.excludedFiles, &m_session->activeFramePaths,
        relPaths, savedActivePaths, newActivePaths,
        true,
        [this]() { return ensureFrameListInput(); },
        [this]() { scheduleLayoutRebuild(false); refreshSpriteTree(); }
    ));
}

// ---------------------------------------------------------------------------
// Action: Re-include a previously excluded sprite — removes it from the
// source's excludedFiles list and adds it back to the layout.  Undoable.
// ---------------------------------------------------------------------------
void MainWindow::onNavigatorReincludeFromSource(int sourceIdx, const QString& relPath)
{
    if (!m_session || sourceIdx < 0 || sourceIdx >= m_session->sources.size()) return;
    auto& src = m_session->sources[sourceIdx];
    if (!src.excludedFiles.contains(relPath)) return;

    const QString cleanedCache = QDir::cleanPath(
        src.cachedFolderPath.isEmpty() ? src.originalPath : src.cachedFolderPath);
    const QString absPath = cleanedCache + '/' + relPath;

    const QStringList savedActivePaths = m_session->activeFramePaths;

    src.excludedFiles.removeAll(relPath);

    QStringList newActivePaths = m_session->activeFramePaths;
    if (QFile::exists(absPath) && !newActivePaths.contains(absPath))
        newActivePaths.append(absPath);
    m_session->activeFramePaths = newActivePaths;

    if (!ensureFrameListInput()) {
        src.excludedFiles.append(relPath);
        m_session->activeFramePaths = savedActivePaths;
        MessageDialog::warning(this, tr("Re-include"), tr("Could not create temporary frame list."));
        return;
    }

    // Re-add to the neutral atlas so it appears in the Atlases workspace.
    const int neutralIdx = m_session->neutralAtlasIndex();
    if (neutralIdx >= 0 && neutralIdx < m_session->atlases.size()) {
        AtlasEntry& neutral = m_session->atlases[neutralIdx];
        if (!neutral.spritePaths.contains(absPath))
            neutral.spritePaths.append(absPath);
    }
    syncExcludedAtlas();
    if (m_atlasesManagementWorkspace && m_atlasesManagementWorkspaceActive)
        m_atlasesManagementWorkspace->refreshSpriteList(m_session->atlases);

    scheduleLayoutRebuild();
    refreshSpriteTree();

    m_undoStack->push(new ExcludeGroupFromSourceCommand(
        &src.excludedFiles, &m_session->activeFramePaths,
        {relPath}, savedActivePaths, newActivePaths,
        false,
        [this]() { return ensureFrameListInput(); },
        [this]() { scheduleLayoutRebuild(false); refreshSpriteTree(); }
    ));
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

        m_session->activeAtlas().timelines.append(timeline);
        m_session->selectedTimelineIndex = m_session->activeAtlas().timelines.size() - 1;

        m_undoStack->push(new TimelineAddCommand(
            &m_session->activeAtlas().timelines, timeline,
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
    for (const auto& model : m_session->activeAtlas().layoutModels) {
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

    const QVector<AnimationTimeline> oldTimelines = m_session->activeAtlas().timelines;
    const int oldSelection = m_session->selectedTimelineIndex;

    int focusIndex = -1;
    QString status;
    bool changed = TimelineGenerationService::generateFromSprites(
        sourceSprites,
        m_session->activeAtlas().timelines,
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
            &m_session->activeAtlas().timelines,
            oldTimelines,
            m_session->activeAtlas().timelines,
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
    // Delegate to NavigatorPanel when available
    if (m_navigatorPanel) {
        m_navigatorPanel->applyFilter(text);
        return;
    }

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

