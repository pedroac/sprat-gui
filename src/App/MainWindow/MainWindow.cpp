#include "MainWindow.h"
#include "LayoutOrchestrator.h"
#include "CliSetupController.h"
#include "ProjectController.h"
#include "PackedAtlasView.h"
#include "AtlasesManagementWorkspace.h"
#include "UndoCommands.h"
#include "AnimationCanvas.h"
#include "MarkersDialog.h"
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
#include <algorithm>
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
#include <QUndoStack>
#include <QShortcut>
#include <QToolButton>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTime>
#include <QDialog>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QPainter>
#include <QImage>
#include <QDirIterator>
#include "ArchiveExtractor.h"

Q_LOGGING_CATEGORY(mainWindow, "mainWindow")
Q_LOGGING_CATEGORY(cli, "cli")
#ifdef SPRAT_EMBEDDED_CLI
#include "EmbeddedCli.h"
#endif

Q_LOGGING_CATEGORY(project, "project")
Q_LOGGING_CATEGORY(autosave, "autosave")

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

QString sanitizeSubfolderName(const QString& name) {
    QString result;
    result.reserve(name.size());
    for (const QChar& c : name) {
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':')
                || c == QLatin1Char('*') || c == QLatin1Char('?') || c == QLatin1Char('"')
                || c == QLatin1Char('<') || c == QLatin1Char('>') || c == QLatin1Char('|')) {
            result += QLatin1Char('_');
        } else {
            result += c;
        }
    }
    return result.isEmpty() ? QStringLiteral("source") : result;
}
}


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
        if (trashPath.isEmpty())
            QDir(removed.cachedFolderPath).removeRecursively(); // fallback
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
            if (src.originalPath.endsWith(".gif", Qt::CaseInsensitive))
                ok = syncLayoutToGif(src, error);
            else
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
            ok = syncLayoutToGif(src, error);
        } else {
            ok = syncLayoutToImage(src, error);
        }
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

bool MainWindow::syncLayoutToGif(const ProjectSource& src, QString& error) {
    QStringList framePaths;
    for (const auto& model : m_session->activeAtlas().layoutModels) {
        for (const auto& sprite : model.sprites) {
            if (sprite && sprite->path.startsWith(src.cachedFolderPath))
                framePaths.append(sprite->path);
        }
    }
    if (framePaths.isEmpty()) {
        error = tr("No frames found for this GIF source.");
        return false;
    }

    const int fps = (!m_session->activeAtlas().timelines.isEmpty())
                    ? m_session->activeAtlas().timelines.first().fps : 8;
    AnimationTimeline tl;
    tl.fps    = fps;
    tl.frames = framePaths;

    const bool ok = AnimationExportService::exportAnimation(
        {tl}, 0, m_session->activeAtlas().layoutModels, fps,
        src.originalPath,
        {
            [this](bool v) { setLoading(v); },
            [this](const QString& s) { m_statusLabel->setText(s); }
        });
    if (!ok) error = tr("GIF export failed (ImageMagick required).");
    return ok;
}

void MainWindow::syncFramePathsToNeutralAtlas(DropAction action) {
    if (m_projectController)
        m_projectController->syncFramePathsToNeutralAtlas(
            static_cast<ProjectController::DropAction>(action));
}

void MainWindow::registerLoadedSource(const QString& sourcePath, DropAction action,
                                      const QString& cachedFolderPath) {
    if (m_projectController)
        m_projectController->registerLoadedSource(
            sourcePath,
            static_cast<ProjectController::DropAction>(action),
            cachedFolderPath);
}

QString MainWindow::computeSourceSubfolderName(const QString& sourcePath) const {
    return m_projectController ? m_projectController->computeSourceSubfolderName(sourcePath) : QString();
}

QString MainWindow::makeUniqueSourceName(const QString& baseName) const {
    return m_projectController ? m_projectController->makeUniqueSourceName(baseName) : baseName;
}

QStringList MainWindow::copyFramesToSourceSubfolder(const QStringList& frames,
                                                    const QString& subfolderPath,
                                                    bool overwriteDuplicates) {
    return m_projectController
               ? m_projectController->copyFramesToSourceSubfolder(frames, subfolderPath, overwriteDuplicates)
               : frames;
}

void MainWindow::openSettingsDialogForSection(SettingsDialog::Section section) {
    SettingsDialog dlg(m_settings, m_cliPaths, this, section);
    QObject::connect(&dlg, &SettingsDialog::installCliToolsRequested, this, &MainWindow::installCliTools);
    QObject::connect(&dlg, &SettingsDialog::syncNowRequested, this, &MainWindow::onSyncNowRequested);
    if (dlg.exec() == QDialog::Accepted) {
        m_settings = dlg.getSettings();
        m_cliPaths = dlg.getCliPaths();
        if (m_cliSetup) m_cliSetup->setCliBaseDir(m_cliPaths.baseDir);
        applySettings();
        CliToolsConfig::saveAppSettings(m_settings, m_cliPaths);
        checkCliTools();
    }
}

#ifdef Q_OS_WASM
MainWindow* MainWindow::s_wasmInstance = nullptr;
#endif

/**
 * @brief Constructs the MainWindow.
 * 
 * Initializes the UI, processes, and timers.
 */
