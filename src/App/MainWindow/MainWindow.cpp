#include "MainWindow.h"
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
#include "SpratProfilesConfig.h"
#include "FolderSyncService.h"
#include "SpriteNameUtils.h"
#ifdef Q_OS_WASM
#include "WasmFileDialog.h"
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

Q_LOGGING_CATEGORY(mainWindow, "mainWindow")
Q_LOGGING_CATEGORY(cli, "cli")
#ifdef SPRAT_EMBEDDED_CLI
#include "EmbeddedCli.h"
#endif

Q_LOGGING_CATEGORY(project, "project")
Q_LOGGING_CATEGORY(autosave, "autosave")

void MainWindow::openSettingsDialogForSection(SettingsDialog::Section section) {
    SettingsDialog dlg(m_settings, m_cliPaths, this, section);
    QObject::connect(&dlg, &SettingsDialog::installCliToolsRequested, this, &MainWindow::installCliTools);
    QObject::connect(&dlg, &SettingsDialog::syncNowRequested, this, &MainWindow::onSyncNowRequested);
    if (dlg.exec() == QDialog::Accepted) {
        m_settings = dlg.getSettings();
        m_cliPaths = dlg.getCliPaths();
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
    setupUi();
    setupKeyboardShortcuts();

    // Initialize undo stack and connect pivot drag signal
    m_undoStack = new QUndoStack(this);
    m_undoStack->setUndoLimit(50);
    if (m_previewView && m_previewView->overlay()) {
        connect(m_previewView->overlay(), &EditorOverlayItem::pivotDragFinished,
                this, [this](int oldX, int oldY, int newX, int newY) {
            if (!m_session->selectedSprite) return;
            m_undoStack->push(new SetPivotCommand(m_session->selectedSprite,
                                                  oldX, oldY, newX, newY, true));
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

    m_layoutRunner = new LayoutRunner(this);
    m_layoutRunner->setMutex(&m_toolMutex);
    connect(m_layoutRunner, &LayoutRunner::finished, this, &MainWindow::onLayoutFinished);
    connect(m_layoutRunner, &LayoutRunner::errorOccurred, this, &MainWindow::onLayoutError);
    connect(m_layoutRunner, &LayoutRunner::logMessage, this, &MainWindow::appendCliLog);

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
    m_cliToolInstaller = new CliToolInstaller(this);
#ifndef SPRAT_EMBEDDED_CLI
    connect(m_process, &QProcess::finished, this, &MainWindow::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &MainWindow::onProcessError);
#endif
    connect(m_cliToolInstaller, &CliToolInstaller::installFinished, this, &MainWindow::onInstallFinished);
    connect(m_cliToolInstaller, &CliToolInstaller::installStarted, this, &MainWindow::showCliInstallOverlay);
    connect(m_cliToolInstaller, &CliToolInstaller::downloadProgress, this, &MainWindow::onDownloadProgress);
    connect(m_cliToolInstaller, &CliToolInstaller::installLog, this, &MainWindow::onCliInstallLog);
    QTimer::singleShot(1000, this, &MainWindow::checkCliTools);
    m_animTimer = new QTimer(this);
    connect(m_animTimer, &QTimer::timeout, this, &MainWindow::onAnimTimerTimeout);
    
    // Autosave setup
    m_autosaveTimer = new QTimer(this);
    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::onAutosaveTimer);
    m_autosaveTimer->start(300000); // Autosave every 5 minutes

    connect(&m_folderDiscoveryWatcher, &QFutureWatcherBase::finished, this, &MainWindow::onFolderDiscoveryFinished);
    connect(&m_projectLoadWatcher, &QFutureWatcherBase::finished, this, &MainWindow::onProjectLoadFinished);
    connect(&m_zipDiscoveryWatcher, &QFutureWatcherBase::finished, this, &MainWindow::onZipDiscoveryFinished);
    connect(&m_frameDetectionWatcher, &QFutureWatcherBase::finished, this, &MainWindow::onFrameDetectionFinished);
    connect(&m_tarExtractionWatcher, &QFutureWatcherBase::finished, this, &MainWindow::onTarExtractionFinished);
    connect(&m_frameExtractionWatcher, &QFutureWatcherBase::finished, this, &MainWindow::onFrameExtractionFinished);
    connect(&m_projectSaveWatcher, &QFutureWatcherBase::finished, this, &MainWindow::onProjectSaveFinished);

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
            QMessageBox::information(this, tr("Unsupported File"),
                                     tr("This file type is not supported."));
            return;
        }
#ifdef Q_OS_WASM
        // Avoid modal dialogs in WASM; treat drops as Replace.
        tryHandleDroppedPath(path, DropAction::Replace);
#else
        DropAction action = confirmDropAction(path);
        if (action != DropAction::Cancel) {
            tryHandleDroppedPath(path, action);
        }
#endif
    }
}
#endif

/**
 * @brief Destroy the Main Window:: Main Window object
 * 
 */
void MainWindow::closeEvent(QCloseEvent* event) {
    // Clean up temporary folders before closing
    if (m_session) {
        m_session->clearTempDirs();
    }

    // Allow the close event to proceed
    QMainWindow::closeEvent(event);
}

MainWindow::~MainWindow() {
    m_isCanceled = true;

    // Ensure all background tasks are stopped/finished before we destroy members
    m_zipDiscoveryWatcher.waitForFinished();
    m_projectLoadWatcher.waitForFinished();
    m_folderDiscoveryWatcher.waitForFinished();
    m_frameDetectionWatcher.waitForFinished();
    m_tarExtractionWatcher.waitForFinished();
    m_frameExtractionWatcher.waitForFinished();
    m_projectSaveWatcher.waitForFinished();

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
        m_session->clearTempDirs();
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
        m_resizeDebounceTimer->setInterval(200);
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
    QString fileName = QString("sprat-gui-frames-%1.txt").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString newFrameListPath = QDir::temp().filePath(fileName);
    QFile file(newFrameListPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[FrameList] Failed to create:" << newFrameListPath;
        return false;
    }
    QTextStream out(&file);
    for (const QString& path : m_session->activeFramePaths) {
        out << path << "\n";
    }
    out.flush();
    file.close();
    qInfo() << "[FrameList] Created:" << newFrameListPath
            << "exists=" << QFile::exists(newFrameListPath)
            << "frames=" << m_session->activeFramePaths.size();

    const QString oldFrameListPath = m_session->frameListPath;
    m_session->frameListPath = newFrameListPath;
    m_session->layoutSourcePath = m_session->frameListPath;
    m_session->layoutSourceIsList = true;
    if (!m_session->activeFramePaths.isEmpty()) {
        // Find the common parent directory of all frames
        QString commonFolder = QFileInfo(m_session->activeFramePaths.first()).absoluteDir().absolutePath();
        for (int i = 1; i < m_session->activeFramePaths.size(); ++i) {
            const QString path = m_session->activeFramePaths.at(i);
            const QString dir = QFileInfo(path).absoluteDir().absolutePath();
            // Find common ancestor between commonFolder and dir
            while (!commonFolder.isEmpty()
                   && dir != commonFolder
                   && !dir.startsWith(commonFolder + "/")) {
                const QString parent = QFileInfo(commonFolder).absoluteDir().absolutePath();
                if (parent == commonFolder) break; // reached filesystem root
                commonFolder = parent;
            }
        }
        m_session->currentFolder = commonFolder;
    }
    if (!oldFrameListPath.isEmpty() && oldFrameListPath != m_session->frameListPath) {
        qInfo() << "[FrameList] Deleting old:" << oldFrameListPath;
        QFile::remove(oldFrameListPath);
    }
    updateManualFrameLabel();
    return true;
}

void MainWindow::populateActiveFrameListFromModel() {
    m_session->activeFramePaths.clear();
    for (const auto& model : m_session->layoutModels) {
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
    for (const auto& timeline : m_session->timelines) {
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
        QMessageBox::warning(this, tr("Profiles"), tr("Could not load profiles configuration:\n%1").arg(error));
    }
    return profiles;
}

bool MainWindow::selectedProfileDefinition(SpratProfile& out) const {
    if (!m_profileCombo) {
        return false;
    }
    const QString selectedName = m_profileCombo->currentText().trimmed();
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

    QStringList effectiveProfiles;
    for (const SpratProfile& profile : profiles) {
        const QString trimmed = profile.name.trimmed();
        if (trimmed.isEmpty() || effectiveProfiles.contains(trimmed)) {
            continue;
        }
        effectiveProfiles.append(trimmed);
    }
    const QString previousSelected = m_profileCombo->currentText().trimmed();
    m_profileCombo->blockSignals(true);
    m_profileCombo->clear();
    m_profileCombo->addItems(effectiveProfiles);
    if (!effectiveProfiles.isEmpty()) {
        QString selected = preferred.trimmed();
        if (selected.isEmpty()) {
            selected = previousSelected;
        }
        if (selected.isEmpty() || !effectiveProfiles.contains(selected)) {
            selected = effectiveProfiles.first();
        }
        m_profileCombo->setCurrentText(selected);
    }
    m_profileCombo->blockSignals(false);

    if (m_profileSelectorStack) {
        m_profileSelectorStack->setCurrentIndex(effectiveProfiles.isEmpty() ? 1 : 0);
    }

    if (!effectiveProfiles.contains(m_session->lastSuccessfulProfile)) {
        m_session->lastSuccessfulProfile.clear();
    }
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
        QMessageBox::information(this, tr("Add Frames"), tr("All selected frames are already loaded."));
        return;
    }

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

void MainWindow::onRemoveFramesRequested(const QStringList& paths) {
    if (paths.isEmpty()) {
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

    QSet<QString> timelineNames;
    for (const auto& timeline : m_session->timelines) {
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
        if (QMessageBox::warning(this, tr("Remove Frames"), warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
    }

    // Deselect sprite if it's being removed
    if (m_session->selectedSprite && targets.contains(m_session->selectedSprite->path)) {
        m_session->selectedSprite.reset();
    }
    m_session->selectedSprites.erase(
        std::remove_if(m_session->selectedSprites.begin(), m_session->selectedSprites.end(),
            [&targets](const SpritePtr& sprite) { return sprite && targets.contains(sprite->path); }),
        m_session->selectedSprites.end()
    );

    QStringList remainingFramePaths = m_session->activeFramePaths;
    for (const QString& path : targets) {
        remainingFramePaths.removeAll(path);
    }

    if (remainingFramePaths.isEmpty()) {
        bool timelineChanged = false;
        for (auto& timeline : m_session->timelines) {
            for (int i = timeline.frames.size() - 1; i >= 0; --i) {
                if (targets.contains(timeline.frames[i])) {
                    timeline.frames.removeAt(i);
                    timelineChanged = true;
                }
            }
        }
        if (timelineChanged) {
            refreshTimelineFrames();
            refreshAnimationTest();
        }

        // Remove animations that became empty because their sprites were removed from layout
        for (int i = m_session->timelines.size() - 1; i >= 0; --i) {
            if (m_session->timelines[i].frames.isEmpty()) {
                m_session->timelines.removeAt(i);
                if (m_session->selectedTimelineIndex > i) {
                    --m_session->selectedTimelineIndex;
                } else if (m_session->selectedTimelineIndex == i) {
                    m_session->selectedTimelineIndex = -1;
                }
                timelineChanged = true;
            }
        }
        if (timelineChanged) {
            refreshTimelineList();
            refreshTimelineFrames();
            refreshAnimationTest();
        }

        m_session->activeFramePaths.clear();
        m_session->layoutSourcePath.clear();
        m_session->layoutSourceIsList = false;
        if (!m_session->frameListPath.isEmpty()) {
            QFile::remove(m_session->frameListPath);
            m_session->frameListPath.clear();
        }
        m_session->layoutModels.clear();
        if (m_canvas) {
            m_canvas->clearCanvas();
        }
        m_session->selectedSprites.clear();
        m_session->selectedSprite.reset();

        // Delete the image files for removed sprites
        for (const QString& path : targets) {
            QFile(path).remove();
        }
        m_statusLabel->setText(tr("No frames loaded"));
        m_folderLabel->setText(tr("Folder: none"));
        m_session->cachedLayoutOutput.clear();
        m_session->cachedLayoutScale = 1.0;
        updateMainContentView();
        updateUiState();
        return;
    }

    const QStringList previousFramePaths = m_session->activeFramePaths;
    m_session->activeFramePaths = remainingFramePaths;
    if (!ensureFrameListInput()) {
        m_session->activeFramePaths = previousFramePaths;
        QMessageBox::warning(this, tr("Remove Frames"), tr("Could not refresh the frame list after removal."));
        return;
    }

    bool timelineChanged = false;
    for (auto& timeline : m_session->timelines) {
        for (int i = timeline.frames.size() - 1; i >= 0; --i) {
            if (targets.contains(timeline.frames[i])) {
                timeline.frames.removeAt(i);
                timelineChanged = true;
            }
        }
    }
    if (timelineChanged) {
        refreshTimelineFrames();
        refreshAnimationTest();
    }

    // Remove animations that became empty because their sprites were removed from layout
    for (int i = m_session->timelines.size() - 1; i >= 0; --i) {
        if (m_session->timelines[i].frames.isEmpty()) {
            m_session->timelines.removeAt(i);
            if (m_session->selectedTimelineIndex > i) {
                --m_session->selectedTimelineIndex;
            } else if (m_session->selectedTimelineIndex == i) {
                m_session->selectedTimelineIndex = -1;
            }
            timelineChanged = true;
        }
    }
    if (timelineChanged) {
        refreshTimelineList();
        refreshTimelineFrames();
        refreshAnimationTest();
    }

    // Delete the image files for removed sprites
    for (const QString& path : targets) {
        QFile(path).remove();
    }

    // Immediately remove deleted sprites from canvas (leaves a gap until rebuild)
    if (m_canvas) {
        m_canvas->removeSprites(targets);
    }
    // Keep session layout models in sync so the next rebuild sees the correct sprite list
    const QSet<QString> targetSet(targets.begin(), targets.end());
    for (auto& model : m_session->layoutModels) {
        model.sprites.erase(
            std::remove_if(model.sprites.begin(), model.sprites.end(),
                [&targetSet](const SpritePtr& s) { return targetSet.contains(s->path); }),
            model.sprites.end());
    }

    m_statusLabel->setText(QString(tr("Removed %1 frame(s)")).arg(targets.size()));
    scheduleLayoutRebuild();
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
        m_session->activeFramePaths.append(pathA);
        m_session->activeFramePaths.append(pathB);
    }

    ensureFrameListInput();
    scheduleLayoutRebuild();
    m_statusLabel->setText(tr("Sprite split into %1 and %2").arg(
        QFileInfo(pathA).fileName(), QFileInfo(pathB).fileName()));
}

/**
 * @brief Opens the settings dialog.
 */
void MainWindow::onSettingsClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::Styles);
}

void MainWindow::onSettingsStylesClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::Styles);
}

