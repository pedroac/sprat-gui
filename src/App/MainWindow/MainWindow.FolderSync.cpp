/// @file MainWindow.FolderSync.cpp
/// Source folder watching and syncing: watcher events, manual and periodic sync, and keeping the sprites folder in step with the active frames.

#include "MainWindow.h"
#include "NineSliceWorkspace.h"
#include "AnimationPreviewPanel.h"
#include "ExportWorkspace.h"
#include "FrameAnimationWorkspace.h"
#include "SpriteEditorPanel.h"
#include "NavigatorTreeWidget.h"
#include "LayoutOrchestrator.h"
#include "CliSetupController.h"
#include "ProjectController.h"
#include "PackedAtlasView.h"
#include "AtlasesManagementWorkspace.h"
#include "UndoCommands.h"
#include "AnimationCanvas.h"
#include "MarkersDialog.h"
#include "LayoutCanvas.h"
#include "NavigatorPanel.h"
#include "SettingsDialog.h"
#include "CliToolsConfig.h"
#include "LayoutParser.h"
#include "ProjectPayloadCodec.h"
#include "AutosaveProjectStore.h"
#include "AnimationTimelineOps.h"
#include "AnimationPlaybackService.h"
#include "AnimationPreviewService.h"
#include "AnimationExportService.h"
#include "SpriteSelectionPresenter.h"
#include "CliToolsUi.h"
#include "MainWindowUiState.h"
#include "SettingsCoordinator.h"
#include "ProjectFileLoader.h"
#include "TimelineGenerationService.h"
#include "ProjectSaveService.h"
#include "TimelineUi.h"
#include "ProfilesDialog.h"
#include "MessageDialog.h"
#include "SpratProfilesConfig.h"
#include "FolderSyncService.h"
#include "SpriteNameUtils.h"
#include "AppConstants.h"
#ifdef Q_OS_WASM
#include "WasmFileDialog.h"
#include "WasmFolderBrowserDialog.h"
#endif
#ifndef Q_OS_WASM
#include "UpdateChecker.h"
#include "UpdateDialog.h"
#endif
#include <algorithm>
#include <memory>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <QApplication>
#include <QMenu>
#include <QAction>
#include <QSet>
#include <QStackedWidget>
#include <QResizeEvent>
#include <QProgressBar>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QProcess>
#include <QFile>
#include <QTextStream>
#include <QIODevice>
#include <QUuid>
#include <QDesktopServices>
#include <QUrl>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QThread>
#include <QSpinBox>
#include <QUndoStack>
#include <QShortcut>
#include <QToolButton>
#include <QMenu>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTime>
#include <QDialog>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QPainter>
#include <QImage>
#include <QDirIterator>
#include "ArchiveExtractor.h"
#include <QAtomicPointer>

namespace {
bool isUnderSpratTrash(const QString& sourceFolder, const QString& path) {
    if (sourceFolder.isEmpty() || path.isEmpty()) {
        return false;
    }

    const QString trashRoot = QDir(sourceFolder).filePath(".sprat-trash");
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    const QString absoluteTrashRoot = QFileInfo(trashRoot).absoluteFilePath();

    return absolutePath == absoluteTrashRoot
        || absolutePath.startsWith(absoluteTrashRoot + '/');
}
} // namespace

void MainWindow::onFolderWatcherFilesAdded(const QStringList& paths) {
    qInfo() << "[Watch] Files added detected:" << paths.count() << "files";
    qInfo() << "[Watch] Current sync mode:" << (int)m_settings.syncMode;

    if (m_settings.syncMode != SyncMode::Watch) {
        qWarning() << "[Watch] Not in Watch mode, ignoring";
        return;
    }

    // Check if these are files we already know about (e.g. added via GUI)
    bool hasNewFiles = false;
    for (const QString& path : paths) {
        if (isUnderSpratTrash(m_session->sourceFolder, path)) {
            qInfo() << "[Watch] Ignoring add inside .sprat-trash:" << path;
            continue;
        }
        if (!m_session->activeFramePaths.contains(path)) {
            hasNewFiles = true;
            break;
        }
    }

    if (!hasNewFiles) {
        qInfo() << "[Watch] All added files already in session, skipping sync";
        return;
    }

    qInfo() << "[Watch] Processing additions...";
    QString message = QString(tr("Detected %1 new sprite(s). Syncing...")).arg(paths.size());
    showSyncNotification(message);
    performManualSync();
}