#include "ViewUtils.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    m_session = new ProjectSession(this);
    m_settings = CliToolsConfig::loadAppSettings();
    m_cliPaths = CliToolsConfig::loadCliPaths();

    // Initialize undo stack before setupUi so actions can reference it
    m_undoStack = new QUndoStack(this);
    m_undoStack->setUndoLimit(AppConstants::kUndoStackLimit);
    connect(m_undoStack, &QUndoStack::cleanChanged,
            this, [this](bool clean) { setWindowModified(!clean); });
    connect(m_undoStack, &QUndoStack::indexChanged,
            this, [this](int) { syncPivotSpinsFromSprite(); });

    setupUi();
    setupKeyboardShortcuts();
    if (m_previewView && m_previewView->overlay()) {
        connect(m_previewView->overlay(), &EditorOverlayItem::pivotDragFinished,
                this, [this](int oldX, int oldY, int newX, int newY) {
            if (!m_session->selectedSprite) return;
            const CoordUnit unit = m_settings.coordUnit;
            const QSize activeSize = spriteCoordinateSpaceSize(m_session->selectedSprite);
            const double relX = (unit == CoordUnit::Percent && activeSize.width() > 0)
                ? (double(newX) * 100.0 / activeSize.width())
                : double(newX);
            const double relY = (unit == CoordUnit::Percent && activeSize.height() > 0)
                ? (double(newY) * 100.0 / activeSize.height())
                : double(newY);
            QVector<SetPivotCommand::CoTarget> coTargets;
            if (m_settings.propagateEditsToChecked) {
                for (const auto& sprite : m_session->selectedSprites) {
                    if (sprite && sprite != m_session->selectedSprite) {
                        const QPair<int, int> oldPos{sprite->pivotX, sprite->pivotY};
                        if (unit == CoordUnit::Percent) {
                            const QSize coSize = spriteCoordinateSpaceSize(sprite);
                            sprite->pivotX = coSize.width() > 0
                                ? qRound(relX * coSize.width() / 100.0)
                                : newX;
                            sprite->pivotY = coSize.height() > 0
                                ? qRound(relY * coSize.height() / 100.0)
                                : newY;
                        } else {
                            sprite->pivotX = newX;
                            sprite->pivotY = newY;
                        }
                        const QPair<int, int> newPos{sprite->pivotX, sprite->pivotY};
                        if (oldPos != newPos) {
                            coTargets.append({sprite, oldPos, newPos});
                        }
                    }
                }
            }
            AnimationPreviewService::invalidateBounds();
            updateOnionSkinDisplay();
            m_undoStack->push(new SetPivotCommand(m_session->selectedSprite,
                                                  oldX, oldY, newX, newY, true,
                                                  std::move(coTargets)));
        });
        connect(m_previewView->overlay(), &EditorOverlayItem::markerDragFinished,
                this, [this](const QVector<NamedPoint>& oldPoints, const QVector<NamedPoint>& newPoints) {
            if (!m_session->selectedSprite) return;
            QVector<SetMarkersCommand::CoTarget> coTargets;
            if (m_settings.propagateEditsToChecked) {
                for (const auto& sprite : m_session->selectedSprites) {
                    if (sprite && sprite != m_session->selectedSprite) {
                        const QVector<NamedPoint> oldCoPoints = sprite->points;
                        sprite->points = newPoints;
                        coTargets.append({sprite, oldCoPoints, sprite->points});
                    }
                }
            }
            m_undoStack->push(new SetMarkersCommand(
                m_session->selectedSprite,
                oldPoints,
                newPoints,
                [this]() {
                    m_previewView->overlay()->updateLayout();
                    if (m_animCanvas && m_animCanvas->overlay())
                        m_animCanvas->overlay()->updateLayout();
                    refreshHandleCombo();
                },
                std::move(coTargets)
            ));
        });
    }

    if (m_animCanvas && m_animCanvas->overlay()) {
        connect(m_animCanvas->overlay(), &EditorOverlayItem::pivotDragFinished,
                this, [this](int oldX, int oldY, int newX, int newY) {
            if (!m_session->selectedSprite) return;
            const CoordUnit unit = m_settings.coordUnit;
            const QSize activeSize = spriteCoordinateSpaceSize(m_session->selectedSprite);
            const double relX = (unit == CoordUnit::Percent && activeSize.width() > 0)
                ? (double(newX) * 100.0 / activeSize.width())
                : double(newX);
            const double relY = (unit == CoordUnit::Percent && activeSize.height() > 0)
                ? (double(newY) * 100.0 / activeSize.height())
                : double(newY);
            QVector<SetPivotCommand::CoTarget> coTargets;
            if (m_settings.propagateEditsToChecked) {
                for (const auto& sprite : m_session->selectedSprites) {
                    if (sprite && sprite != m_session->selectedSprite) {
                        const QPair<int, int> oldPos{sprite->pivotX, sprite->pivotY};
                        if (unit == CoordUnit::Percent) {
                            const QSize coSize = spriteCoordinateSpaceSize(sprite);
                            sprite->pivotX = coSize.width() > 0
                                ? qRound(relX * coSize.width() / 100.0)
                                : newX;
                            sprite->pivotY = coSize.height() > 0
                                ? qRound(relY * coSize.height() / 100.0)
                                : newY;
                        } else {
                            sprite->pivotX = newX;
                            sprite->pivotY = newY;
                        }
                        const QPair<int, int> newPos{sprite->pivotX, sprite->pivotY};
                        if (oldPos != newPos) {
                            coTargets.append({sprite, oldPos, newPos});
                        }
                    }
                }
            }
            AnimationPreviewService::invalidateBounds();
            updateOnionSkinDisplay();
            m_undoStack->push(new SetPivotCommand(m_session->selectedSprite,
                                                  oldX, oldY, newX, newY, true,
                                                  std::move(coTargets)));
        });
        connect(m_animCanvas->overlay(), &EditorOverlayItem::markerDragFinished,
                this, [this](const QVector<NamedPoint>& oldPoints, const QVector<NamedPoint>& newPoints) {
            if (!m_session->selectedSprite) return;
            QVector<SetMarkersCommand::CoTarget> coTargets;
            if (m_settings.propagateEditsToChecked) {
                for (const auto& sprite : m_session->selectedSprites) {
                    if (sprite && sprite != m_session->selectedSprite) {
                        const QVector<NamedPoint> oldCoPoints = sprite->points;
                        sprite->points = newPoints;
                        coTargets.append({sprite, oldCoPoints, sprite->points});
                    }
                }
            }
            m_undoStack->push(new SetMarkersCommand(
                m_session->selectedSprite,
                oldPoints,
                newPoints,
                [this]() {
                    m_previewView->overlay()->updateLayout();
                    if (m_animCanvas && m_animCanvas->overlay())
                        m_animCanvas->overlay()->updateLayout();
                    refreshHandleCombo();
                },
                std::move(coTargets)
            ));
        });
    }

    // Load recent projects
    m_recentProjects = CliToolsConfig::loadRecentProjects();
    updateRecentProjectsMenu();

    // Apply theme on startup
    SettingsCoordinator::applyTheme(m_settings.theme);

    setupCliInstallOverlay();
#ifdef Q_OS_WASM
    s_wasmInstance = this;
    setAcceptDrops(false);
    // Install JS drop handlers: intercept file drops, write to MEMFS, then call sprat_on_file_picked
    QTimer::singleShot(0, []() { wasmInstallDropHandlers(); });
    // Fix Qt 6.10 WASM keyboard focus issue (DEL, shortcuts, etc.)
    QTimer::singleShot(100, []() { wasmSetupKeyboardFocus(); });
#else
    setAcceptDrops(true);
#endif

    // Create LayoutOrchestrator
    {
        LayoutOrchestrator::Config cfg;
        cfg.session         = m_session;
        cfg.canvas          = m_canvas;
        cfg.profileCombo    = m_profileCombo;
        cfg.resolutionCombo = m_sourceResolutionCombo;
        cfg.atlasViewStack       = m_atlasViewStack;
        cfg.atlasMgmtWorkspace   = m_atlasesManagementWorkspace;
        cfg.layoutBinary         = m_spratLayoutBin;
        cfg.activeFramesAreInSourceFolder = [this]() { return activeFramesAreInSourceFolder(); };
        cfg.copyActiveFramesToSourceFolder = [this](bool overwrite) { copyActiveFramesToSourceFolder(overwrite); };
        cfg.selectedProfileDefinition = [this](SpratProfile& out) { return selectedProfileDefinition(out); };
        cfg.layoutParserFolder = [this]() { return layoutParserFolder(); };
        cfg.sourceFolderMatchesActiveFrames = [this]() { return sourceFolderMatchesActiveFrames(); };
        cfg.configuredProfiles = [this]() { return configuredProfiles(); };
        m_layoutOrchestrator = new LayoutOrchestrator(cfg, this);

        // Sync initial settings
        m_layoutOrchestrator->setSyncMode(m_settings.syncMode);
        m_layoutOrchestrator->setDeduplicateMode(m_settings.deduplicateMode);
        m_layoutOrchestrator->setLayoutZoomOnChange(m_settings.layoutZoomOnChange);
        m_layoutOrchestrator->setCLIReady(m_cliReady);
        m_layoutOrchestrator->setMergeReplaceAllDuplicates(m_mergeReplaceAllDuplicates);
        m_layoutOrchestrator->setActiveWorkspace(static_cast<int>(m_activeWorkspace));

        connect(m_layoutOrchestrator, &LayoutOrchestrator::statusMessageChanged,
                this, [this](const QString& msg) { if (m_statusLabel) m_statusLabel->setText(msg); });
        connect(m_layoutOrchestrator, &LayoutOrchestrator::loadingStateChanged,
                this, &MainWindow::setLoading);
        connect(m_layoutOrchestrator, &LayoutOrchestrator::spriteTreeRefreshNeeded,
                this, &MainWindow::refreshSpriteTree);
        connect(m_layoutOrchestrator, &LayoutOrchestrator::uiUpdateNeeded,
                this, &MainWindow::updateUiState);
        connect(m_layoutOrchestrator, &LayoutOrchestrator::mainContentViewUpdateNeeded,
                this, &MainWindow::updateMainContentView);
        connect(m_layoutOrchestrator, &LayoutOrchestrator::openSourceFolderActionUpdateNeeded,
                this, &MainWindow::updateOpenSourceFolderAction);
        connect(m_layoutOrchestrator, &LayoutOrchestrator::cliDiagnosticsUpdateNeeded,
                this, &MainWindow::updateCliDiagnostics);
        connect(m_layoutOrchestrator, &LayoutOrchestrator::activeFrameListUpdateNeeded,
                this, &MainWindow::populateActiveFrameListFromModel);
        connect(m_layoutOrchestrator, &LayoutOrchestrator::initSourceFolderWatcherNeeded,
                this, &MainWindow::initializeSourceFolderWatcher);
        connect(m_layoutOrchestrator, &LayoutOrchestrator::manualFrameLabelUpdateNeeded,
                this, &MainWindow::updateManualFrameLabel);
        connect(m_layoutOrchestrator, &LayoutOrchestrator::pendingProjectPayloadReady,
                this, &MainWindow::applyProjectPayload);
        connect(m_layoutOrchestrator, &LayoutOrchestrator::selectSpritesByPathsRequested,
                this, [this](const QStringList& paths, const QString& primary) {
                    if (m_canvas) m_canvas->selectSpritesByPaths(paths, primary);
                });
        connect(m_layoutOrchestrator, &LayoutOrchestrator::tempDirsCleanupNeeded,
                this, [this]() { if (m_projectController) m_projectController->clearTempDirs(); });
        connect(m_layoutOrchestrator, &LayoutOrchestrator::atlasDimsUpdated,
                this, [this](const QString& text) {
                    if (m_atlasDimsLabel) {
                        m_atlasDimsLabel->setText(text);
                        m_atlasDimsLabel->setVisible(true);
                    }
                });
        connect(m_layoutOrchestrator, &LayoutOrchestrator::profileFallbackRequested,
                this, [this](const QString& profile) {
                    if (!m_profileCombo) return;
                    const QSignalBlocker blocker(m_profileCombo);
                    m_profileCombo->setCurrentIndex(m_profileCombo->findData(profile));
                    m_currentProfile = profile;
                    if (m_layoutOrchestrator) m_layoutOrchestrator->setCurrentProfile(profile);
                });
        connect(m_layoutOrchestrator, &LayoutOrchestrator::cliReadyCheckNeeded,
                this, &MainWindow::checkCliTools);
    }

    m_folderWatcher = new SourceFolderWatcher(this);
    connect(m_folderWatcher, &SourceFolderWatcher::filesAdded,
            this, &MainWindow::onFolderWatcherFilesAdded);
    connect(m_folderWatcher, &SourceFolderWatcher::filesRemoved,
            this, &MainWindow::onFolderWatcherFilesRemoved);
    connect(m_folderWatcher, &SourceFolderWatcher::filesModified,
            this, &MainWindow::onFolderWatcherFilesModified);

