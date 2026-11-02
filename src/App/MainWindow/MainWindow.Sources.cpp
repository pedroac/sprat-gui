/// @file MainWindow.Sources.cpp
/// Source list actions: remove, retry and re-sync sources, and sync the layout back to an image or GIF.

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

void MainWindow::removeSource(int index) {
    if (!m_session || index < 0 || index >= m_session->sources.size()) return;

    // Save full state before any changes (needed for undo).
    const QVector<ProjectSource> savedSources       = m_session->sources;
    const QVector<SmartFolder>   savedSmartFolders  = m_session->smartFolders;
    const QStringList            savedActivePaths   = m_session->activeFramePaths;
    const QVector<AnimationTimeline> savedTimelines = m_session->activeAtlas().timelines;
    const int savedTimelineIdx = m_session->selectedTimelineIndex;
    const QVector<LayoutModel>   savedLayoutModels  = m_session->activeAtlas().layoutModels;

    const ProjectSource removed = m_session->sources.at(index);
    const bool hasSmartFolder   = (index < m_session->smartFolders.size());
    const SmartFolder removedSF = hasSmartFolder
                                  ? m_session->smartFolders.at(index) : SmartFolder{};

    m_session->sources.removeAt(index);
    if (hasSmartFolder)
        m_session->smartFolders.removeAt(index);

    // Identify sprites that belong to the removed source.
    // Normalise paths with QDir::cleanPath so that paths containing
    // redundant "../.." components match the canonical cachedFolderPath.
    QStringList toRemove;
    const QString& srcFolder = m_session->sourceFolder;
    const QString cleanedCache = removed.cachedFolderPath.isEmpty()
        ? QString() : QDir::cleanPath(removed.cachedFolderPath);

    for (const QString& p : m_session->activeFramePaths) {
        bool belongs = false;
        if (!cleanedCache.isEmpty()) {
            const QString cp = QDir::cleanPath(p);
            belongs = cp.startsWith(cleanedCache + QLatin1Char('/'))
                      || cp == cleanedCache;
        } else if (!srcFolder.isEmpty() && !removed.originalPath.isEmpty()) {
            // Folder source loaded via Replace: reverse-map relative path.
            const QString rel = QDir(srcFolder).relativeFilePath(p);
            belongs = QFileInfo::exists(QDir(removed.originalPath).filePath(rel));
        }
        if (belongs)
            toRemove.append(p);
    }

    // Remove sprites from activeFramePaths and layoutModels.
    const QSet<QString> targetSet(toRemove.begin(), toRemove.end());
    for (const QString& p : toRemove)
        m_session->activeFramePaths.removeAll(p);
    for (auto& model : m_session->activeAtlas().layoutModels) {
        model.sprites.erase(
            std::remove_if(model.sprites.begin(), model.sprites.end(),
                [&targetSet](const SpritePtr& s) {
                    return s && targetSet.contains(s->path);
                }),
            model.sprites.end());
    }

    // Remove sprites from timelines; drop timelines that become empty.
    for (auto& tl : m_session->activeAtlas().timelines) {
        for (int i = tl.frames.size() - 1; i >= 0; --i) {
            if (targetSet.contains(tl.frames[i]))
                tl.frames.removeAt(i);
        }
    }
    for (int i = m_session->activeAtlas().timelines.size() - 1; i >= 0; --i) {
        if (m_session->activeAtlas().timelines[i].frames.isEmpty()) {
            m_session->activeAtlas().timelines.removeAt(i);
            if (m_session->selectedTimelineIndex > i)
                --m_session->selectedTimelineIndex;
            else if (m_session->selectedTimelineIndex == i)
                m_session->selectedTimelineIndex = -1;
        }
    }

    // Move the cached folder to a system-temp trash instead of deleting
    // it permanently, so it can be restored on undo.
    QString trashPath;
    if (!removed.cachedFolderPath.isEmpty()
            && removed.cachedFolderPath != srcFolder) {
        trashPath = TrashBin::sendFolder(removed.cachedFolderPath);
        // If it can't be moved, leave the files: deleting them would make undo lose them.
        if (trashPath.isEmpty())
            qWarning() << "Could not move removed source folder to the trash:" << removed.cachedFolderPath;
    }

    // Callbacks for the undo command.
    auto postExecuteRedo = [this]() {
        if (m_session->sources.isEmpty() || m_session->activeFramePaths.isEmpty()) {
            m_session->activeAtlas().layoutModels.clear();
            m_session->activeFramePaths.clear();
            m_session->layoutSourcePath.clear();
            m_session->layoutSourceIsList = false;
            m_session->sourceFolder.clear();
            refreshSpriteTree();
            updateMainContentView();
            updateUiState();
        } else {
            scheduleLayoutRebuild(true);
        }
    };

    auto postExecuteUndo = [this]() {
        scheduleLayoutRebuild(true);
    };

    if (m_undoStack) {
        m_undoStack->push(new RemoveSourceCommand(
            &m_session->sources,
            &m_session->smartFolders,
            &m_session->activeFramePaths,
            &m_session->activeAtlas().timelines,
            &m_session->selectedTimelineIndex,
            &m_session->activeAtlas().layoutModels,
            index,
            removed,
            removedSF,
            hasSmartFolder,
            toRemove,
            trashPath,
            savedSources,
            savedSmartFolders,
            savedActivePaths,
            savedTimelines,
            savedTimelineIdx,
            savedLayoutModels,
            [this]() { return ensureFrameListInput(); },
            postExecuteRedo,
            std::move(postExecuteUndo)
        ));
    }

    postExecuteRedo();
}

