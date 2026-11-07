/// @file MainWindow.Frames.cpp
/// Active frames: adding, removing and splitting sprites, the excluded atlas, and the frame list file passed to spratlayout.

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

bool MainWindow::ensureFrameListInput() {
    if (m_session->activeFramePaths.isEmpty()) {
        return false;
    }

    // Compute common parent directory of all frames before writing the list
    QString commonFolder;
    if (!m_session->activeFramePaths.isEmpty()) {
        commonFolder = QFileInfo(m_session->activeFramePaths.first()).absolutePath();
        for (int i = 1; i < m_session->activeFramePaths.size(); ++i) {
            const QString path = m_session->activeFramePaths.at(i);
            // Quick check: if path starts with commonFolder (and is in a subfolder), we're good
            if (path.startsWith(commonFolder + "/")) {
                continue;
            }
            
            // Otherwise, we need to find the common ancestor.
            // Moving up the directory tree using string manipulation is faster than QFileInfo.
            while (!commonFolder.isEmpty() && !path.startsWith(commonFolder + "/")) {
                int lastSlash = commonFolder.lastIndexOf('/');
                if (lastSlash <= 0) { // Reached root or empty
                    commonFolder = (lastSlash == 0) ? "/" : "";
                    break;
                }
                commonFolder = commonFolder.left(lastSlash);
            }
        }
    }

    // Reuse the same path across calls to avoid a race where spratlayout tries to
    // open the file just after a second ensureFrameListInput() call has deleted it.
    if (m_session->frameListPath.isEmpty()) {
        const QString fileName = QString("sprat-gui-frames-%1.txt")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        m_session->frameListPath = QDir::temp().filePath(fileName);
    }
    const QString frameListPath = m_session->frameListPath;

    QFile file(frameListPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning() << "[FrameList] Failed to write:" << frameListPath;
        return false;
    }
    QTextStream out(&file);
    for (const QString& path : m_session->activeFramePaths) {
        out << path << "\n";
    }
    out.flush();
    file.close();
    qInfo() << "[FrameList] Written:" << frameListPath
            << "exists=" << QFile::exists(frameListPath)
            << "frames=" << m_session->activeFramePaths.size();

    m_session->layoutSourcePath = frameListPath;
    m_session->layoutSourceIsList = true;
    m_session->currentFolder = commonFolder;
    updateManualFrameLabel();
    return true;
}

void MainWindow::populateActiveFrameListFromModel() {
    m_session->activeFramePaths.clear();
    for (const auto& model : m_session->activeAtlas().layoutModels) {
        for (const auto& sprite : model.sprites) {
            m_session->activeFramePaths.append(sprite->path);
        }
    }
}

void MainWindow::updateManualFrameLabel() {
    if (!m_folderLabel) {
        return;
    }
    if (m_session->activeFramePaths.isEmpty()) {
        m_folderLabel->setText(tr("No sprites"));
    } else {
        m_folderLabel->setText(tr("%n sprite(s)", nullptr, int(m_session->activeFramePaths.size())));
    }
}

void MainWindow::onCanvasAddFramesRequested() {
    QString startDir = m_session->currentFolder;
    if (startDir.isEmpty() && !m_session->activeFramePaths.isEmpty()) {
        startDir = QFileInfo(m_session->activeFramePaths.first()).absoluteDir().absolutePath();
    }
    QString filter = tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tga *.dds)");
    QStringList files = QFileDialog::getOpenFileNames(this, tr("Add Frames"), startDir, filter);
    if (files.isEmpty()) {
        return;
    }

    QSet<QString> existing(m_session->activeFramePaths.begin(), m_session->activeFramePaths.end());
    QStringList added;
    for (const QString& file : files) {
        QFileInfo info(file);
        if (!info.exists() || info.isDir()) {
            continue;
        }
        QString absPath = info.absoluteFilePath();
        if (existing.contains(absPath)) {
            continue;
        }
        existing.insert(absPath);
        added.append(absPath);
    }
    if (added.isEmpty()) {
        MessageDialog::information(this, tr("Add Frames"), tr("All selected frames are already loaded."));
        return;
    }

    const QStringList oldFramePaths = m_session->activeFramePaths;
    m_undoStack->push(new AddFramesCommand(
        &m_session->activeFramePaths,
        added,
        oldFramePaths,
        [this]() { return ensureFrameListInput(); },
        [this]() {
            updateManualFrameLabel();
            scheduleLayoutRebuild(true);
        }
    ));

    m_statusLabel->setText(QString(tr("Adding %1 frame(s)...")).arg(added.size()));
    if (!ensureFrameListInput()) {
        m_session->activeFramePaths = oldFramePaths;
        MessageDialog::warning(this, tr("Add Frames"), tr("Couldn't save the sprite list to the temporary folder, so nothing was changed.\n\nCheck that the disk isn't full, then try again."));
        return;
    }
    // Canvas action - rebuild immediately with loading UI
    updateManualFrameLabel();
    scheduleLayoutRebuild(true);
}