void MainWindow::onFolderWatcherFilesRemoved(const QStringList& paths) {
    qInfo() << "[Watch] Files removed detected:" << paths.count() << "files";
    qInfo() << "[Watch] Current sync mode:" << (int)m_settings.syncMode;

    if (m_settings.syncMode != SyncMode::Watch) {
        qWarning() << "[Watch] Not in Watch mode, ignoring";
        return;
    }

    // Check if these are files we already removed via GUI
    bool hasRemainingFiles = false;
    for (const QString& path : paths) {
        if (isUnderSpratTrash(m_session->sourceFolder, path)) {
            qInfo() << "[Watch] Ignoring removal inside .sprat-trash:" << path;
            continue;
        }
        if (m_session->activeFramePaths.contains(path)) {
            hasRemainingFiles = true;
            break;
        }
    }

    if (!hasRemainingFiles) {
        qInfo() << "[Watch] All removed files already gone from session, skipping sync";
        return;
    }

    qInfo() << "[Watch] Processing removals...";
    QString message = QString(tr("%1 sprite(s) removed from source folder.")).arg(paths.size());
    showSyncNotification(message);
    performManualSync();
}

void MainWindow::onFolderWatcherFilesModified(const QStringList& paths) {
    qInfo() << "[Watch] Files modified detected:" << paths.count() << "files";
    qInfo() << "[Watch] Current sync mode:" << (int)m_settings.syncMode;

    if (m_settings.syncMode != SyncMode::Watch) {
        qWarning() << "[Watch] Not in Watch mode, ignoring";
        return;
    }

    // Only rebuild if the modified files are actually part of our layout
    bool inModel = false;
    for (const QString& path : paths) {
        if (isUnderSpratTrash(m_session->sourceFolder, path)) {
            qInfo() << "[Watch] Ignoring modification inside .sprat-trash:" << path;
            continue;
        }
        if (m_session->activeFramePaths.contains(path)) {
            inModel = true;
            break;
        }
    }

    if (!inModel) {
        qInfo() << "[Watch] Modified files not in session, skipping rebuild";
        return;
    }

    qInfo() << "[Watch] Processing modifications...";
    // Watch mode detected file changes - rebuild immediately
    scheduleLayoutRebuild(true);
}

void MainWindow::onSyncNowRequested() {
    qInfo() << "[SyncNow] Button clicked";
    qInfo() << "[SyncNow] m_session->sourceFolder:" << m_session->sourceFolder;
    qInfo() << "[SyncNow] m_session->sourceFolderIsTemp:" << (m_projectController ? m_projectController->isSourceFolderTemp() : false);

    if (m_session->sourceFolder.isEmpty()) {
        qWarning() << "[SyncNow] Source folder is empty!";
        MessageDialog::information(this, tr("Sync"), tr("No source folder configured."));
        return;
    }

    qInfo() << "[SyncNow] Calling performManualSync()";
    performManualSync();
}