void MainWindow::onSettingsSpritesheetClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::Spritesheet);
}

void MainWindow::onSettingsCliToolsClicked() {
    openSettingsDialogForSection(SettingsDialog::Section::CliTools);
}

void MainWindow::onManageProfiles() {
    ProfilesDialog dialog(configuredProfiles(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QVector<SpratProfile> profiles = dialog.profiles();
    if (!SpratProfilesConfig::saveProfileDefinitions(profiles)) {
        QMessageBox::warning(this, tr("Profiles"), tr("Could not save profiles configuration."));
        return;
    }

    const QString previousProfile = m_profileCombo ? m_profileCombo->currentText() : QString();
    applyConfiguredProfiles(profiles, previousProfile);
    if (!m_session->layoutSourcePath.isEmpty() && m_profileCombo && !m_profileCombo->currentText().trimmed().isEmpty()) {
        scheduleLayoutRebuild();
    }
}


/**
 * @brief Applies application settings.
 */
void MainWindow::applySettings() {
    if (m_canvas) {
        SettingsCoordinator::apply(m_settings, m_canvas, m_previewView, m_animCanvas);
    }

    // Update source folder watcher based on sync mode
    if (m_settings.syncMode == SyncMode::None) {
        cleanupSourceFolderWatcher();
    } else {
        // For all non-None modes (Manual and Watch), call initializeSourceFolderWatcher()
        // which handles the mode internally via its own if branches
        initializeSourceFolderWatcher();

        // Set up periodic check for Watch mode to detect file deletions
        if (m_settings.syncMode == SyncMode::Watch) {
            if (!m_watchModePeriodicCheckTimer) {
                m_watchModePeriodicCheckTimer = new QTimer(this);
                connect(m_watchModePeriodicCheckTimer, &QTimer::timeout,
                        this, &MainWindow::onWatchModePeriodicCheck);
            }
            // Check every 2 seconds for deleted files
            m_watchModePeriodicCheckTimer->start(2000);
        }
    }

    // If sync was just enabled (None -> active) and a layout already exists,
    // re-run layout to copy images into the sprites folder.
    if (m_settings.syncMode != SyncMode::None
        && m_appliedSyncMode == SyncMode::None
        && m_session
        && !m_session->layoutModels.isEmpty()
        && m_canvas) {
        scheduleLayoutRebuild();
    }
    m_appliedSyncMode = m_settings.syncMode;

    updateOpenSourceFolderAction();
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

    qInfo() << "[Watch] Processing modifications...";
    scheduleLayoutRebuild();
}

void MainWindow::onSyncNowRequested() {
    qInfo() << "[SyncNow] Button clicked";
    qInfo() << "[SyncNow] m_session->sourceFolder:" << m_session->sourceFolder;
    qInfo() << "[SyncNow] m_session->sourceFolderIsTemp:" << m_sourceFolderIsTemp;

    if (m_session->sourceFolder.isEmpty()) {
        qWarning() << "[SyncNow] Source folder is empty!";
        QMessageBox::information(this, tr("Sync"), tr("No source folder configured."));
        return;
    }

    qInfo() << "[SyncNow] Calling performManualSync()";
    performManualSync();
}

void MainWindow::performFolderSync() {
    if (m_session->layoutModels.isEmpty()) {
        qWarning() << "Cannot sync: no layout models loaded";
        return;
    }

    // Detect changes
    auto syncResult = FolderSyncService::detectChanges(
        m_session->sourceFolder,
        m_session->layoutModels.first().sprites);

    if (!syncResult.error.isEmpty()) {
        QMessageBox::warning(this, tr("Sync Error"), syncResult.error);
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
            m_session->layoutModels.first(), syncResult, m_session->sourceFolder)) {
        QMessageBox::warning(this, tr("Sync Error"), tr("Failed to merge changes."));
        return;
    }
    ensureUniqueSpriteNames(m_session->layoutModels, m_session->sourceFolder);

    // Update activeFramePaths to match the modified layout
    populateActiveFrameListFromModel();

    // Always regenerate frame list file after changes to keep it in sync
    ensureFrameListInput();

    showSyncNotification(summary);

    // Re-run layout to position new sprites or remove deleted ones
    if (!syncResult.newImagePaths.isEmpty() || !syncResult.deletedImagePaths.isEmpty()) {
        if (!m_session->activeFramePaths.isEmpty()) {
            if (!syncResult.newImagePaths.isEmpty()) {
                m_statusLabel->setText(tr("Re-generating layout with new sprites..."));
            } else {
                m_statusLabel->setText(tr("Re-generating layout..."));
            }
            scheduleLayoutRebuild();
        } else if (!syncResult.deletedImagePaths.isEmpty()) {
            // All frames were deleted
            m_statusLabel->setText(tr("All sprites removed."));
        }
    }
}

void MainWindow::performManualSync() {
    qInfo() << "[Sync] Starting manual sync...";

    if (!m_session) {
        qWarning() << "[Sync] Session is null";
        QMessageBox::warning(this, tr("Sync Error"), tr("No session."));
        return;
    }

    if (m_session->layoutModels.isEmpty()) {
        qWarning() << "[Sync] No layout models loaded";
        QMessageBox::information(this, tr("Sync"), tr("No layout loaded."));
        return;
    }

    if (m_session->sourceFolder.isEmpty()) {
        qWarning() << "[Sync] Source folder is empty";
        QMessageBox::information(this, tr("Sync"), tr("No sprites folder configured."));
        return;
    }

    QDir folderDir(m_session->sourceFolder);
    if (!folderDir.exists()) {
        qWarning() << "[Sync] Folder does not exist:" << m_session->sourceFolder;
        QMessageBox::warning(this, tr("Sync Error"), tr("Sprites folder does not exist."));
        return;
    }

    qInfo() << "[Sync] Using folder:" << m_session->sourceFolder;
    qInfo() << "[Sync] Layout has" << m_session->layoutModels.first().sprites.size() << "sprites";

    LayoutModel& layout = m_session->layoutModels.first();
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
    ensureUniqueSpriteNames(m_session->layoutModels, m_session->sourceFolder);

    // Step 3: Update activeFramePaths and frame list
    populateActiveFrameListFromModel();
    qInfo() << "[Sync]   activeFramePaths now has" << m_session->activeFramePaths.size() << "items";

    ensureFrameListInput();
    qInfo() << "[Sync]   Frame list updated, layoutSourcePath:" << m_session->layoutSourcePath;

    // Show summary
    QString summary = QString(tr("Sync complete: %1 removed, %2 added"))
        .arg(removedCount).arg(addedCount);
    showSyncNotification(summary);

    // Step 4: Refresh layout
    qInfo() << "[Sync] Step 4: Refreshing layout...";
    if (!m_session->activeFramePaths.isEmpty()) {
        qInfo() << "[Sync]   Running layout...";
        m_statusLabel->setText(tr("Regenerating layout..."));
        scheduleLayoutRebuild();
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
    if (!m_session || m_session->layoutModels.isEmpty() || m_session->sourceFolder.isEmpty()) {
        return;
    }

    // Verify sprites folder still exists
    if (!QDir(m_session->sourceFolder).exists()) {
        qWarning() << "[Watch] Sprites folder no longer exists:" << m_session->sourceFolder;
        m_session->sourceFolder.clear();
        cleanupSourceFolderWatcher();
        return;
    }

    LayoutModel& layout = m_session->layoutModels.first();
    bool spriteRemoved = false;

    // Check if any sprites reference files that no longer exist
    for (int i = layout.sprites.size() - 1; i >= 0; --i) {
        const auto& sprite = layout.sprites[i];
        if (sprite && !QFileInfo::exists(sprite->path)) {
            qInfo() << "[Watch] Removing missing sprite:" << sprite->name;
            layout.sprites.removeAt(i);
            spriteRemoved = true;
        }
    }

    // If any sprites were removed, update the UI
    if (spriteRemoved && m_canvas) {
        qInfo() << "[Watch] Periodic check detected removed sprites, updating UI";
        m_canvas->setModelsAsync(m_session->layoutModels, &m_isCanceled, [this]() {
            if (!m_isCanceled) {
                refreshSpriteTree();
                updateUiState();
            }
        });
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

    // NOTE: File watcher disabled due to intermittent crash on initialization
    // The crash is unrelated to split sprites feature
    // File watching will be re-enabled after root cause is identified
    qInfo() << "[Watcher] File watching temporarily disabled (using manual mode)";
    m_settings.syncMode = SyncMode::Manual;
    qInfo() << "[Watcher] Manual mode - folder ready for manual sync:" << m_session->sourceFolder;

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
        m_sourceFolderIsTemp = true;
        // Store temp dir in session separately - it must persist while sprites
        // reference files in it. Not added to tempDirs list which is cleared
        // during layout operations.
        m_session->setSourceFolderTempDir(std::move(tempDir));
    }
}

void MainWindow::onOpenSourceFolderClicked() {
    const QString folder = m_session ? m_session->sourceFolder : QString();
    if (folder.isEmpty()) return;
    QDir().mkpath(folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void MainWindow::updateOpenSourceFolderAction() {
    if (!m_openSourceFolderAction) return;
    const bool enabled = m_session
        && m_settings.syncMode != SyncMode::None
        && !m_session->sourceFolder.isEmpty();
    m_openSourceFolderAction->setEnabled(enabled);
}

bool MainWindow::activeFramesAreInSourceFolder() const {
    if (m_session->sourceFolder.isEmpty() || m_session->activeFramePaths.isEmpty()) {
        return false;
    }
    QString canonicalFolder = QDir(m_session->sourceFolder).absolutePath();
    if (!canonicalFolder.endsWith('/')) canonicalFolder += '/';
    for (const QString& path : m_session->activeFramePaths) {
        const QString absPath = QFileInfo(path).absoluteFilePath();
        if (!absPath.startsWith(canonicalFolder)) {
            return false;
        }
    }
    return true;
}

void MainWindow::copyActiveFramesToSourceFolder(bool overwriteDuplicates) {
    if (m_session->activeFramePaths.isEmpty() || m_session->sourceFolder.isEmpty()) {
        return;
    }
    QDir().mkpath(m_session->sourceFolder);
    QString canonicalFolder = QDir(m_session->sourceFolder).absolutePath();
    if (!canonicalFolder.endsWith('/')) canonicalFolder += '/';

    // Determine the common root of active frames for relative path computation
    const QString originalRoot = m_session->currentFolder;

    QStringList newPaths;
    newPaths.reserve(m_session->activeFramePaths.size());

    for (const QString& path : m_session->activeFramePaths) {
        const QFileInfo srcInfo(path);
        const QString absPath = srcInfo.absoluteFilePath();

        // Already in source folder (including subfolders) — keep as-is.
        if (absPath.startsWith(canonicalFolder)) {
            newPaths.append(path);
            continue;
        }

        // Compute relative path from original root to preserve subfolder structure
        QString relPath;
        if (!originalRoot.isEmpty() && absPath.startsWith(originalRoot)) {
            relPath = QDir(originalRoot).relativeFilePath(absPath);
        } else {
            relPath = srcInfo.fileName();
        }

        QString dst = QDir(m_session->sourceFolder).filePath(relPath);

        // Create intermediate subdirectories
        QDir().mkpath(QFileInfo(dst).absolutePath());

        // Handle existing files
        if (QFileInfo::exists(dst)) {
            if (overwriteDuplicates) {
                // Replace the existing file
                QFile::remove(dst);
            } else {
                // Resolve name conflicts: try baseName_1, baseName_2, ..., baseName_99.
                const QString baseName = QFileInfo(dst).completeBaseName();
                const QString suffix   = QFileInfo(dst).suffix();
                const QString dstDir   = QFileInfo(dst).absolutePath();
                bool resolved = false;
                for (int i = 1; i <= 99; ++i) {
                    const QString candidate = QDir(dstDir).filePath(
                        QString("%1_%2.%3").arg(baseName).arg(i).arg(suffix));
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

        if (!QFile::copy(path, dst)) {
            qWarning() << "copyActiveFramesToSourceFolder: copy failed" << path << "->" << dst;
            newPaths.append(path);
            continue;
        }

        newPaths.append(dst);
    }

    m_session->activeFramePaths = newPaths;
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
    m_undoStack->undo();
    syncPivotSpinsFromSprite();
}

void MainWindow::onRedo() {
    if (!m_undoStack) return;
    m_undoStack->redo();
    syncPivotSpinsFromSprite();
}

void MainWindow::onQuickStart() {
    QString content = tr(
        "<h2>Quick Start Guide</h2>"

        "<h3>Main Workflow</h3>"
        "<ol>"
        "<li><b>Load sprites</b> &mdash; Drag and drop a folder of images onto the window, "
        "or use <i>File &rarr; Load Images Folder</i>. You can also drop a single sprite sheet image "
        "and the app will auto-detect individual frames inside it.</li>"
        "<li><b>Choose a layout profile</b> &mdash; Pick a profile from the dropdown above the atlas "
        "(e.g. <i>fast</i>, <i>balanced</i>). Profiles control atlas size limits, padding, "
        "trim, rotation, and other packing options. You can create custom profiles in "
        "<i>Settings &rarr; Manage Profiles</i>.</li>"
        "<li><b>Wait for the atlas to build</b> &mdash; The layout rebuilds automatically "
        "after you stop making changes. If you remove or add sprites the canvas updates immediately "
        "and a full repack runs in the background.</li>"
        "<li><b>Edit sprites</b> &mdash; Click a sprite in the atlas to select it. "
        "The right-hand panel shows a zoomed preview where you can drag the pivot point, "
        "add markers (points, circles, rectangles, polygons), and rename the sprite.</li>"
        "<li><b>Create animations</b> &mdash; Open the Timelines panel, add a timeline, "
        "then drag sprites from the atlas into it. Adjust the frame order by dragging, "
        "set the FPS, and preview the result in the Animation panel.</li>"
        "<li><b>Save</b> &mdash; <i>File &rarr; Save</i> (Ctrl+S) exports the atlas image, "
        "a metadata file with sprite positions, and the full project state. "
        "You can save to a folder or a ZIP archive.</li>"
        "</ol>"

        "<h3>Loading Sprites</h3>"
        "<p>There are several ways to get images into the project:</p>"
        "<ul>"
        "<li><b>Folder</b> &mdash; A folder of individual sprite images (PNG, JPG, BMP, GIF, WebP, TGA, DDS).</li>"
        "<li><b>Single image</b> &mdash; Drop a sprite sheet and the Frame Detection dialog will "
        "help you slice it into individual frames.</li>"
        "<li><b>Archive</b> &mdash; ZIP or TAR files are extracted automatically.</li>"
        "<li><b>URL</b> &mdash; <i>File &rarr; Load URL</i> downloads an image, archive, or project from the web.</li>"
        "<li><b>Clipboard</b> &mdash; Ctrl+V imports an image from the clipboard.</li>"
        "<li><b>Project file</b> &mdash; Reopen a previously saved <tt>.json</tt> project with all metadata intact.</li>"
        "</ul>"
        "<p>When a project already has sprites loaded, you will be asked whether to <b>Replace</b> "
        "or <b>Merge</b> the new content.</p>"

        "<h3>Sprites Folder &amp; Sync</h3>"
        "<p>Each project can have a <b>sprites folder</b> on disk. "
        "Use <i>File &rarr; Open Sprites Folder</i> to reveal it in your file manager. "
        "In <i>Settings &rarr; Spritesheet</i> you can enable folder synchronization:</p>"
        "<ul>"
        "<li><b>Manual</b> &mdash; Press <i>Sync Now</i> to pick up new, modified, or deleted files.</li>"
        "<li><b>Watch</b> &mdash; The app monitors the folder in real time and updates the layout automatically.</li>"
        "</ul>"
        "<p>This lets you edit sprites in an external image editor and see changes reflected instantly.</p>"

        "<h3>Sprite Navigator</h3>"
        "<p>Switch from <i>Layout</i> to <i>Navigator</i> view above the atlas to see a "
        "tree of all sprites organised by folder. From the Navigator you can:</p>"
        "<ul>"
        "<li>Check sprites and use the right-click menu to delete, group, or ungroup them.</li>"
        "<li>Create a timeline from a group of sprites in one click.</li>"
        "<li>Auto-create timelines from every sub-folder at once.</li>"
        "<li>Add new frames to any folder directly.</li>"
        "</ul>"

        "<h3>Animation Preview</h3>"
        "<p>The Animation panel plays back the currently selected timeline. "
        "Use the play/pause button or scrub frames with the arrow buttons. "
        "Right-click the preview to export the animation as GIF, MP4, WebM, or other formats "
        "(requires FFmpeg or ImageMagick).</p>"

        "<h3>Key Features</h3>"
        "<ul>"
        "<li><b>Layout profiles</b> &mdash; Multiple packing configurations (atlas size, padding, "
        "extrude, trim transparency, rotation, multipack).</li>"
        "<li><b>Frame detection</b> &mdash; Automatically slice a sprite sheet into individual frames.</li>"
        "<li><b>Split mode</b> &mdash; Alt+Click on a sprite to split it in two.</li>"
        "<li><b>Pivot &amp; markers</b> &mdash; Set pivot points and attach named markers "
        "(points, circles, rectangles, polygons) to each sprite.</li>"
        "<li><b>Timelines</b> &mdash; Build animation sequences by ordering frames, "
        "then preview and export them.</li>"
        "<li><b>Folder sync</b> &mdash; Keep the project in sync with a folder on disk "
        "(manual or live watch).</li>"
        "<li><b>Deduplication</b> &mdash; Detect exact or perceptually identical frames "
        "and merge them to save atlas space.</li>"
        "<li><b>Resolution scaling</b> &mdash; Set source and target resolutions for automatic rescaling.</li>"
        "<li><b>Multipack</b> &mdash; Automatically split into multiple atlases when sprites "
        "exceed the maximum dimensions.</li>"
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
        "<tr><td>Undo Pivot Change</td><td><b>Ctrl+Z</b></td></tr>"
        "<tr><td>Redo Pivot Change</td><td><b>Ctrl+Y</b></td></tr>"
        "<tr><td>Paste / Import from Clipboard</td><td><b>Ctrl+V</b></td></tr>"
        "<tr><td>Quit</td><td><b>Ctrl+Q</b></td></tr>"
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
        "<tr><td>Search by Name</td><td>Start typing (printable characters)</td></tr>"
        "<tr><td>Clear Search</td><td><b>Escape</b></td></tr>"
        "<tr><td>Quick Split</td><td><b>Alt+Click</b> on a sprite</td></tr>"
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