#ifndef SPRAT_EMBEDDED_CLI
    m_process = new QProcess(this);
#endif
#ifndef SPRAT_EMBEDDED_CLI
    connect(m_process, &QProcess::finished, this, &MainWindow::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &MainWindow::onProcessError);
#endif

    // Create CliSetupController
    m_cliSetup = new CliSetupController(this, this);
    connect(m_cliSetup, &CliSetupController::statusMessageChanged,
            this, [this](const QString& msg) { if (m_statusLabel) m_statusLabel->setText(msg); });
    connect(m_cliSetup, &CliSetupController::cliReady, this, [this]() {
        m_cliReady = true;
        if (m_layoutOrchestrator) m_layoutOrchestrator->setCLIReady(true);
        updateUiState();
        scheduleLayoutRebuild(true);
    });
    connect(m_cliSetup, &CliSetupController::cliFailed, this, [this]() {
        m_cliReady = false;
        if (m_layoutOrchestrator) m_layoutOrchestrator->setCLIReady(false);
        updateUiState();
    });
    connect(m_cliSetup, &CliSetupController::binaryPathsResolved, this, [this](const CliPaths& paths) {
        m_spratLayoutBin  = paths.layoutBinary;
        m_spratPackBin    = paths.packBinary;
        m_spratConvertBin = paths.convertBinary;
        m_spratFramesBin  = paths.framesBinary;
        m_spratUnpackBin  = paths.unpackBinary;
        if (m_layoutOrchestrator) m_layoutOrchestrator->updateLayoutBinary(paths.layoutBinary);
        if (m_projectController) {
            m_projectController->setFramesBinary(paths.framesBinary);
            m_projectController->setConvertBinary(paths.convertBinary);
            m_projectController->setUnpackBinary(paths.unpackBinary);
        }
    });
    connect(m_cliSetup, &CliSetupController::installOverlayShowNeeded,
            this, &MainWindow::showCliInstallOverlay);
    connect(m_cliSetup, &CliSetupController::installOverlayHideNeeded,
            this, &MainWindow::hideCliInstallOverlay);
    connect(m_cliSetup, &CliSetupController::installProgress,
            this, &MainWindow::onDownloadProgress);
    connect(m_cliSetup, &CliSetupController::installLog,
            this, &MainWindow::onCliInstallLog);
    connect(m_cliSetup, &CliSetupController::installFinished,
            this, [this](bool success) {
                // overlay hide is handled by installOverlayHideNeeded signal
                Q_UNUSED(success)
            });

    QTimer::singleShot(AppConstants::kCliStartupDelayMs, this, &MainWindow::checkCliTools);
    m_animTimer = new QTimer(this);
    connect(m_animTimer, &QTimer::timeout, this, &MainWindow::onAnimTimerTimeout);
    // Autosave setup
    m_autosaveTimer = new QTimer(this);
    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::onAutosaveTimer);

#ifndef Q_OS_WASM
    // One-time Choose default projects folder on very first launch
    // We consider it first launch if the setting is explicitly empty in the config file,
    // even if loadAppSettings() provides a runtime fallback.
    QSettings settings(CliToolsConfig::configPath(), QSettings::IniFormat);
    if (!settings.contains("settings/default_projects_folder")) {
        QTimer::singleShot(1000, this, [this]() {
            QString defaultPath = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("Sprat Projects");
            MessageDialog::information(this, tr("Welcome to Sprat"),
                tr("Please choose a default folder for your projects.\n\n"
                   "Sprat works best when projects have a permanent home on your disk."));

            QString dir = QFileDialog::getExistingDirectory(this, tr("Choose Default Projects Folder"), defaultPath);
            if (!dir.isEmpty()) {
                m_settings.defaultProjectsFolder = dir;
                CliToolsConfig::saveAppSettings(m_settings, m_cliPaths);
            } else {
                // Use default if cancelled
                m_settings.defaultProjectsFolder = defaultPath;
                QDir().mkpath(defaultPath);
                CliToolsConfig::saveAppSettings(m_settings, m_cliPaths);
            }
        });
    }
#endif

    m_autosaveTimer->start(AppConstants::kAutosaveIntervalMs);

    // Create ProjectController and wire its signals to MainWindow processing slots
    m_projectController = new ProjectController(m_session, this);
    connect(m_projectController, &ProjectController::projectLoadFinished,
            this, &MainWindow::onProjectLoadFinished);
    connect(m_projectController, &ProjectController::zipDiscoveryFinished,
            this, &MainWindow::onZipDiscoveryFinished);
    connect(m_projectController, &ProjectController::frameDetectionFinished,
            this, &MainWindow::onFrameDetectionFinished);
    connect(m_projectController, &ProjectController::tarExtractionFinished,
            this, &MainWindow::onTarExtractionFinished);
    connect(m_projectController, &ProjectController::frameExtractionFinished,
            this, &MainWindow::onFrameExtractionFinished);
    connect(m_projectController, &ProjectController::runLayoutQuietNeeded,
            this, [this]() { onRunLayout(true); });

    connect(&m_exportWatcher, &QFutureWatcherBase::finished, this, &MainWindow::onExportFinished);

    m_isRestoringProject = false;
}

#ifdef Q_OS_WASM
MainWindow* MainWindow::wasmInstance() {
    return s_wasmInstance;
}