void MainWindow::onRetrySourceRequested(int sourceIndex) {
    if (!m_session || sourceIndex < 0 || sourceIndex >= m_session->sources.size()) return;
    const ProjectSource src = m_session->sources.at(sourceIndex);
    if (!src.hasError) return;

    // Remove the failed source entry (no undo — there's nothing to undo)
    m_session->sources.removeAt(sourceIndex);
    m_session->rebuildSpriteIndex();
    refreshSpriteTree();

    // Re-initiate loading from the original path
    if (src.type == SourceType::Url) {
        tryHandleRemoteUrl(QUrl(src.originalPath), DropAction::Merge);
    } else {
        tryHandleDroppedPath(src.originalPath, DropAction::Merge);
    }
}

void MainWindow::onRemoveSourceRequested(int sourceIndex) {
    if (!m_session || sourceIndex < 0 || sourceIndex >= m_session->sources.size()) return;

    const QString sourceName = m_session->sources.at(sourceIndex).name;
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Remove Source"));
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setText(tr("Remove \"%1\" from this project?").arg(sourceName));
    msgBox.setInformativeText(
        tr("All sprites from this source will be removed from the atlas."));
    auto* removeBtn = msgBox.addButton(tr("Remove Source"), QMessageBox::DestructiveRole);
    msgBox.addButton(QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Cancel);
    msgBox.exec();

    if (msgBox.clickedButton() == removeBtn)
        removeSource(sourceIndex);
}