void MainWindow::performFolderSync() {
    if (m_session->activeAtlas().layoutModels.isEmpty()) {
        qWarning() << "Cannot sync: no layout models loaded";
        return;
    }

    // Detect changes — use source-aware detection when sources are configured
    FolderSyncService::SyncResult syncResult;
    if (!m_session->sources.isEmpty()) {
        syncResult = FolderSyncService::detectChangesFromSources(
            m_session->sources,
            m_session->activeAtlas().layoutModels.first().sprites);
    } else {
        syncResult = FolderSyncService::detectChanges(
            m_session->sourceFolder,
            m_session->activeAtlas().layoutModels.first().sprites);
    }

    if (!syncResult.error.isEmpty()) {
        MessageDialog::warning(this, tr("Sync Error"), syncResult.error);
        return;
    }

    if (!syncResult.hasChanges()) {
        showSyncNotification(tr("No changes detected"));
        return;
    }

    // Show summary
    QString summary = FolderSyncService::describeSyncResult(syncResult);
    qInfo() << "Folder sync:" << summary;

    // Merge changes into layout
    if (!FolderSyncService::mergeSyncResults(
            m_session->activeAtlas().layoutModels.first(), syncResult, m_session->sourceFolder)) {
        MessageDialog::warning(this, tr("Sync Error"), tr("Failed to merge changes."));
        return;
    }
    ensureUniqueSpriteNames(m_session->activeAtlas().layoutModels, m_session->sourceFolder);

    // Update activeFramePaths to match the modified layout
    populateActiveFrameListFromModel();

    // Always regenerate frame list file after changes to keep it in sync
    ensureFrameListInput();

    showSyncNotification(summary);

    // Re-run layout to position new sprites or remove deleted ones
    if (!syncResult.newImagePaths.isEmpty() || !syncResult.deletedImagePaths.isEmpty()) {
        if (!m_session->activeFramePaths.isEmpty()) {
            // IMPORTANT: Refresh GUI immediately to show deleted sprites removed
            // This prevents stale UI while layout rebuilds in background
            refreshSpriteTree();
            {
                auto* syncCanvas = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
                if (syncCanvas) syncCanvas->update();
            }

            // Show feedback about what happened
            if (!syncResult.newImagePaths.isEmpty() && !syncResult.deletedImagePaths.isEmpty()) {
                m_statusLabel->setText(tr("Sprites synced: %1 added, %2 removed. Rebuilding layout...")
                    .arg(syncResult.newImagePaths.size()).arg(syncResult.deletedImagePaths.size()));
            } else if (!syncResult.newImagePaths.isEmpty()) {
                m_statusLabel->setText(tr("Sprites synced: %1 added. Rebuilding layout...")
                    .arg(syncResult.newImagePaths.size()));
            } else {
                m_statusLabel->setText(tr("Sprites synced: %1 removed. Rebuilding layout...")
                    .arg(syncResult.deletedImagePaths.size()));
            }

            // Build layout in background (debounced to avoid blocking UI)
            scheduleLayoutRebuild(false);

            // Clear the status message after 4 seconds to show it completed
            QTimer::singleShot(4000, this, [this]() {
                if (m_statusLabel) {
                    m_statusLabel->clear();
                }
            });
        } else if (!syncResult.deletedImagePaths.isEmpty()) {
            // All frames were deleted
            m_statusLabel->setText(tr("All sprites removed."));
            refreshSpriteTree();
            {
                auto* syncCanvas = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
                if (syncCanvas) syncCanvas->update();
            }
        }
    }
}