void MainWindow::onAddFramesRequested() {
    QString startDir = m_session->currentFolder;
    if (startDir.isEmpty() && !m_session->activeFramePaths.isEmpty()) {
        startDir = QFileInfo(m_session->activeFramePaths.first()).absoluteDir().absolutePath();
    }
    QString filter = tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tga *.dds)");
    QStringList files = QFileDialog::getOpenFileNames(this, tr("Add Frames"), startDir, filter);
    if (files.isEmpty()) {
        return;
    }

    QSet<QString> existing(m_session->activeFramePaths.begin(), m_session->activeFramePaths.end());
    QStringList added;
    for (const QString& file : files) {
        QFileInfo info(file);
        if (!info.exists() || info.isDir()) {
            continue;
        }
        QString absPath = info.absoluteFilePath();
        if (existing.contains(absPath)) {
            continue;
        }
        existing.insert(absPath);
        added.append(absPath);
    }
    if (added.isEmpty()) {
        MessageDialog::information(this, tr("Add Frames"), tr("All selected frames are already loaded."));
        return;
    }

    const QStringList oldFramePaths = m_session->activeFramePaths;
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
        MessageDialog::warning(this, tr("Add Frames"), tr("Couldn't save the sprite list to the temporary folder, so nothing was changed.\n\nCheck that the disk isn't full, then try again."));
        return;
    }
    updateManualFrameLabel();
    // Navigator/deferred action - use lazy loading (debounce)
    scheduleLayoutRebuild();
}

void MainWindow::syncExcludedAtlas() {
    if (!m_session) return;
    const int excIdx = m_session->excludedAtlasIndex();
    if (excIdx < 0 || excIdx >= m_session->atlases.size()) return;
    AtlasEntry& excl = m_session->atlases[excIdx];
    excl.spritePaths.clear();

    // Collect excluded paths from sources
    for (const auto& src : m_session->sources) {
        const QString base = QDir::cleanPath(
            src.cachedFolderPath.isEmpty() ? src.originalPath : src.cachedFolderPath);
        for (const QString& rel : src.excludedFiles)
            excl.spritePaths.append(base + '/' + rel);
    }
    // Collect excluded paths from smart folders (legacy)
    for (const auto& sf : m_session->smartFolders) {
        for (const QString& rel : sf.excludedFiles) {
            const QString abs = sf.path + '/' + rel;
            if (!excl.spritePaths.contains(abs))
                excl.spritePaths.append(abs);
        }
    }

    // Remove excluded sprites from every non-excluded atlas
    const QSet<QString> exclSet(excl.spritePaths.begin(), excl.spritePaths.end());
    for (auto& a : m_session->atlases) {
        if (a.isExcluded) continue;
        a.spritePaths.erase(
            std::remove_if(a.spritePaths.begin(), a.spritePaths.end(),
                [&exclSet](const QString& sp) {
                    return exclSet.contains(QFileInfo(sp).absoluteFilePath())
                        || exclSet.contains(sp);
                }),
            a.spritePaths.end());
    }
}

QStringList MainWindow::excludedSpritePaths() const {
    QStringList paths;
    if (!m_session) return paths;
    const int excIdx = m_session->excludedAtlasIndex();
    if (excIdx < 0 || excIdx >= m_session->atlases.size()) return paths;
    for (const QString& p : m_session->atlases[excIdx].spritePaths)
        if (QFileInfo::exists(p)) paths.append(p);
    return paths;
}

void MainWindow::addToExcludedFiles(const QString& absPath) {
    const QString cleaned = QDir::cleanPath(absPath);
    for (auto& src : m_session->sources) {
        const QString base = QDir::cleanPath(
            src.cachedFolderPath.isEmpty() ? src.originalPath : src.cachedFolderPath);
        if (cleaned.startsWith(base + '/')) {
            const QString rel = cleaned.mid(base.length() + 1);
            if (!src.excludedFiles.contains(rel)) src.excludedFiles.append(rel);
            return;
        }
    }
    for (auto& sf : m_session->smartFolders) {
        if (!sf.path.isEmpty() && cleaned.startsWith(sf.path + '/')) {
            const QString rel = cleaned.mid(sf.path.length() + 1);
            if (!sf.excludedFiles.contains(rel)) sf.excludedFiles.append(rel);
            return;
        }
    }
}