void MainWindow::onSyncSourceRequested(int sourceIndex) {
    if (!m_session || sourceIndex < 0 || sourceIndex >= m_session->sources.size()) return;
    const ProjectSource& src = m_session->sources.at(sourceIndex);

    // --- Check if the source can be located ---
    const bool located = [&]() -> bool {
        switch (src.type) {
        case SourceType::Folder:      return QFileInfo(src.originalPath).isDir();
        case SourceType::Archive:
        case SourceType::SingleImage: return QFileInfo::exists(src.originalPath);
        case SourceType::Url:         return true;
        }
        return false;
    }();

    if (located) {
        // --- Detect individual files missing from the source ---
        QStringList haveLocally;  // File is in our cache but absent from the source archive
        QStringList missingFully; // File is referenced in the layout but gone from disk entirely

        if (src.type == SourceType::Archive && !src.cachedFolderPath.isEmpty()) {
            // Files in the local cache that were removed from the zip
            QString listErr;
            const QStringList zipEntries = ArchiveExtractor::listEntries(src.originalPath, listErr);
            const QSet<QString> zipSet(zipEntries.begin(), zipEntries.end());
            QDirIterator cit(src.cachedFolderPath, QDir::Files, QDirIterator::Subdirectories);
            while (cit.hasNext()) {
                cit.next();
                const QString rel = QDir(src.cachedFolderPath).relativeFilePath(cit.filePath());
                if (!zipSet.contains(rel))
                    haveLocally.append(cit.filePath());
            }
        } else if (src.type == SourceType::Folder) {
            // Layout sprites from this folder whose files no longer exist
            for (const auto& model : m_session->activeAtlas().layoutModels) {
                for (const auto& sprite : model.sprites) {
                    if (!sprite || !sprite->path.startsWith(src.originalPath)) continue;
                    if (!QFileInfo::exists(sprite->path)
                            && !missingFully.contains(sprite->path))
                        missingFully.append(sprite->path);
                }
            }
        }

        if (!haveLocally.isEmpty()) {
            QMessageBox msgBox(this);
            msgBox.setWindowTitle(tr("Files Missing in Source"));
            msgBox.setIcon(QMessageBox::Warning);
            msgBox.setText(tr("%n file(s) are present in the project but missing from the source.",
                              "", haveLocally.size()));
            msgBox.setDetailedText(haveLocally.join(QLatin1Char('\n')));
            auto* addBtn = msgBox.addButton(tr("Add to Source"), QMessageBox::AcceptRole);
            msgBox.addButton(tr("Skip"), QMessageBox::RejectRole);
            msgBox.exec();

            if (msgBox.clickedButton() == addBtn) {
                QString error;
                if (!ArchiveExtractor::createZip(src.cachedFolderPath, src.originalPath, error))
                    MessageDialog::warning(this, tr("Failed to Update Source"), error);
            }
        }

        if (!missingFully.isEmpty()) {
            QMessageBox msgBox(this);
            msgBox.setWindowTitle(tr("Files Missing in Source"));
            msgBox.setIcon(QMessageBox::Warning);
            msgBox.setText(tr("%n file(s) are missing from the source and the project.",
                              "", missingFully.size()));
            msgBox.setDetailedText(missingFully.join(QLatin1Char('\n')));
            auto* locateBtn = msgBox.addButton(tr("Locate in Folder…"), QMessageBox::AcceptRole);
            msgBox.addButton(tr("Skip"), QMessageBox::RejectRole);
            msgBox.exec();

            if (msgBox.clickedButton() == locateBtn) {
                const QString dir = QFileDialog::getExistingDirectory(
                    this, tr("Select Folder Containing Missing Files"),
                    src.originalPath);
                if (!dir.isEmpty()) {
                    bool anyFound = false;
                    for (const QString& missing : missingFully) {
                        const QString fileName = QFileInfo(missing).fileName();
                        const QString candidate = QDir(dir).filePath(fileName);
                        if (QFileInfo::exists(candidate)) {
                            // Copy to source and update layout path to the new location
                            const QString dest = QDir(src.originalPath).filePath(fileName);
                            if (candidate != dest)
                                QFile::copy(candidate, dest);
                            for (QString& p : m_session->activeFramePaths)
                                if (p == missing) p = dest;
                            anyFound = true;
                        }
                    }
                    if (!anyFound)
                        MessageDialog::warning(this, tr("Files Not Found"),
                            tr("None of the missing files were found in the selected folder."));
                }
            }
        }

        onRunLayout(true);
        return;
    }

    // --- Determine what we still have ---

    // Case 1: cached/embedded copy exists
    const bool hasCachedFiles = !src.cachedFolderPath.isEmpty()
                                 && QFileInfo(src.cachedFolderPath).isDir()
                                 && [&]() {
                                     QDirIterator it(src.cachedFolderPath, QDir::Files,
                                                     QDirIterator::Subdirectories);
                                     return it.hasNext();
                                 }();

    // Case 2: sprites that still exist on disk are loaded in the layout
    QStringList liveSpritePaths;
    if (!hasCachedFiles) {
        for (const auto& model : m_session->activeAtlas().layoutModels) {
            for (const auto& sprite : model.sprites) {
                if (!sprite) continue;
                const bool fromCache  = !src.cachedFolderPath.isEmpty()
                                        && sprite->path.startsWith(src.cachedFolderPath);
                const bool fromSource = !src.originalPath.isEmpty()
                                        && sprite->path.startsWith(src.originalPath);
                if ((fromCache || fromSource) && QFileInfo::exists(sprite->path)
                        && !liveSpritePaths.contains(sprite->path))
                    liveSpritePaths.append(sprite->path);
            }
        }
    }

    const QString notFoundMsg =
        tr("The source \"%1\" could not be located:\n%2").arg(src.name, src.originalPath);

    // --- Case 1: cached files present — offer to recreate source ---
    if (hasCachedFiles) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Source Not Found"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setText(notFoundMsg);
        msgBox.setInformativeText(
            tr("The files are cached in the project. "
               "Do you want to recreate the source from the cached files?"));
        auto* recreateBtn = msgBox.addButton(tr("Recreate Source"), QMessageBox::AcceptRole);
        msgBox.addButton(QMessageBox::Cancel);
        msgBox.setDefaultButton(recreateBtn);
        msgBox.exec();
        if (msgBox.clickedButton() != recreateBtn) return;

        QDir().mkpath(QFileInfo(src.originalPath).absolutePath());
        QString error;
        bool ok = false;
        switch (src.type) {
        case SourceType::Archive:
            ok = ArchiveExtractor::createZip(src.cachedFolderPath, src.originalPath, error);
            break;
        case SourceType::Folder: {
            ok = QDir().mkpath(src.originalPath);
            if (ok) {
                QDirIterator it(src.cachedFolderPath, QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    it.next();
                    const QString rel  = QDir(src.cachedFolderPath).relativeFilePath(it.filePath());
                    const QString dest = QDir(src.originalPath).filePath(rel);
                    QDir().mkpath(QFileInfo(dest).absolutePath());
                    QFile::copy(it.filePath(), dest);
                }
            }
            break;
        }
        case SourceType::SingleImage:
            if (src.originalPath.endsWith(".gif", Qt::CaseInsensitive)) {
                syncLayoutToGif(src, [this, src](bool gifOk, const QString& gifError) {
                    if (gifOk) {
                        m_statusLabel->setText(tr("Source recreated: %1").arg(
                            QFileInfo(src.originalPath).fileName()));
                        onRunLayout(true);
                    } else {
                        MessageDialog::warning(this, tr("Recreate Source Failed"),
                            gifError.isEmpty() ? tr("Could not recreate the source.") : gifError);
                    }
                });
                return; // result delivered asynchronously via onGifSyncFinished
            }
            ok = syncLayoutToImage(src, error);
            break;
        default:
            break;
        }

        if (ok) {
            m_statusLabel->setText(
                tr("Source recreated: %1").arg(QFileInfo(src.originalPath).fileName()));
            onRunLayout(true);
        } else {
            MessageDialog::warning(this, tr("Recreate Source Failed"),
                error.isEmpty() ? tr("Could not recreate the source.") : error);
        }
        return;
    }

    // --- Case 2: live sprites in layout — offer to embed ---
    if (!liveSpritePaths.isEmpty()) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Source Not Found"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setText(notFoundMsg);
        msgBox.setInformativeText(
            tr("The layout still has %n sprite(s) from this source. "
               "Do you want to embed them in the project?", "", liveSpritePaths.size()));
        auto* embedBtn = msgBox.addButton(tr("Embed Sprites"), QMessageBox::AcceptRole);
        msgBox.addButton(QMessageBox::Cancel);
        msgBox.setDefaultButton(embedBtn);
        msgBox.exec();
        if (msgBox.clickedButton() != embedBtn) return;

        ensureSourceFolder();

        QString safeName;
        for (const QChar& c : src.name)
            safeName += (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_'))
                        ? c : QLatin1Char('_');
        if (safeName.isEmpty()) safeName = QStringLiteral("embedded");

        QString destFolder = QDir(m_session->sourceFolder).filePath(safeName);
        { int n = 1;
          while (QFileInfo::exists(destFolder))
              destFolder = QDir(m_session->sourceFolder).filePath(
                  safeName + QLatin1Char('_') + QString::number(++n)); }

        if (!QDir().mkpath(destFolder)) {
            MessageDialog::warning(this, tr("Embed Failed"),
                                   tr("Could not create destination folder."));
            return;
        }

        QHash<QString, QString> pathMap;
        for (const QString& from : liveSpritePaths) {
            const QString to = QDir(destFolder).filePath(QFileInfo(from).fileName());
            if (QFile::copy(from, to))
                pathMap[from] = to;
        }
        for (QString& p : m_session->activeFramePaths)
            if (pathMap.contains(p)) p = pathMap.value(p);

        m_session->sources[sourceIndex].cachedFolderPath = destFolder;
        scheduleLayoutRebuild(true);
        m_statusLabel->setText(
            tr("Sprites embedded: %1").arg(QFileInfo(destFolder).fileName()));
        return;
    }

    // --- Case 3: nothing usable — offer to pick, remove, or cancel ---
    {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Source Not Found"));
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setText(notFoundMsg);
        msgBox.setInformativeText(
            tr("The source could not be located and no sprites are loaded from it."));
        auto* pickBtn   = msgBox.addButton(tr("Pick from Filesystem…"), QMessageBox::ActionRole);
        auto* removeBtn = msgBox.addButton(tr("Remove Source"),          QMessageBox::DestructiveRole);
        msgBox.addButton(QMessageBox::Cancel);
        msgBox.exec();

        if (msgBox.clickedButton() == pickBtn) {
            const QString startDir = QFileInfo(src.originalPath).absolutePath();
            QString path;
            switch (src.type) {
            case SourceType::Folder:
                path = QFileDialog::getExistingDirectory(
                    this, tr("Select Replacement Folder"), startDir);
                break;
            case SourceType::Archive:
                path = QFileDialog::getOpenFileName(
                    this, tr("Select Replacement Archive"), startDir,
                    tr("Archives (*.zip *.tar *.tar.gz *.tar.bz2 *.tar.xz)"));
                break;
            case SourceType::SingleImage:
                path = QFileDialog::getOpenFileName(
                    this, tr("Select Replacement Image"), startDir,
                    tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tga)"));
                break;
            default:
                break;
            }
            if (!path.isEmpty()) {
                m_session->sources[sourceIndex].originalPath = path;
                m_session->sources[sourceIndex].cachedFolderPath.clear();
                onRunLayout(true);
            }
        } else if (msgBox.clickedButton() == removeBtn) {
            removeSource(sourceIndex);
        }
    }
}