void MainWindow::performManualSync() {
    qInfo() << "[Sync] Starting manual sync...";

    if (!m_session) {
        qWarning() << "[Sync] Session is null";
        MessageDialog::warning(this, tr("Sync Error"), tr("No session."));
        return;
    }

    if (m_session->activeAtlas().layoutModels.isEmpty()) {
        qWarning() << "[Sync] No layout models loaded";
        MessageDialog::information(this, tr("Sync"), tr("No layout loaded."));
        return;
    }

    if (m_session->sourceFolder.isEmpty()) {
        qWarning() << "[Sync] Source folder is empty";
        MessageDialog::information(this, tr("Sync"), tr("No sprites folder configured."));
        return;
    }

    QDir folderDir(m_session->sourceFolder);
    if (!folderDir.exists()) {
        qWarning() << "[Sync] Folder does not exist:" << m_session->sourceFolder;
        MessageDialog::warning(this, tr("Sync Error"), tr("Sprites folder does not exist."));
        return;
    }

    qInfo() << "[Sync] Using folder:" << m_session->sourceFolder;
    qInfo() << "[Sync] Layout has" << m_session->activeAtlas().layoutModels.first().sprites.size() << "sprites";

    LayoutModel& layout = m_session->activeAtlas().layoutModels.first();
    int removedCount = 0;
    int addedCount = 0;

    // Step 1: Traverse layout sprites - remove missing
    qInfo() << "[Sync] Step 1: Checking layout sprites...";
    for (int i = layout.sprites.size() - 1; i >= 0; --i) {
        const auto& sprite = layout.sprites[i];
        if (!sprite) continue;

        QFileInfo fileInfo(sprite->path);
        qInfo() << "[Sync]   Sprite:" << sprite->name << "Path:" << sprite->path << "Exists:" << fileInfo.exists();

        // If file doesn't exist in folder, remove sprite
        if (!fileInfo.exists()) {
            qInfo() << "[Sync]   Removing sprite" << sprite->name;
            layout.sprites.removeAt(i);
            removedCount++;
        }
    }

    // Step 2: Traverse folder images - add missing ones
    qInfo() << "[Sync] Step 2: Checking folder images...";
    QStringList folderImages = FolderSyncService::getImageFilesInFolder(m_session->sourceFolder);
    qInfo() << "[Sync]   Found" << folderImages.size() << "images in folder";

    // Build a set of existing sprite paths for fast lookup
    QSet<QString> existingPaths;
    for (const auto& sprite : layout.sprites) {
        if (sprite) existingPaths.insert(sprite->path);
    }

    for (const QString& imagePath : folderImages) {
        qInfo() << "[Sync]   Image:" << imagePath;

        // Check if image is already in layout by absolute path
        if (existingPaths.contains(imagePath)) {
            qInfo() << "[Sync]     Found in layout";
            continue;
        }

        // Not found, add new sprite with name derived from relative path
        qInfo() << "[Sync]     Adding to layout";
        auto newSprite = std::make_shared<Sprite>();
        newSprite->path = imagePath;

        // Derive name from relative path within source folder
        QString rel = QDir(m_session->sourceFolder).relativeFilePath(imagePath);
        QFileInfo relInfo(rel);
        newSprite->name = (relInfo.path() == ".")
            ? relInfo.baseName()
            : relInfo.path() + "/" + relInfo.baseName();

        newSprite->rect = QRect(0, 0, 0, 0);
        newSprite->trimmed = false;
        newSprite->rotated = false;
        newSprite->pivotX = 0;
        newSprite->pivotY = 0;

        layout.sprites.append(newSprite);
        addedCount++;
    }

    qInfo() << "[Sync] Step 3: Updating layout data...";
    qInfo() << "[Sync]   Removed:" << removedCount << "Added:" << addedCount;
    ensureUniqueSpriteNames(m_session->activeAtlas().layoutModels, m_session->sourceFolder);

    // Step 3: Update activeFramePaths and frame list
    populateActiveFrameListFromModel();
    qInfo() << "[Sync]   activeFramePaths now has" << m_session->activeFramePaths.size() << "items";

    ensureFrameListInput();
    qInfo() << "[Sync]   Frame list updated, layoutSourcePath:" << m_session->layoutSourcePath;

    // Show summary
    QString summary = QString(tr("Sync complete: %1 removed, %2 added"))
        .arg(removedCount).arg(addedCount);
    showSyncNotification(summary);

    // Step 4: Refresh layout — only rebuild when the sprite set actually changed.
    // Rebuilding unconditionally (even on zero-change calls) caused the periodic
    // Watch-mode check to trigger continuous sprite animation when nothing changed.
    qInfo() << "[Sync] Step 4: Refreshing layout...";
    if (!m_session->activeFramePaths.isEmpty()) {
        if (removedCount > 0 || addedCount > 0) {
            qInfo() << "[Sync]   Running layout...";
            m_statusLabel->setText(tr("Regenerating layout..."));
            // Folder sync completed - rebuild immediately
            scheduleLayoutRebuild(true);
        } else {
            qInfo() << "[Sync]   No changes - skipping layout rebuild";
        }
    } else {
        qWarning() << "[Sync]   No sprites left!";
        m_statusLabel->setText(tr("No sprites to display."));
    }

    qInfo() << "[Sync] Manual sync complete";
}