void MainWindow::removeFromExcludedFiles(const QString& absPath) {
    const QString cleaned = QDir::cleanPath(absPath);
    for (auto& src : m_session->sources) {
        const QString base = QDir::cleanPath(
            src.cachedFolderPath.isEmpty() ? src.originalPath : src.cachedFolderPath);
        if (cleaned.startsWith(base + '/')) {
            src.excludedFiles.removeAll(cleaned.mid(base.length() + 1));
            return;
        }
    }
    for (auto& sf : m_session->smartFolders) {
        if (!sf.path.isEmpty() && cleaned.startsWith(sf.path + '/')) {
            sf.excludedFiles.removeAll(cleaned.mid(sf.path.length() + 1));
            return;
        }
    }
}

void MainWindow::onCanvasRemoveFramesRequested(const QStringList& paths) {
    if (paths.isEmpty()) {
        return;
    }

    // In AtlasesManagement Layout mode, "remove" means move to the neutral (Default)
    // atlas. If this already IS the neutral atlas, remove from the session entirely.
    // Both operations mirror Navigation-mode behaviour and bypass the undo stack.
    if (m_currentWorkspace == m_atlasesManagementWorkspace
        && m_atlasesManagementWorkspace
        && m_atlasesManagementWorkspace->viewMode() == AtlasesManagementWorkspace::ViewMode::Layout
        && m_session) {

        const int srcIdx = m_session->activeAtlasIndex;
        if (srcIdx >= 0 && srcIdx < m_session->atlases.size()) {
            AtlasEntry& src = m_session->atlases[srcIdx];

            // Only act on paths that belong to this atlas.
            QStringList targets;
            for (const QString& p : paths)
                if (src.spritePaths.contains(p))
                    targets.append(p);

            if (!targets.isEmpty()) {
                // Exclude sprites: persist to excludedFiles and sync the excluded atlas.
                for (const QString& p : targets)
                    addToExcludedFiles(p);
                syncExcludedAtlas();

                // Deselect any excluded sprite.
                if (m_session->selectedSprite && targets.contains(m_session->selectedSprite->path))
                    onSpriteSelected(nullptr);
                m_session->selectedSprites.erase(
                    std::remove_if(m_session->selectedSprites.begin(),
                                   m_session->selectedSprites.end(),
                                   [&targets](const SpritePtr& s) {
                                       return s && targets.contains(s->path);
                                   }),
                    m_session->selectedSprites.end());

                // Strip from activeFramePaths and layoutModels.
                for (const QString& p : targets)
                    m_session->activeFramePaths.removeAll(p);
                const QSet<QString> removedSet(targets.begin(), targets.end());
                for (auto& model : src.layoutModels) {
                    QVector<SpritePtr> kept;
                    kept.reserve(model.sprites.size());
                    for (const auto& s : model.sprites)
                        if (!removedSet.contains(s->path))
                            kept.append(s);
                    model.sprites = std::move(kept);
                }

                emit m_session->atlasesChanged();
                m_atlasesManagementWorkspace->refreshSpriteList(m_session->atlases);
                if (m_atlasWorkspace && m_atlasWorkspace->canvas()) m_atlasWorkspace->canvas()->setModels(src.layoutModels);
                refreshSpriteTree();
                scheduleLayoutRebuild(true);
            }
        }
        return;
    }

    QStringList targets;
    for (const QString& path : paths) {
        if (m_session->activeFramePaths.contains(path)) {
            targets.append(path);
        }
    }
    if (targets.isEmpty()) {
        return;
    }

    // Atlas membership before the exclusion, so undo puts each sprite back in its atlas.
    QVector<QStringList> savedAtlasSpritePaths;
    for (const AtlasEntry& atlas : m_session->atlases)
        savedAtlasSpritePaths.append(atlas.spritePaths);

    // Persist exclusion so sprites appear in the "Excluded" section.
    for (const QString& p : targets)
        addToExcludedFiles(p);
    syncExcludedAtlas();
    if (m_atlasesManagementWorkspace && m_currentWorkspace == m_atlasesManagementWorkspace)
        m_atlasesManagementWorkspace->refreshSpriteList(m_session->atlases);

    QSet<QString> timelineNames;
    for (const auto& timeline : m_session->activeAtlas().timelines) {
        for (const QString& frame : timeline.frames) {
            if (targets.contains(frame)) {
                timelineNames.insert(timeline.name);
                break;
            }
        }
    }

    if (!timelineNames.isEmpty()) {
        QString warning = QString(tr("The selected frame(s) are referenced by the following timelines:\n%1\nRemoving them will drop those entries from the timelines. Continue?"))
                          .arg(QStringList(timelineNames.values()).join(", "));
        if (MessageDialog::confirmWarning(this, tr("Remove Frames"), warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
    }

    // Deselect sprite if it's being removed, and clear the editor immediately.
    if (m_session->selectedSprite && targets.contains(m_session->selectedSprite->path)) {
        onSpriteSelected(nullptr);
    }
    m_session->selectedSprites.erase(
        std::remove_if(m_session->selectedSprites.begin(), m_session->selectedSprites.end(),
            [&targets](const SpritePtr& sprite) { return sprite && targets.contains(sprite->path); }),
        m_session->selectedSprites.end()
    );

    const QStringList savedActivePaths = m_session->activeFramePaths;
    const QVector<AnimationTimeline> savedTimelines = m_session->activeAtlas().timelines;
    const int savedTimelineIdx = m_session->selectedTimelineIndex;
    const QVector<LayoutModel> savedLayoutModels = m_session->activeAtlas().layoutModels;

    auto postExecuteRedo = [this, targets]() {
        // Again on redo: undo took them off the excluded list.
        for (const QString& p : targets)
            addToExcludedFiles(p);
        syncExcludedAtlas();
        if (m_session->activeFramePaths.isEmpty()) {
            m_session->layoutSourcePath.clear();
            m_session->layoutSourceIsList = false;
            m_session->removeFrameListFile();
            m_session->activeAtlas().layoutModels.clear();
            {
                auto* rmCanvas = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
                if (rmCanvas) rmCanvas->clearCanvas();
            }
            m_session->selectedSprites.clear();
            m_session->selectedSprite.reset();
            m_statusLabel->setText(tr("No frames loaded"));
            m_folderLabel->setText(tr("No sprites"));
            m_session->cachedLayoutOutput.clear();
            m_session->cachedLayoutScale = 1.0;
            updateMainContentView();
            updateUiState();
            refreshSpriteTree();
            refreshTimelineList();
            refreshTimelineFrames();
            refreshAnimationTest();
        } else {
            captureOldSpritePositions();
            auto* rmCanvas = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
            if (rmCanvas) rmCanvas->removeSprites(targets);
            m_statusLabel->setText(QString(tr("Removed %1 frame(s)")).arg(targets.size()));
            refreshTimelineFrames();
            refreshTimelineList();
            refreshAnimationTest();
            scheduleLayoutRebuild(true, true);
        }
    };

    auto postExecuteUndo = [this, targets, savedAtlasSpritePaths]() {
        for (const QString& p : targets)
            removeFromExcludedFiles(p);
        if (savedAtlasSpritePaths.size() == m_session->atlases.size()) {
            for (int i = 0; i < m_session->atlases.size(); ++i)
                if (!m_session->atlases[i].isExcluded)
                    m_session->atlases[i].spritePaths = savedAtlasSpritePaths[i];
        }
        syncExcludedAtlas();
        if (m_atlasesManagementWorkspace && m_currentWorkspace == m_atlasesManagementWorkspace)
            m_atlasesManagementWorkspace->refreshSpriteList(m_session->atlases);
        refreshSpriteTree();
        refreshTimelineFrames();
        refreshTimelineList();
        refreshAnimationTest();
        scheduleLayoutRebuild(true);
    };

    // Both smart-folder and manually imported sprites are removed from the layout only
    // (files are never deleted from disk). RemoveSpritesCommand is kept distinct for
    // potential future divergence (e.g. adding to the exclusion list for smart folders).
    if (shouldDeleteRemovedSpritesFromSource()) {
        m_undoStack->push(new RemoveSpritesCommand(
            &m_session->activeFramePaths, &m_session->activeAtlas().timelines,
            &m_session->selectedTimelineIndex, &m_session->activeAtlas().layoutModels,
            targets,
            savedActivePaths, savedTimelines, savedTimelineIdx, savedLayoutModels,
            [this]() { return ensureFrameListInput(); },
            postExecuteRedo,
            postExecuteUndo
        ));
        return;
    }

    m_undoStack->push(new RemoveFramesCommand(
        &m_session->activeFramePaths, &m_session->activeAtlas().timelines,
        &m_session->selectedTimelineIndex, &m_session->activeAtlas().layoutModels,
        targets,
        savedActivePaths, savedTimelines, savedTimelineIdx, savedLayoutModels,
        [this]() { return ensureFrameListInput(); },
        postExecuteRedo,
        postExecuteUndo
    ));
}

void MainWindow::onRemoveFramesRequested(const QStringList& paths) {
    onCanvasRemoveFramesRequested(paths);
}

void MainWindow::onSplitSpriteRequested(SpritePtr sprite, Qt::Orientation orientation, int localPos) {
    if (!sprite || !m_session) return;

    QImage originalImage(sprite->path);
    if (originalImage.isNull()) {
        m_statusLabel->setText(tr("Split failed: could not load %1").arg(sprite->path));
        return;
    }

    // Trim offsets
    int l = sprite->trimmed ? sprite->trimRect.x()      : 0;
    int t = sprite->trimmed ? sprite->trimRect.y()      : 0;
    int b = sprite->trimmed ? sprite->trimRect.height() : 0;
    int trimmedH = originalImage.height() - t - b;

    QImage imgA, imgB;
    if (!sprite->rotated) {
        if (orientation == Qt::Horizontal) {
            int origY = localPos + t;
            origY = qBound(1, origY, originalImage.height() - 1);
            imgA = originalImage.copy(0, 0, originalImage.width(), origY);
            imgB = originalImage.copy(0, origY, originalImage.width(), originalImage.height() - origY);
        } else {
            int origX = localPos + l;
            origX = qBound(1, origX, originalImage.width() - 1);
            imgA = originalImage.copy(0, 0, origX, originalImage.height());
            imgB = originalImage.copy(origX, 0, originalImage.width() - origX, originalImage.height());
        }
    } else {
        if (orientation == Qt::Horizontal) {
            int origX = localPos + l;
            origX = qBound(1, origX, originalImage.width() - 1);
            imgA = originalImage.copy(0, 0, origX, originalImage.height());
            imgB = originalImage.copy(origX, 0, originalImage.width() - origX, originalImage.height());
        } else {
            int origY = (trimmedH - 1 - localPos) + t;
            origY = qBound(1, origY, originalImage.height() - 1);
            imgA = originalImage.copy(0, 0, originalImage.width(), origY);
            imgB = originalImage.copy(0, origY, originalImage.width(), originalImage.height() - origY);
        }
    }

    if (imgA.isNull() || imgB.isNull()) {
        m_statusLabel->setText(tr("Split failed: degenerate result"));
        return;
    }

    QFileInfo srcInfo(sprite->path);
    QString outDir = (!m_session->sourceFolder.isEmpty())
                         ? m_session->sourceFolder
                         : srcInfo.absolutePath();
    QDir().mkpath(outDir);

    const QString base   = srcInfo.completeBaseName();
    const QString suffix = srcInfo.suffix();
    auto makeUniquePath = [&](const QString& hint) -> QString {
        QString p = QDir(outDir).filePath(hint + "." + suffix);
        if (!QFileInfo::exists(p)) return p;
        for (int i = 1; i <= 99; ++i) {
            p = QDir(outDir).filePath(hint + "_" + QString::number(i) + "." + suffix);
            if (!QFileInfo::exists(p)) return p;
        }
        return p;
    };
    const QString pathA = makeUniquePath(base + "_a");
    const QString pathB = makeUniquePath(base + "_b");

    if (!imgA.save(pathA) || !imgB.save(pathB)) {
        m_statusLabel->setText(tr("Split failed: could not save sub-images"));
        return;
    }

    int idx = m_session->activeFramePaths.indexOf(sprite->path);
    if (idx >= 0) {
        m_session->activeFramePaths.removeAt(idx);
        m_session->activeFramePaths.insert(idx, pathB);
        m_session->activeFramePaths.insert(idx, pathA);
    } else {
        idx = m_session->activeFramePaths.size(); // will be appended
        m_session->activeFramePaths.append(pathA);
        m_session->activeFramePaths.append(pathB);
    }
    // idx is the position where pathA was inserted

    ensureFrameListInput();
    // Explicit user action - rebuild immediately
    scheduleLayoutRebuild(true);
    m_statusLabel->setText(tr("Sprite split into %1 and %2").arg(
        QFileInfo(pathA).fileName(), QFileInfo(pathB).fileName()));

    // Build trim rect for the command (used on subsequent redo to re-split)
    QRect trimRect;
    if (sprite->trimmed) {
        trimRect = sprite->trimRect;
    }

    m_undoStack->push(new SplitSpriteCommand(
        &m_session->activeFramePaths,
        sprite->path,
        pathA,
        pathB,
        idx,
        orientation,
        localPos,
        sprite->rotated,
        trimRect,
        [this]() { return ensureFrameListInput(); },
        [this]() { scheduleLayoutRebuild(true); }
    ));
}