void MainWindow::onSyncLayoutRequested(int sourceIndex) {
    if (!m_session || sourceIndex < 0 || sourceIndex >= m_session->sources.size()) return;
    const ProjectSource& src = m_session->sources.at(sourceIndex);
    if (src.type == SourceType::Url) return;

    // --- Compute diff summary for the confirmation dialog ---
    QString diffSummary;

    switch (src.type) {
    case SourceType::Folder: {
        int present = 0, missing = 0;
        for (const auto& model : m_session->activeAtlas().layoutModels) {
            for (const auto& sprite : model.sprites) {
                if (!sprite || !sprite->path.startsWith(src.originalPath)) continue;
                if (QFileInfo::exists(sprite->path)) ++present;
                else ++missing;
            }
        }
        QStringList parts;
        if (present > 0) parts << tr("%n file(s) present", "", present);
        if (missing > 0) parts << tr("%n file(s) missing", "", missing);
        diffSummary = parts.isEmpty()
            ? tr("No sprites found for this source.")
            : parts.join(QStringLiteral(", ")) + QLatin1Char('.');
        break;
    }

    case SourceType::Archive: {
        if (!src.cachedFolderPath.isEmpty()) {
            QStringList newFiles;
            {
                QDirIterator it(src.cachedFolderPath, QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    it.next();
                    newFiles.append(QDir(src.cachedFolderPath).relativeFilePath(it.filePath()));
                }
                newFiles.sort();
            }
            QStringList oldFiles;
            if (QFileInfo::exists(src.originalPath)) {
                QString listErr;
                oldFiles = ArchiveExtractor::listEntries(src.originalPath, listErr);
                oldFiles.sort();
            }
            const QSet<QString> oldSet(oldFiles.begin(), oldFiles.end());
            const QSet<QString> newSet(newFiles.begin(), newFiles.end());
            int added = 0, updated = 0, deleted = 0;
            for (const auto& f : newFiles) {
                if (oldSet.contains(f)) ++updated; else ++added;
            }
            for (const auto& f : oldFiles) {
                if (!newSet.contains(f)) ++deleted;
            }
            QStringList parts;
            if (added   > 0) parts << tr("%n file(s) added",   "", added);
            if (updated > 0) parts << tr("%n file(s) updated", "", updated);
            if (deleted > 0) parts << tr("%n file(s) deleted", "", deleted);
            diffSummary = parts.isEmpty()
                ? tr("No changes detected.")
                : parts.join(QStringLiteral(", ")) + QLatin1Char('.');
        }
        break;
    }

    case SourceType::SingleImage: {
        const bool isGif = src.originalPath.endsWith(".gif", Qt::CaseInsensitive)
                           && !src.cachedFolderPath.isEmpty();
        const bool fileExists = QFileInfo::exists(src.originalPath);
        if (isGif) {
            int frameCount = 0;
            QDirIterator it(src.cachedFolderPath, QDir::Files);
            while (it.hasNext()) { it.next(); ++frameCount; }
            diffSummary = fileExists
                ? tr("%n frame(s): 1 file updated.", "", frameCount)
                : tr("%n frame(s): 1 file added.", "", frameCount);
        } else {
            diffSummary = fileExists ? tr("1 file updated.") : tr("1 file added.");
        }
        break;
    }

    default:
        break;
    }

    // --- Confirmation dialog ---
    QString dialogText;
    if (src.type == SourceType::Folder) {
        dialogText = tr("Verify sprite files in \"%1\".\n\n%2\n\nContinue?")
                     .arg(src.originalPath, diffSummary);
    } else {
        dialogText = tr("This will overwrite \"%1\" with the current layout content.\n\n"
                        "%2\n\nThis action cannot be undone. Continue?")
                     .arg(src.originalPath, diffSummary);
    }

    const QMessageBox::StandardButton answer = MessageDialog::confirmWarning(
        this, tr("Sync Layout to Source"),
        dialogText,
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;

    // --- Perform the sync ---
    QString error;
    bool ok = false;

    switch (src.type) {
    case SourceType::Folder:
        ok = true;
        for (const auto& model : m_session->activeAtlas().layoutModels) {
            for (const auto& sprite : model.sprites) {
                if (!sprite || !sprite->path.startsWith(src.originalPath)) continue;
                if (!QFileInfo::exists(sprite->path)) {
                    error = tr("File not found: %1").arg(sprite->path);
                    ok = false;
                }
            }
        }
        break;

    case SourceType::Archive:
        if (src.cachedFolderPath.isEmpty()) {
            error = tr("No cached folder for this archive source.");
        } else {
            ok = ArchiveExtractor::createZip(src.cachedFolderPath, src.originalPath, error);
        }
        break;

    case SourceType::SingleImage:
        if (src.originalPath.endsWith(".gif", Qt::CaseInsensitive)
                && !src.cachedFolderPath.isEmpty()) {
            syncLayoutToGif(src, [this, src](bool gifOk, const QString& gifError) {
                if (gifOk) {
                    m_statusLabel->setText(tr("Sync Layout: saved to '%1'").arg(
                        QFileInfo(src.originalPath).fileName()));
                } else {
                    MessageDialog::warning(this, tr("Sync Layout Failed"),
                        gifError.isEmpty() ? tr("Unknown error.") : gifError);
                }
            });
            return; // result delivered asynchronously via onGifSyncFinished
        }
        ok = syncLayoutToImage(src, error);
        break;

    default:
        break;
    }

    if (ok) {
        m_statusLabel->setText(tr("Sync Layout: saved to '%1'").arg(
            QFileInfo(src.originalPath).fileName()));
    } else {
        MessageDialog::warning(this, tr("Sync Layout Failed"),
            error.isEmpty() ? tr("Unknown error.") : error);
    }
}

bool MainWindow::syncLayoutToImage(const ProjectSource& src, QString& error) {
    if (m_session->activeAtlas().layoutModels.isEmpty()) {
        error = tr("No layout to export.");
        return false;
    }
    const LayoutModel& model = m_session->activeAtlas().layoutModels.first();
    if (model.atlasWidth <= 0 || model.atlasHeight <= 0) {
        error = tr("Invalid atlas dimensions.");
        return false;
    }
    QImage atlas(model.atlasWidth, model.atlasHeight, QImage::Format_ARGB32);
    atlas.fill(Qt::transparent);
    QPainter painter(&atlas);
    for (const auto& sprite : model.sprites) {
        if (!sprite) continue;
        QPixmap pix(sprite->path);
        if (!pix.isNull())
            painter.drawPixmap(sprite->rect.topLeft(), pix);
    }
    painter.end();
    if (!atlas.save(src.originalPath)) {
        error = tr("Could not write to '%1'.").arg(src.originalPath);
        return false;
    }
    return true;
}

void MainWindow::syncLayoutToGif(const ProjectSource& src,
                                  std::function<void(bool, const QString&)> onDone)
{
    if (m_gifSyncWatcher.isRunning()) {
        onDone(false, tr("GIF sync already in progress."));
        return;
    }

    QStringList framePaths;
    for (const auto& model : m_session->activeAtlas().layoutModels) {
        for (const auto& sprite : model.sprites) {
            if (sprite && sprite->path.startsWith(src.cachedFolderPath))
                framePaths.append(sprite->path);
        }
    }
    if (framePaths.isEmpty()) {
        onDone(false, tr("No frames found for this GIF source."));
        return;
    }

    const int fps = (!m_session->activeAtlas().timelines.isEmpty())
                    ? m_session->activeAtlas().timelines.first().fps : 8;
    AnimationTimeline tl;
    tl.fps    = fps;
    tl.frames = framePaths;
    auto layoutModels = m_session->activeAtlas().layoutModels;
    auto outPath      = src.originalPath;

    AnimationExportService::ExportCallbacks cbs;
    cbs.setLoading = [this](bool v) {
        QMetaObject::invokeMethod(this, [this, v]() { setLoading(v); }, Qt::QueuedConnection);
    };
    cbs.setStatus = [this](const QString& s) {
        QMetaObject::invokeMethod(this, [this, s]() { m_statusLabel->setText(s); }, Qt::QueuedConnection);
    };
    cbs.showError = [this](const QString& t, const QString& msg) {
        QMetaObject::invokeMethod(this, [this, t, msg]() { MessageDialog::critical(this, t, msg); }, Qt::QueuedConnection);
    };

    // shared_ptr lets the background lambda write the error while the
    // m_gifSyncOnDone closure reads it after the future finishes (no race).
    auto errorOut = std::make_shared<QString>();
    m_gifSyncOnDone = [onDone = std::move(onDone), errorOut](bool ok) {
        onDone(ok, *errorOut);
    };

    setLoading(true);

    disconnect(&m_gifSyncWatcher, &QFutureWatcher<bool>::finished,
               this, &MainWindow::onGifSyncFinished);
    connect(&m_gifSyncWatcher, &QFutureWatcher<bool>::finished,
            this, &MainWindow::onGifSyncFinished);

    m_gifSyncWatcher.setFuture(
        QtConcurrent::run([tl, layoutModels, fps, outPath, cbs, errorOut]() -> bool {
            bool ok = AnimationExportService::exportAnimation(
                {tl}, 0, layoutModels, fps, outPath, cbs);
            if (!ok)
                *errorOut = QStringLiteral("GIF export failed (ImageMagick required).");
            return ok;
        }));
}

void MainWindow::onGifSyncFinished() {
    const bool ok = m_gifSyncWatcher.result();
    if (m_gifSyncOnDone) {
        m_gifSyncOnDone(ok);
        m_gifSyncOnDone = nullptr;
    }
}