void MainWindow::onWatchModePeriodicCheck() {
    // Periodic check in Watch mode to detect file removals
    // This handles cases where directoryChanged signal is not reliably emitted

    // Stop timer if not in Watch mode
    if (m_settings.syncMode != SyncMode::Watch) {
        if (m_watchModePeriodicCheckTimer) {
            m_watchModePeriodicCheckTimer->stop();
        }
        return;
    }

    // Validate session and basic state
    if (!m_session || m_session->activeAtlas().layoutModels.isEmpty() || m_session->sourceFolder.isEmpty()) {
        return;
    }

    // Verify sprites folder still exists
    if (!QDir(m_session->sourceFolder).exists()) {
        qWarning() << "[Watch] Sprites folder no longer exists:" << m_session->sourceFolder;
        m_session->sourceFolder.clear();
        cleanupSourceFolderWatcher();
        return;
    }

    // Check for any changes: missing sprites or new files in the folder
    const LayoutModel& layout = m_session->activeAtlas().layoutModels.first();
    bool needsSync = false;

    for (const auto& sprite : layout.sprites) {
        if (sprite && !QFileInfo::exists(sprite->path)) {
            qInfo() << "[Watch] Detected missing sprite:" << sprite->name;
            needsSync = true;
            break;
        }
    }

    if (!needsSync) {
        QStringList folderImages = FolderSyncService::getImageFilesInFolder(m_session->sourceFolder);
        QSet<QString> existingPaths;
        for (const auto& sprite : layout.sprites) {
            if (sprite) existingPaths.insert(sprite->path);
        }
        for (const QString& path : folderImages) {
            if (!existingPaths.contains(path)) {
                qInfo() << "[Watch] Detected new file in folder:" << path;
                needsSync = true;
                break;
            }
        }
    }

    if (needsSync) {
        qInfo() << "[Watch] Periodic check triggering sync";
        performManualSync();
    }
}

void MainWindow::initializeSourceFolderWatcher() {
    qInfo() << "[Watcher] Initializing source folder watcher, mode:" << (int)m_settings.syncMode;
    ensureSourceFolder();

    if (!m_session) {
        qWarning() << "[Watcher] Session is null";
        return;
    }

    if (m_session->sourceFolder.isEmpty()) {
        qWarning() << "[Watcher] Source folder is empty";
        return;
    }

    if (!m_folderWatcher) {
        qWarning() << "[Watcher] Folder watcher is null";
        return;
    }

    qInfo() << "[Watcher] Source folder:" << m_session->sourceFolder;

    if (m_settings.syncMode == SyncMode::Watch) {
        // Build the list of all smart folder paths (fall back to sourceFolder for legacy sessions)
        QStringList pathsToWatch;
        for (const auto& sf : m_session->smartFolders) {
            if (!sf.path.isEmpty()) pathsToWatch.append(sf.path);
        }
        if (pathsToWatch.isEmpty() && !m_session->sourceFolder.isEmpty()) {
            pathsToWatch.append(m_session->sourceFolder);
        }

        // Only restart if the watched paths changed
        const QString currentWatched = m_folderWatcher->watchedPath();
        const bool alreadyWatchingCorrect = (pathsToWatch.size() == 1 && currentWatched == pathsToWatch.first())
                                         || (pathsToWatch.isEmpty() && !m_folderWatcher->isWatching());
        if (!alreadyWatchingCorrect) {
            if (pathsToWatch.size() == 1) {
                m_folderWatcher->watchFolder(pathsToWatch.first());
            } else {
                m_folderWatcher->watchFolders(pathsToWatch);
            }
        }
    } else {
        if (m_folderWatcher->isWatching()) {
            m_folderWatcher->stopWatching();
        }
        qInfo() << "[Watcher] Manual mode - folder ready:" << m_session->sourceFolder;
    }

    updateOpenSourceFolderAction();
}