void MainWindow::onWasmFilePicked(const QString& path, int mode) {
    if (path.isEmpty()) {
        return;
    }
#ifdef Q_OS_WASM
    if (jsIsAsyncBusy()) {
        QTimer::singleShot(100, [path, mode]() {
            MainWindow::wasmInstance()->onWasmFilePicked(path, mode);
        });
        return;
    }
#endif
    bool isFolder = (mode == 1);
    qInfo() << "[WASM] onWasmFilePicked path=" << path << "isFolder=" << isFolder;
    if (isFolder) {
        if (m_animCanvas) {
            m_animCanvas->setZoomManual(false);
        }
        loadFolder(path);
    } else {
        const QUrl url(path);
        if (url.isValid() && !url.scheme().isEmpty() && !url.isLocalFile()) {
            tryHandleRemoteUrl(url, DropAction::Replace);
            return;
        }
        if (!isSupportedDropPath(path)) {
            MessageDialog::information(this, tr("Unsupported File"),
                                      tr("This file type is not supported."));
            return;
        }
        DropAction action = confirmDropAction(path);
        tryHandleDroppedPath(path, action);
    }
}
#endif

/**
 * @brief Destroy the Main Window:: Main Window object
 * 
 */
void MainWindow::closeEvent(QCloseEvent* event) {
    if (isWindowModified() && m_undoStack && !m_undoStack->isClean()) {
        const auto choice = MessageDialog::customQuestion(
            this, tr("Unsaved Changes"),
            tr("You have unsaved changes. Save before closing?"),
            {tr("Save"), tr("Don't Save"), tr("Cancel")},
            0,
            tr("Unsaved Changes"),
            {QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton),
             QApplication::style()->standardIcon(QStyle::SP_DialogDiscardButton),
             QApplication::style()->standardIcon(QStyle::SP_DialogCancelButton)});
        if (choice == 2) {          // Cancel
            event->ignore();
            return;
        }
        if (choice == 0) {          // Save
            onSaveClicked();
        }
    }

    // Clean up temporary folders before closing
    if (m_projectController) {
        m_projectController->clearTempDirs();
    }

    // Allow the close event to proceed
    QMainWindow::closeEvent(event);
}

void MainWindow::updateFolderLabel(const QString& folder) {
    if (!m_folderLabel) return;
    if (folder.isEmpty()) {
        m_folderLabel->setText(tr("Folder: none"));
        return;
    }
    QString text = tr("Folder: ") + folder;
    if (m_settings.syncMode == SyncMode::Watch)
        text += tr(" (watching)");
    m_folderLabel->setText(text);
}

MainWindow::~MainWindow() {
    m_isCanceled = true;

    // Ensure all background tasks are stopped/finished before we destroy members
    if (m_projectController) {
        m_projectController->cancelAll();
    }
    m_exportWatcher.waitForFinished();

    if (m_autosaveTimer) {
        m_autosaveTimer->stop();
        delete m_autosaveTimer;
    }

    if (m_session && !m_session->frameListPath.isEmpty()) {
        QFile::remove(m_session->frameListPath);
        m_session->frameListPath.clear();
    }

    // Explicitly clean up temporary folders (from ZIP extraction, etc.)
    // when the app is closing (as a safety measure in case closeEvent wasn't called)
    if (m_session) {
        m_projectController->clearTempDirs();
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    // In WASM, directly calling the base class or doing work here can cause
    // a crash if a resize event arrives while the app is suspended.
    // Instead, we do NOTHING but start a debounced timer. The timer's
    // callback will do the actual work when it's safe.
    m_pendingResizeSize = event->size();
    m_pendingResizeOldSize = event->oldSize();

    if (!m_resizeDebounceTimer) {
        m_resizeDebounceTimer = new QTimer(this);
        m_resizeDebounceTimer->setSingleShot(true);
        m_resizeDebounceTimer->setInterval(AppConstants::kResizeDebounceMs);
        connect(m_resizeDebounceTimer, &QTimer::timeout, this, [this]() {
#ifdef Q_OS_WASM
            if (jsIsAsyncBusy() || jsHaveJspi()) { // On JSPI, we must be extra careful, though jsIsAsyncBusy should handle it
                m_resizeDebounceTimer->start(); // Try again if busy
                return;
            }
#endif
            QResizeEvent dummyEvent(m_pendingResizeSize, m_pendingResizeOldSize);
            QMainWindow::resizeEvent(&dummyEvent);
            updateCliOverlayGeometry();
            handleAnimPreviewResize();
            // Update canvas overlay if visible
            if (m_canvasOverlay && m_canvasOverlay->isVisible() && m_canvas) {
                m_canvasOverlay->resize(m_canvas->size());
            }
        });
    }
    m_resizeDebounceTimer->start();
}
/**
 * @brief Parses the output from spratlayout into a LayoutModel.
 */
LayoutModel MainWindow::parseLayoutOutput(const QString& output, const QString& folderPath) {
    QVector<LayoutModel> models = LayoutParser::parse(output, folderPath, m_session->currentFolder);
    return models.isEmpty() ? LayoutModel() : models.first();
}

QString MainWindow::layoutParserFolder() const {
    if (m_session->layoutSourceIsList && !m_session->layoutSourcePath.isEmpty()) {
        return QFileInfo(m_session->layoutSourcePath).dir().absolutePath();
    }
    return m_session->layoutSourcePath;
}

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
        m_folderLabel->setText(tr("Folder: none"));
    } else {
        m_folderLabel->setText(QString(tr("Frames: %1 (manual selection)")).arg(m_session->activeFramePaths.size()));
    }
}