void MainWindow::cleanupSourceFolderWatcher() {
    if (!m_folderWatcher) {
        return;
    }
    if (m_folderWatcher->isWatching()) {
        m_folderWatcher->stopWatching();
        qInfo() << "Source folder watcher stopped";
    }

    // Stop periodic check timer
    if (m_watchModePeriodicCheckTimer) {
        m_watchModePeriodicCheckTimer->stop();
    }

    updateOpenSourceFolderAction();
}

void MainWindow::ensureSourceFolder() {
    if (!m_session->sourceFolder.isEmpty()) return;

    auto tempDir = std::make_unique<QTemporaryDir>();
    if (tempDir->isValid()) {
        m_session->sourceFolder = tempDir->path();
        if (m_projectController) m_projectController->setSourceFolderIsTemp(true);
        // Store temp dir in session separately - it must persist while sprites
        // reference files in it. Not added to tempDirs list which is cleared
        // during layout operations.
        if (m_projectController) m_projectController->setSourceFolderTempDir(std::move(tempDir));
    }
}

void MainWindow::onOpenSourceFolderClicked() {
#ifdef Q_OS_WASM
    const QString folder = m_session ? m_session->currentFolder : QString();
    if (folder.isEmpty()) return;
    auto* dlg = new WasmFolderBrowserDialog(folder, this);

    // Accumulate all deletions made during the dialog session; process them
    // after exec() returns so the layout rebuild runs in the parent event loop,
    // not inside the modal's nested event loop (avoids a WASM UI freeze).
    QStringList accumulatedDeleted;
    QStringList accumulatedAdded;
    connect(dlg, &WasmFolderBrowserDialog::filesDeleted,
            dlg, [&accumulatedDeleted](const QStringList& paths) {
        accumulatedDeleted << paths;
    });
    connect(dlg, &WasmFolderBrowserDialog::filesAdded,
            dlg, [&accumulatedAdded](const QStringList& paths) {
        accumulatedAdded << paths;
    });

    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->exec();

    if (!accumulatedDeleted.isEmpty()) {
        onSpritesDeleted(accumulatedDeleted);
    }
    if (!accumulatedAdded.isEmpty()) {
        performManualSync();
    }
#else
    const QString folder = m_session ? m_session->sourceFolder : QString();
    if (folder.isEmpty()) return;
    QDir().mkpath(folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
#endif
}

void MainWindow::updateOpenSourceFolderAction() {
    if (!m_openSourceFolderAction) return;
#ifdef Q_OS_WASM
    const bool enabled = m_session && !m_session->activeFramePaths.isEmpty();
#else
    const bool enabled = m_session
        && m_settings.syncMode != SyncMode::None
        && !m_session->sourceFolder.isEmpty();
#endif
    m_openSourceFolderAction->setEnabled(enabled);
}

#ifdef Q_OS_WASM
void MainWindow::onSpritesDeleted(const QStringList& paths)
{
    if (!m_session) return;

    // Filter to paths that are actually loaded
    QStringList targets;
    for (const QString& path : paths) {
        if (m_session->activeFramePaths.contains(path)) {
            targets.append(path);
        }
    }
    if (targets.isEmpty()) return;

    // Deselect sprite if it is being removed
    if (m_session->selectedSprite && targets.contains(m_session->selectedSprite->path)) {
        onSpriteSelected(nullptr);
    }
    m_session->selectedSprites.erase(
        std::remove_if(m_session->selectedSprites.begin(), m_session->selectedSprites.end(),
            [&targets](const SpritePtr& s) { return s && targets.contains(s->path); }),
        m_session->selectedSprites.end());

    // Remove from activeFramePaths
    for (const QString& path : targets) {
        m_session->activeFramePaths.removeAll(path);
    }

    // Remove from timelines, drop empty timelines
    const QSet<QString> targetSet(targets.begin(), targets.end());
    for (auto& timeline : m_session->activeAtlas().timelines) {
        for (int i = timeline.frames.size() - 1; i >= 0; --i) {
            if (targetSet.contains(timeline.frames[i])) {
                timeline.frames.removeAt(i);
            }
        }
    }
    for (int i = m_session->activeAtlas().timelines.size() - 1; i >= 0; --i) {
        if (m_session->activeAtlas().timelines[i].frames.isEmpty()) {
            m_session->activeAtlas().timelines.removeAt(i);
            if (m_session->selectedTimelineIndex > i) {
                --m_session->selectedTimelineIndex;
            } else if (m_session->selectedTimelineIndex == i) {
                m_session->selectedTimelineIndex = -1;
            }
        }
    }

    if (m_session->activeFramePaths.isEmpty()) {
        m_session->layoutSourcePath.clear();
        m_session->layoutSourceIsList = false;
        m_session->removeFrameListFile();
        m_session->currentFolder.clear();
        m_session->activeAtlas().layoutModels.clear();
        {
            auto* wasmCanvas = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
            if (wasmCanvas) wasmCanvas->clearCanvas();
        }
        m_session->selectedSprites.clear();
        m_session->selectedSprite.reset();
        m_statusLabel->setText(tr("No frames loaded"));
        m_folderLabel->setText(tr("Folder: none"));
        m_session->cachedLayoutOutput.clear();
        m_session->cachedLayoutScale = 1.0;
        updateMainContentView();
        updateUiState();
        updateOpenSourceFolderAction();
        refreshSpriteTree();
        refreshTimelineList();
        refreshTimelineFrames();
        refreshAnimationTest();
    } else {
        ensureFrameListInput();
        captureOldSpritePositions();
        auto* wasmCanvas = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
        if (wasmCanvas) wasmCanvas->removeSprites(targets);
        m_statusLabel->setText(QString(tr("Removed %1 file(s)")).arg(targets.size()));
        refreshTimelineFrames();
        refreshTimelineList();
        refreshAnimationTest();
        scheduleLayoutRebuild(false, true);
    }
}
#endif

bool MainWindow::activeFramesAreInSourceFolder() const {
    if (m_session->sourceFolder.isEmpty() || m_session->activeFramePaths.isEmpty()) {
        return false;
    }
    
    QElapsedTimer timer;
    timer.start();

    QDir sourceDir(m_session->sourceFolder);
    // Use an absolute path for faster prefix matching
    const QString absoluteSourcePath = sourceDir.absolutePath() + "/";

    for (const QString& path : m_session->activeFramePaths) {
        // Optimization: avoid QFileInfo if path already starts with the source folder
        if (path.startsWith(absoluteSourcePath)) {
            continue;
        }
        
        // If not, it might be a relative path or an absolute path outside.
        // We still need QFileInfo for canonical/absolute resolution if it's not a direct prefix match.
        const QString absPath = QFileInfo(path).absoluteFilePath();
        if (!absPath.startsWith(absoluteSourcePath)) {
            qInfo() << "[Performance] activeFramesAreInSourceFolder check failed in" << timer.elapsed() << "ms";
            return false;
        }
    }
    
    qInfo() << "[Performance] activeFramesAreInSourceFolder checked" << m_session->activeFramePaths.size() << "files in" << timer.elapsed() << "ms";
    return true;
}

bool MainWindow::sourceFolderMatchesActiveFrames() const {
    if (!m_session || m_session->sourceFolder.isEmpty() || m_session->activeFramePaths.isEmpty()) {
        return false;
    }
    if (!activeFramesAreInSourceFolder()) {
        return false;
    }
    if (QDir(QDir(m_session->sourceFolder).filePath(".sprat-trash")).exists()) {
        return false;
    }

    const QStringList folderImages = FolderSyncService::getImageFilesInFolder(m_session->sourceFolder);
    if (folderImages.size() != m_session->activeFramePaths.size()) {
        return false;
    }

    const QSet<QString> folderSet(folderImages.begin(), folderImages.end());
    const QSet<QString> activeSet(m_session->activeFramePaths.begin(), m_session->activeFramePaths.end());
    return folderSet == activeSet;
}

bool MainWindow::shouldDeleteRemovedSpritesFromSource() const {
    return m_session
        && m_settings.syncMode != SyncMode::None
        && !m_session->sourceFolder.isEmpty();
}

void MainWindow::copyActiveFramesToSourceFolder(bool overwriteDuplicates) {
    if (m_session->activeFramePaths.isEmpty() || m_session->sourceFolder.isEmpty()) {
        return;
    }
    
    QElapsedTimer timer;
    timer.start();

    QDir sourceDir(m_session->sourceFolder);
    sourceDir.mkpath(".");
    QString canonicalFolder = sourceDir.absolutePath();
    if (!canonicalFolder.endsWith('/')) canonicalFolder += '/';

    // Determine the common root of active frames for relative path computation
    const QString originalRootPath = m_session->currentFolder;
    QDir originalRootDir(originalRootPath);

    QStringList newPaths;
    newPaths.reserve(m_session->activeFramePaths.size());

    for (const QString& path : m_session->activeFramePaths) {
        // Optimization: if path already starts with the source folder, keep as-is
        if (path.startsWith(canonicalFolder)) {
            newPaths.append(path);
            continue;
        }

        const QFileInfo srcInfo(path);
        const QString absPath = srcInfo.absoluteFilePath();

        // Check again after resolving absolute path
        if (absPath.startsWith(canonicalFolder)) {
            newPaths.append(absPath);
            continue;
        }

        // Compute relative path from original root to preserve subfolder structure
        QString relPath;
        if (!originalRootPath.isEmpty() && absPath.startsWith(originalRootPath)) {
            relPath = originalRootDir.relativeFilePath(absPath);
        } else {
            relPath = srcInfo.fileName();
        }

        QString dst = sourceDir.filePath(relPath);
        QFileInfo dstInfo(dst);

        // Create intermediate subdirectories if needed
        QString dstDir = dstInfo.absolutePath();
        if (!QDir(dstDir).exists()) {
            sourceDir.mkpath(sourceDir.relativeFilePath(dstDir));
        }

        // Handle existing files
        if (dstInfo.exists()) {
            if (overwriteDuplicates) {
                // Replace the existing file
                QFile::remove(dst);
            } else {
                // Resolve name conflicts: try baseName_1, baseName_2, ..., baseName_99.
                const QString baseName = dstInfo.completeBaseName();
                const QString suffix   = dstInfo.suffix();
                bool resolved = false;
                for (int i = 1; i <= 99; ++i) {
                    const QString candidateName = QStringLiteral("%1_%2.%3").arg(baseName).arg(i).arg(suffix);
                    const QString candidate = QDir(dstDir).filePath(candidateName);
                    if (!QFileInfo::exists(candidate)) {
                        dst = candidate;
                        resolved = true;
                        break;
                    }
                }
                if (!resolved) {
                    qWarning() << "copyActiveFramesToSourceFolder: conflict unresolvable for"
                               << relPath << "- skipping";
                    newPaths.append(path);
                    continue;
                }
            }
        }

        if (QFile::copy(absPath, dst)) {
            newPaths.append(dst);
        } else {
            qWarning() << "copyActiveFramesToSourceFolder: copy failed" << absPath << "->" << dst;
            newPaths.append(absPath);
        }
    }
    m_session->activeFramePaths = newPaths;
    qInfo() << "[Performance] copyActiveFramesToSourceFolder took" << timer.elapsed() << "ms for" << newPaths.size() << "files";
}

void MainWindow::showSyncNotification(const QString& message) {
    if (m_statusLabel) {
        m_statusLabel->setText(message);
    }
    qInfo() << "Sync:" << message;
}