// ---------------------------------------------------------------------------
// Helper: Check if a timeline name already exists
// ---------------------------------------------------------------------------
bool MainWindow::hasDuplicateTimelineName(const QString& timelineName) const {
    if (!m_session) return false;
    for (const auto& timeline : m_session->activeAtlas().timelines) {
        if (timeline.name == timelineName) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Helper: Get a unique timeline name with optional path prefix
// ---------------------------------------------------------------------------
QString MainWindow::getUniqueTimelineName(const QString& baseName, const QString& folderPath) {
    QString fullName = baseName;
    if (!folderPath.isEmpty()) {
        fullName = folderPath + "/" + baseName;
    }

    // If no collision, return as-is
    if (!hasDuplicateTimelineName(fullName)) {
        return fullName;
    }

    // If collision exists, try appending numbers
    for (int i = 1; i <= 1000; ++i) {
        QString candidateName = fullName + "_" + QString::number(i);
        if (!hasDuplicateTimelineName(candidateName)) {
            return candidateName;
        }
    }

    // Fallback (shouldn't happen)
    return fullName + "_unique";
}

QVector<SpratProfile> MainWindow::configuredProfiles() {
    QString error;
    const QVector<SpratProfile> profiles = SpratProfilesConfig::loadProfileDefinitions(&error);
    if (!error.isEmpty()) {
        m_statusLabel->setText(tr("Invalid profiles configuration"));
        MessageDialog::warning(this, tr("Profiles"), tr("Could not load profiles configuration:\n%1").arg(error));
    }
    return profiles;
}

bool MainWindow::selectedProfileDefinition(SpratProfile& out) const {
    if (!m_profileCombo) {
        return false;
    }
    const QString selectedName = m_profileCombo->currentData().toString().trimmed();
    if (selectedName.isEmpty()) {
        return false;
    }
    const QVector<SpratProfile> profiles = SpratProfilesConfig::loadProfileDefinitions();
    for (const SpratProfile& profile : profiles) {
        if (profile.name.trimmed() == selectedName) {
            out = profile;
            return true;
        }
    }
    return false;
}

void MainWindow::applyConfiguredProfiles(const QVector<SpratProfile>& profiles, const QString& preferred) {
    if (!m_profileCombo) {
        return;
    }

    QStringList effectiveNames;
    for (const SpratProfile& profile : profiles) {
        const QString trimmed = profile.name.trimmed();
        if (trimmed.isEmpty() || effectiveNames.contains(trimmed)) {
            continue;
        }
        effectiveNames.append(trimmed);
    }
    const QString previousSelected = m_profileCombo->currentData().toString().trimmed();
    m_profileCombo->blockSignals(true);
    m_profileCombo->clear();
    for (const SpratProfile& profile : profiles) {
        const QString name = profile.name.trimmed();
        if (name.isEmpty() || m_profileCombo->findData(name) >= 0) {
            continue;
        }
        const QString display = profile.label.trimmed().isEmpty() ? name : profile.label.trimmed();
        m_profileCombo->addItem(display, name);
    }
    if (!effectiveNames.isEmpty()) {
        QString selected = preferred.trimmed();
        if (selected.isEmpty()) {
            selected = previousSelected;
        }
        if (selected.isEmpty() || !effectiveNames.contains(selected)) {
            selected = effectiveNames.first();
        }
        const int idx = m_profileCombo->findData(selected);
        m_profileCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    m_profileCombo->blockSignals(false);

    if (m_profileSelectorStack) {
        m_profileSelectorStack->setCurrentIndex(effectiveNames.isEmpty() ? 1 : 0);
    }
    m_currentProfile = m_profileCombo->currentData().toString();

    if (!effectiveNames.contains(m_session->lastSuccessfulProfile)) {
        m_session->lastSuccessfulProfile.clear();
    }

    QStringList profileNames, profileLabels;
    for (const SpratProfile& p : profiles) {
        const QString name = p.name.trimmed();
        if (!name.isEmpty() && !profileNames.contains(name)) {
            profileNames  << name;
            profileLabels << (p.label.trimmed().isEmpty() ? name : p.label.trimmed());
        }
    }

    if (m_atlasesManagementWorkspace) {
        const QStringList prevEnabled = m_atlasesManagementWorkspace->enabledProfiles();
        m_atlasesManagementWorkspace->setProfiles(profileNames, profileLabels, m_currentProfile, prevEnabled);
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
        MessageDialog::warning(this, tr("Add Frames"), tr("Could not create temporary frame list."));
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
        MessageDialog::warning(this, tr("Add Frames"), tr("Could not create temporary frame list."));
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
    if (m_activeWorkspace == Workspace::AtlasesManagement
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
                if (m_canvas) m_canvas->setModels(src.layoutModels);
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

    // Persist exclusion so sprites appear in the "Excluded" section.
    for (const QString& p : targets)
        addToExcludedFiles(p);
    syncExcludedAtlas();
    if (m_atlasesManagementWorkspace && m_atlasesManagementWorkspaceActive)
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
        if (m_session->activeFramePaths.isEmpty()) {
            m_session->layoutSourcePath.clear();
            m_session->layoutSourceIsList = false;
            if (!m_session->frameListPath.isEmpty()) {
                QFile::remove(m_session->frameListPath);
                m_session->frameListPath.clear();
            }
            m_session->activeAtlas().layoutModels.clear();
            if (m_canvas) m_canvas->clearCanvas();
            m_session->selectedSprites.clear();
            m_session->selectedSprite.reset();
            m_statusLabel->setText(tr("No frames loaded"));
            m_folderLabel->setText(tr("Folder: none"));
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
            if (m_canvas) m_canvas->removeSprites(targets);
            m_statusLabel->setText(QString(tr("Removed %1 frame(s)")).arg(targets.size()));
            refreshTimelineFrames();
            refreshTimelineList();
            refreshAnimationTest();
            scheduleLayoutRebuild(true, true);
        }
    };

    auto postExecuteUndo = [this]() {
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
    int r = sprite->trimmed ? sprite->trimRect.width()  : 0;
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

/**
 * @brief Opens the settings dialog.
 */
void MainWindow::onSettingsClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::FramesEditor);
}

void MainWindow::onSettingsSpritesheetClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::Spritesheet);
}

void MainWindow::onSettingsFramesEditorClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::FramesEditor);
}

void MainWindow::onSettingsAtlasLayoutClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::AtlasLayout);
}

void MainWindow::onSettingsExportationClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::Exportation);
}

#ifndef Q_OS_WASM
void MainWindow::onSettingsCliToolsClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::CliTools);
}
#endif

void MainWindow::onManageProfiles() {
    ProfilesDialog dialog(configuredProfiles(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QVector<SpratProfile> profiles = dialog.profiles();
    if (!SpratProfilesConfig::saveProfileDefinitions(profiles)) {
        MessageDialog::warning(this, tr("Profiles"), tr("Could not save profiles configuration."));
        return;
    }

    const QString previousProfile = m_profileCombo ? m_profileCombo->currentData().toString() : QString();
    applyConfiguredProfiles(profiles, previousProfile);

    // Invalidate cached layout so changed profile settings take effect
    if (m_session) {
        m_session->cachedLayoutOutput.clear();
        m_session->lastSuccessfulProfile.clear();
    }

    if (m_exportWorkspaceActive && m_exportWorkspace) {
        // Re-populate the export workspace profile combo with the updated definitions
        const QStringList enabled = m_atlasesManagementWorkspace
            ? m_atlasesManagementWorkspace->enabledProfiles() : QStringList();
        QVector<SpratProfile> toExport;
        for (const SpratProfile& p : profiles) {
            if (enabled.isEmpty() || enabled.contains(p.name.trimmed()))
                toExport << p;
        }
        m_exportWorkspace->populate(toExport,
                                    m_profileCombo ? m_profileCombo->currentData().toString() : QString(),
                                    m_lastSaveConfig,
                                    QString());
        // Refresh the preview with the updated profile settings
        const SaveConfig cfg = m_exportWorkspace->getConfig();
        schedulePreviewPack(cfg.profiles.isEmpty() ? QString() : cfg.profiles.first(),
                            cfg.scaleFilter);
    } else if (!m_session->layoutSourcePath.isEmpty()
               && m_profileCombo
               && !m_profileCombo->currentData().toString().trimmed().isEmpty()) {
        // Rebuild the atlas layout with the new profile settings
        scheduleLayoutRebuild(true);
    }
}


void MainWindow::updateOnionSkinDisplay() {
    if (!m_previewView || !m_session) return;
    if (m_settings.onionSkinOpacity == 0) { m_previewView->setGhostSprites({}); return; }
    const QPoint activePivot = m_session->selectedSprite
        ? QPoint(m_session->selectedSprite->pivotX, m_session->selectedSprite->pivotY)
        : QPoint();
    QList<SpritePtr> ghosts;
    for (const auto& s : m_session->selectedSprites)
        if (s && s != m_session->selectedSprite) ghosts.append(s);
    m_previewView->setGhostSprites(ghosts, activePivot);
}

/**
 * @brief Applies application settings.
 */
void MainWindow::applySettings() {
    if (m_canvas) {
        SettingsCoordinator::apply(m_settings, m_canvas, m_previewView, m_animCanvas);
    }
    if (m_packedAtlasView) {
        m_packedAtlasView->setSettings(m_settings);
    }
    if (m_exportLayoutCanvas) {
        m_exportLayoutCanvas->setSettings(m_settings);
    }

    // Update source folder watcher based on sync mode
    if (m_settings.syncMode == SyncMode::None) {
        cleanupSourceFolderWatcher();
    } else {
        // For all non-None modes (Manual and Watch), call initializeSourceFolderWatcher()
        // which handles the mode internally via its own if branches
        initializeSourceFolderWatcher();

        // Stop polling timer — QFileSystemWatcher handles changes event-driven
        if (m_watchModePeriodicCheckTimer) {
            m_watchModePeriodicCheckTimer->stop();
        }
    }

    // If sync was just enabled (None -> active) and a layout already exists,
    // re-run layout to copy images into the sprites folder.
    if (m_settings.syncMode != SyncMode::None
        && m_appliedSyncMode == SyncMode::None
        && m_session
        && !m_session->activeAtlas().layoutModels.isEmpty()
        && m_canvas) {
        // Sync mode enabled - rebuild immediately
        scheduleLayoutRebuild(true);
    }
    m_appliedSyncMode = m_settings.syncMode;

    // Propagate settings changes to LayoutOrchestrator
    if (m_layoutOrchestrator) {
        m_layoutOrchestrator->setSyncMode(m_settings.syncMode);
        m_layoutOrchestrator->setDeduplicateMode(m_settings.deduplicateMode);
        m_layoutOrchestrator->setLayoutZoomOnChange(m_settings.layoutZoomOnChange);
    }

    // Refresh folder label to add/remove the "(watching)" suffix
    if (m_session && !m_session->currentFolder.isEmpty())
        updateFolderLabel(m_session->currentFolder);

    updateOpenSourceFolderAction();
    updateOnionSkinDisplay();

    // Refresh multi-selection label: message only applies when propagation is on
    if (m_multiSelectionLabel && m_session) {
        const int n = m_session->selectedSprites.size();
        if (n > 1 && m_settings.propagateEditsToChecked) {
            m_multiSelectionLabel->setText(
                tr("%1 sprites selected — pivot and marker changes apply to all").arg(n));
            m_multiSelectionLabel->setVisible(true);
        } else {
            m_multiSelectionLabel->setVisible(false);
        }
    }
}

void MainWindow::appendCliLog(const QString& text) {
    if (m_cliLog) {
        m_cliLog->appendPlainText(text);
    }
}

bool MainWindow::runTool(const QString& tool, const QStringList& args, const QByteArray* input, QByteArray* output, QByteArray* error) {
#ifdef Q_OS_WASM
    if (jsIsAsyncBusy()) {
        qWarning() << "[WASM] runTool called while Asyncify is busy! Tool:" << tool;
        return false;
    }
#endif
    QMutexLocker locker(&m_toolMutex);
    QElapsedTimer timer;
    timer.start();

    QString toolName = QFileInfo(tool).fileName();
#ifdef SPRAT_EMBEDDED_CLI
    // Convert tool path to command name
    QString command = QFileInfo(tool).baseName();
    CliResult result = EmbeddedCli::run(command, args, input ? *input : QByteArray());

    if (output) *output = result.stdOut;
    if (error) *error = result.stdErr;

    int exitCode = result.exitCode;
    QString stderrStr = QString::fromUtf8(result.stdErr).trimmed();
    bool ok = exitCode == 0;
#else
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(tool, args);
    if (input && !input->isEmpty()) {
        process.write(*input);
        process.closeWriteChannel();
    }
    if (!process.waitForFinished(-1)) {
        qint64 ms = timer.elapsed();
        QString logEntry = QStringLiteral("[%1] %2 %3\n[%1] Failed to finish (%4 ms)")
            .arg(QTime::currentTime().toString("HH:mm:ss"), toolName, args.join(' '), QString::number(ms));
        QMetaObject::invokeMethod(this, [this, logEntry]() { appendCliLog(logEntry); }, Qt::QueuedConnection);
        return false;
    }
    QByteArray stdoutData = process.readAllStandardOutput();
    QByteArray stderrData = process.readAllStandardError();
    if (output) *output = stdoutData;
    if (error) *error = stderrData;
    int exitCode = process.exitCode();
    QString stderrStr = QString::fromUtf8(stderrData).trimmed();
    bool ok = process.exitStatus() == QProcess::NormalExit && exitCode == 0;
#endif

    qint64 ms = timer.elapsed();
    QString logEntry = QStringLiteral("[%1] %2 %3\n[%1] Exit: %4 (%5 ms)")
        .arg(QTime::currentTime().toString("HH:mm:ss"), toolName, args.join(' '),
             QString::number(exitCode), QString::number(ms));
    if (!stderrStr.isEmpty()) {
        logEntry += QStringLiteral("\n  stderr: %1").arg(stderrStr);
    }
    QMetaObject::invokeMethod(this, [this, logEntry]() { appendCliLog(logEntry); }, Qt::QueuedConnection);

    return ok;
}

// ===== Source Folder Sync Slots =====

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

    // Detect changes — use smart-folder-aware detection when smart folders are configured
    FolderSyncService::SyncResult syncResult;
    if (!m_session->smartFolders.isEmpty()) {
        syncResult = FolderSyncService::detectChangesFromSmartFolders(
            m_session->smartFolders,
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
            if (m_canvas) {
                m_canvas->update();
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
            if (m_canvas) {
                m_canvas->update();
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
        if (!m_session->frameListPath.isEmpty()) {
            QFile::remove(m_session->frameListPath);
            m_session->frameListPath.clear();
        }
        m_session->currentFolder.clear();
        m_session->activeAtlas().layoutModels.clear();
        if (m_canvas) m_canvas->clearCanvas();
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
        if (m_canvas) m_canvas->removeSprites(targets);
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

void MainWindow::clearSourceFolderImages(const QString& excludePath) {
    if (m_session->sourceFolder.isEmpty()) return;
    const QString canonicalSource = QDir(m_session->sourceFolder).absolutePath();
    if (!excludePath.isEmpty() &&
        QDir(excludePath).absolutePath() == canonicalSource) {
        return; // Don't erase the folder we're about to load from
    }
    // Remove all contents (including subdirectories) and recreate the empty folder
    QDir dir(canonicalSource);
    dir.removeRecursively();
    QDir().mkpath(canonicalSource);
}

void MainWindow::showSyncNotification(const QString& message) {
    if (m_statusLabel) {
        m_statusLabel->setText(message);
    }
    qInfo() << "Sync:" << message;
}

void MainWindow::onUndo() {
    if (!m_undoStack) return;
    clearCoordinateFieldOverride();
    m_undoStack->undo();
    syncPivotSpinsFromSprite();
}

void MainWindow::onRedo() {
    if (!m_undoStack) return;
    clearCoordinateFieldOverride();
    m_undoStack->redo();
    syncPivotSpinsFromSprite();
}

void MainWindow::onQuickStart() {
    QString content = tr(
        "<h2>Quick Start Guide</h2>"

        "<h3>Workflow Overview</h3>"
        "<ol>"
        "<li><b>Load sprites</b> &mdash; drag a folder, archive, or image onto the window.</li>"
        "<li><b>Sprites workspace</b> &mdash; select sprites to edit pivots and markers.</li>"
        "<li><b>Atlas Layout workspace</b> &mdash; choose profiles and review the packed layout.</li>"
        "<li><b>Atlases workspace</b> &mdash; organize sprites across named atlases.</li>"
        "<li><b>Frame Animation workspace</b> &mdash; build timelines and preview animations.</li>"
        "<li><b>Save</b> &mdash; Ctrl+S saves the project state to <tt>.json</tt> or <tt>.zip</tt>.</li>"
        "<li><b>Exportation workspace</b> &mdash; preview the packed atlas and export to disk.</li>"
        "</ol>"

        "<h3>1 &mdash; Loading Sprites</h3>"
        "<p>Drag and drop onto the window or use <i>File &rarr; Load Images Folder</i>:</p>"
        "<ul>"
        "<li><b>Folder</b> &mdash; individual sprite images (PNG, JPG, BMP, GIF, WebP, TGA, DDS).</li>"
        "<li><b>Single image</b> &mdash; a sprite sheet; the Frame Detection dialog slices it into frames.</li>"
        "<li><b>Archive</b> &mdash; ZIP or TAR extracted automatically.</li>"
        "<li><b>URL</b> &mdash; <i>File &rarr; Load URL</i> downloads an image, archive, or project.</li>"
        "<li><b>Clipboard</b> &mdash; Ctrl+V imports an image directly.</li>"
        "<li><b>Project file</b> &mdash; reopens a previously saved <tt>.json</tt> with all metadata.</li>"
        "</ul>"
        "<p>If sprites are already loaded you will be asked to <b>Replace</b> or <b>Merge</b>.</p>"
        "<p>In <i>Settings &rarr; Atlas Sprites</i> enable <b>Manual</b> or <b>Watch</b> sync "
        "to keep the project updated when files change on disk.</p>"

        "<h3>2 &mdash; Sprites Workspace</h3>"
        "<p>The default workspace. The atlas canvas (left) shows the packed layout; "
        "the right panel is the sprite editor.</p>"
        "<ul>"
        "<li>Use the <b>Layout / Navigator</b> toggle above the canvas to switch between "
        "the packed atlas view and a folder tree of all sprites.</li>"
        "<li>Click a sprite to select it. The right panel shows a zoomed preview where you can "
        "rename the sprite, drag the <b>pivot point</b>, and add or edit "
        "<b>markers</b> (point, circle, rectangle, polygon).</li>"
        "<li>In the Navigator right-click sprites or folders to delete, group/ungroup, "
        "add frames, or create timelines in one step.</li>"
        "</ul>"

        "<h3>3 &mdash; Atlas Layout Workspace</h3>"
        "<p>Open from the toolbar. Fills the window with the layout canvas and a right panel:</p>"
        "<ul>"
        "<li><b>Search</b> field &mdash; type to filter visible sprites by name.</li>"
        "<li><b>View</b> group &mdash; switch the active named atlas, set source resolution, adjust zoom.</li>"
        "<li><b>Profiles</b> group &mdash; check or uncheck profiles to include them in the layout; "
        "select one to make it active. Use <i>Manage&hellip;</i> to create custom profiles "
        "(atlas size limits, padding, trim, rotation, GPU compression, and more).</li>"
        "<li><b>Pages</b> group &mdash; appears when the active atlas spans multiple texture pages; "
        "click a page to preview it.</li>"
        "</ul>"
        "<p>The layout rebuilds automatically a short time after you stop making changes.</p>"

        "<h3>4 &mdash; Atlases Workspace</h3>"
        "<p>Open <i>Manage Atlases</i> from the toolbar to organize sprites across named atlases:</p>"
        "<ul>"
        "<li>Add or remove atlases with the <b>Add / Remove</b> buttons; double-click to rename.</li>"
        "<li>The sprite tree shows sprites for the selected atlas. "
        "Use <b>Ctrl+Click</b> to toggle a sprite&rsquo;s checkbox or <b>Shift+Click</b> to check a range. "
        "Then <b>drag</b> checked sprites onto an atlas in the left list, or "
        "<b>right-click an atlas</b> and choose <i>Move checked sprites to &ldquo;&hellip;&rdquo;</i>.</li>"
        "<li><b>Right-click a folder/group</b> in the sprite tree:"
        "<ul>"
        "<li><i>Create atlas from &ldquo;&lt;group&gt;&rdquo;</i> &mdash; creates a new atlas named after "
        "the folder and moves all its sprites there.</li>"
        "<li><i>Move &ldquo;&lt;group&gt;&rdquo; to atlas</i> &mdash; if an atlas with that name already "
        "exists, moves the sprites into it instead.</li>"
        "</ul></li>"
        "<li><b>Right-click a source node</b> &rarr; <i>Autocreate atlases</i> &mdash; "
        "creates one atlas per direct subfolder of that source automatically.</li>"
        "<li>Each named atlas is exported independently and can be previewed in the "
        "Exportation workspace.</li>"
        "</ul>"

        "<h3>5 &mdash; Frame Animation Workspace</h3>"
        "<p>Open from the toolbar. The Timelines panel lists all animations:</p>"
        "<ul>"
        "<li>Add a timeline, then drag sprites from the canvas into it. Reorder frames by dragging.</li>"
        "<li>Set the FPS and use play/pause/step controls to preview the animation.</li>"
        "<li>Right-click the preview to export as GIF, MP4, WebM, or other formats "
        "(requires FFmpeg or ImageMagick).</li>"
        "<li>Use <i>Auto-create timelines</i> (right-click in the Navigator) to generate "
        "animations automatically from naming patterns such as <tt>Run_0</tt>, <tt>Run_1</tt>, &hellip;</li>"
        "</ul>"

        "<h3>6 &mdash; Save</h3>"
        "<p><i>File &rarr; Save</i> (Ctrl+S) saves the full project state &mdash; layout options, "
        "sprite names, pivots, markers, timelines &mdash; to a <tt>.json</tt> file or a <tt>.zip</tt> "
        "archive. The app autosaves every 5 minutes and offers to restore on next launch.</p>"

        "<h3>7 &mdash; Exportation Workspace</h3>"
        "<p>Open from the toolbar (or use the quick <i>Export</i> button to re-export to the last "
        "used folder without opening the workspace).</p>"
        "<ul>"
        "<li>The left pane shows a <b>live packed-atlas preview</b>. "
        "Use the <b>Atlas</b> selector to preview individual atlases "
        "(the actual export always processes all).</li>"
        "<li>The right pane lets you choose the <b>output folder</b>, <b>metadata format</b> "
        "(transform), and <b>scale filter</b>.</li>"
        "<li>Click <b>Export</b> to run the full pipeline; click <b>Cancel</b> to return.</li>"
        "</ul>"

        "<p>See <i>Help &rarr; Hotkeys</i> for all keyboard shortcuts.</p>"
    );

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Quick Start"));
    dlg.setMinimumWidth(560);
    dlg.setMinimumHeight(520);

    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    QTextEdit* text = new QTextEdit(&dlg);
    text->setHtml(content);
    text->setReadOnly(true);
    layout->addWidget(text);


    QPushButton* okBtn = new QPushButton(tr("OK"), &dlg);
    layout->addWidget(okBtn);
    connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.exec();
}

void MainWindow::onShowHotkeys() {
    QString content = tr(
        "<h2>Keyboard Shortcuts</h2>"

        "<h3>General</h3>"
        "<table border='1' cellpadding='4' cellspacing='0' width='100%'>"
        "<tr><th width='60%'>Action</th><th>Shortcut</th></tr>"
        "<tr><td>Save Project</td><td><b>Ctrl+S</b></td></tr>"
        "<tr><td>Save Project As</td><td><b>Ctrl+Shift+S</b></td></tr>"
        "<tr><td>Undo Pivot Change</td><td><b>Ctrl+Z</b></td></tr>"
        "<tr><td>Redo Pivot Change</td><td><b>Ctrl+Y</b></td></tr>"
        "<tr><td>Paste / Import from Clipboard</td><td><b>Ctrl+V</b></td></tr>"
        "<tr><td>Quit</td><td><b>Ctrl+Q</b></td></tr>"
        "</table>"

        "<h3>Workspaces</h3>"
        "<table border='1' cellpadding='4' cellspacing='0' width='100%'>"
        "<tr><th width='60%'>Action</th><th>Shortcut</th></tr>"
        "<tr><td>Sprites workspace</td><td><b>Alt+A</b></td></tr>"
        "<tr><td>Frame Animation workspace</td><td><b>Alt+F</b></td></tr>"
        "<tr><td>Atlas Layout workspace</td><td><b>Alt+L</b></td></tr>"
        "<tr><td>Atlases workspace</td><td><b>Alt+M</b></td></tr>"
        "<tr><td>Exportation workspace</td><td><b>Alt+E</b></td></tr>"
        "</table>"

        "<h3>Canvas Views (Layout, Preview, Animation)</h3>"
        "<table border='1' cellpadding='4' cellspacing='0' width='100%'>"
        "<tr><th width='60%'>Action</th><th>Shortcut</th></tr>"
        "<tr><td>Zoom In</td><td><b>Ctrl++</b> or <b>Ctrl+Scroll Up</b></td></tr>"
        "<tr><td>Zoom Out</td><td><b>Ctrl+-</b> or <b>Ctrl+Scroll Down</b></td></tr>"
        "<tr><td>100% Zoom</td><td><b>Ctrl+1</b></td></tr>"
        "<tr><td>Fit to Window</td><td><b>Ctrl+0</b></td></tr>"
        "<tr><td>Pan View</td><td><b>Space+Drag</b> or <b>Middle Mouse Drag</b></td></tr>"
        "</table>"

        "<h3>Layout Canvas</h3>"
        "<table border='1' cellpadding='4' cellspacing='0' width='100%'>"
        "<tr><th width='60%'>Action</th><th>Shortcut</th></tr>"
        "<tr><td>Navigate Sprites</td><td><b>Arrow Keys</b></td></tr>"
        "<tr><td>First / Last Sprite in Row</td><td><b>Home</b> / <b>End</b></td></tr>"
        "<tr><td>Extend Selection</td><td><b>Shift+Arrow Keys</b></td></tr>"
        "<tr><td>Select All Sprites</td><td><b>Ctrl+A</b></td></tr>"
        "<tr><td>Delete Selected Sprites</td><td><b>Delete</b></td></tr>"
        "<tr><td>Quick Split</td><td><b>Alt+Click</b> on a sprite</td></tr>"
        "</table>"
        "<p><i>Sprite search is done via the Search field in the Atlas Layout workspace right panel.</i></p>"

        "<h3>Navigation View</h3>"
        "<table border='1' cellpadding='4' cellspacing='0' width='100%'>"
        "<tr><th width='60%'>Action</th><th>Shortcut</th></tr>"
        "<tr><td>Delete / Exclude Selected Sprites</td><td><b>Delete</b></td></tr>"
        "<tr><td>Toggle Sprite Checkbox</td><td><b>Ctrl+Click</b></td></tr>"
        "<tr><td>Check a Range of Sprites</td><td><b>Shift+Click</b></td></tr>"
        "<tr><td>Context Menu (Atlases workspace)</td><td><b>Right-Click</b> folder/group or source</td></tr>"
        "</table>"

        "<h3>Timeline</h3>"
        "<table border='1' cellpadding='4' cellspacing='0' width='100%'>"
        "<tr><th width='60%'>Action</th><th>Shortcut</th></tr>"
        "<tr><td>Navigate Frames</td><td><b>Arrow Keys</b></td></tr>"
        "<tr><td>Extend Frame Selection</td><td><b>Shift+Arrow Keys</b></td></tr>"
        "<tr><td>Select All Frames</td><td><b>Ctrl+A</b></td></tr>"
        "<tr><td>Delete Selected Frames</td><td><b>Delete</b></td></tr>"
        "</table>"

        "<h3>Sprite Preview</h3>"
        "<table border='1' cellpadding='4' cellspacing='0' width='100%'>"
        "<tr><th width='60%'>Action</th><th>Shortcut</th></tr>"
        "<tr><td>Delete Selected Marker / Vertex</td><td><b>Delete</b></td></tr>"
        "</table>"

        "<h3>Animation Playback</h3>"
        "<table border='1' cellpadding='4' cellspacing='0' width='100%'>"
        "<tr><th width='60%'>Action</th><th>Shortcut</th></tr>"
        "<tr><td>Play / Pause Animation</td><td><b>Space</b></td></tr>"
        "</table>"

        "<h3>Debug</h3>"
        "<table border='1' cellpadding='4' cellspacing='0' width='100%'>"
        "<tr><th width='60%'>Action</th><th>Shortcut</th></tr>"
        "<tr><td>Toggle Debug panel</td><td><b>F12</b></td></tr>"
        "</table>"
    );

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Keyboard Hotkeys"));
    dlg.setMinimumWidth(550);
    dlg.setMinimumHeight(450);

    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    QTextEdit* text = new QTextEdit(&dlg);
    text->setHtml(content);
    text->setReadOnly(true);
    layout->addWidget(text);

    QPushButton* okBtn = new QPushButton(tr("OK"), &dlg);
    layout->addWidget(okBtn);
    connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    dlg.exec();
}

MainWindow::SessionUndoState MainWindow::captureSessionUndoState() const {
    SessionUndoState state;
    state.currentFolder = m_session->currentFolder;
    state.layoutSourcePath = m_session->layoutSourcePath;
    state.layoutSourceIsList = m_session->layoutSourceIsList;
    state.sourceFolder = m_session->sourceFolder;
    state.sources = m_session->sources;
    state.activeFramePaths = m_session->activeFramePaths;
    state.frameListPath = m_session->frameListPath;
    state.atlases = m_session->atlases;
    state.activeAtlasIndex = m_session->activeAtlasIndex;
    state.cachedLayoutOutput = m_session->cachedLayoutOutput;
    state.cachedLayoutScale = m_session->cachedLayoutScale;
    state.lastSuccessfulProfile = m_session->lastSuccessfulProfile;
    state.lastRunUsedTrim = m_session->lastRunUsedTrim;
    state.selectedTimelineIndex = m_session->selectedTimelineIndex;
    state.selectedPointName = m_session->selectedPointName;
    state.selectedSpritePaths.clear();
    for (const auto& s : m_session->selectedSprites) if (s) state.selectedSpritePaths << s->path;
    state.primarySelectedSpritePath = m_session->selectedSprite ? m_session->selectedSprite->path : QString();
    state.sourceFolderIsTemp = m_projectController ? m_projectController->isSourceFolderTemp() : false;
    return state;
}

namespace {
SpritePtr spriteByPath(const QVector<LayoutModel>& models, const QString& path) {
    if (path.isEmpty()) return nullptr;
    for (const auto& model : models) {
        for (const auto& sprite : model.sprites) {
            if (sprite && sprite->path == path) return sprite;
        }
    }
    return nullptr;
}
}

void MainWindow::applySessionUndoState(const SessionUndoState& state) {
    m_session->currentFolder = state.currentFolder;
    m_session->layoutSourcePath = state.layoutSourcePath;
    m_session->layoutSourceIsList = state.layoutSourceIsList;
    m_session->sourceFolder = state.sourceFolder;
    m_session->sources = state.sources;
    m_session->activeFramePaths = state.activeFramePaths;
    m_session->frameListPath = state.frameListPath;
    m_session->atlases = state.atlases;
    m_session->activeAtlasIndex = state.activeAtlasIndex;
    m_session->cachedLayoutOutput = state.cachedLayoutOutput;
    m_session->cachedLayoutScale = state.cachedLayoutScale;
    m_session->lastSuccessfulProfile = state.lastSuccessfulProfile;
    m_session->lastRunUsedTrim = state.lastRunUsedTrim;
    m_session->selectedTimelineIndex = state.selectedTimelineIndex;
    m_session->selectedPointName = state.selectedPointName;
    if (m_projectController) m_projectController->setSourceFolderIsTemp(state.sourceFolderIsTemp);

    // Restore selections
    m_session->selectedSprites.clear();
    for (const QString& p : state.selectedSpritePaths) {
        SpritePtr s = spriteByPath(m_session->activeAtlas().layoutModels, p);
        if (s) m_session->selectedSprites.push_back(s);
    }
    m_session->selectedSprite = spriteByPath(m_session->activeAtlas().layoutModels, state.primarySelectedSpritePath);

    // Refresh UI
    updateMainContentView();
    updateUiState();
    refreshSpriteTree();
    refreshTimelineList();
    refreshTimelineFrames();
    refreshAnimationTest();
    if (m_canvas) m_canvas->update();
    m_previewView->overlay()->updateLayout();
    refreshHandleCombo();
    updateManualFrameLabel();
    updateOpenSourceFolderAction();
}

namespace {
class SessionUndoCommand : public QUndoCommand {
public:
    SessionUndoCommand(MainWindow* mw, const QString& text,
                       const MainWindow::SessionUndoState& before,
                       const MainWindow::SessionUndoState& after,
                       bool alreadyApplied)
        : QUndoCommand(text), m_mw(mw), m_before(before), m_after(after), m_skipFirstRedo(alreadyApplied) {}

    void undo() override { m_mw->applySessionUndoState(m_before); }
    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        m_mw->applySessionUndoState(m_after);
    }

private:
    MainWindow* m_mw;
    MainWindow::SessionUndoState m_before;
    MainWindow::SessionUndoState m_after;
    mutable bool m_skipFirstRedo;
};
}

void MainWindow::pushSessionUndoCommand(const QString& text,
                                        const SessionUndoState& before,
                                        const SessionUndoState& after,
                                        bool alreadyApplied) {
    m_undoStack->push(new SessionUndoCommand(this, text, before, after, alreadyApplied));
}

void MainWindow::beginPendingSessionUndoCommand(const QString& text) {
    m_pendingSessionUndoCommand = { text, captureSessionUndoState() };
}

void MainWindow::finalizePendingSessionUndoCommand() {
    if (m_pendingSessionUndoCommand) {
        pushSessionUndoCommand(m_pendingSessionUndoCommand->text,
                               m_pendingSessionUndoCommand->before,
                               captureSessionUndoState(),
                               true);
        m_pendingSessionUndoCommand.reset();
    }
}

void MainWindow::discardPendingSessionUndoCommand() {
    m_pendingSessionUndoCommand.reset();
}
