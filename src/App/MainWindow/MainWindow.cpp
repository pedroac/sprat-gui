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

Q_LOGGING_CATEGORY(mainWindow, "mainWindow")
Q_LOGGING_CATEGORY(cli, "cli")
#ifdef SPRAT_EMBEDDED_CLI
#include "EmbeddedCli.h"
#endif

Q_LOGGING_CATEGORY(project, "project")
Q_LOGGING_CATEGORY(autosave, "autosave")

// ---------------------------------------------------------------------------
// Qt message handler — routes qInfo/qWarning/qCritical to the Events tab.
// ---------------------------------------------------------------------------
static QAtomicPointer<MainWindow> s_eventLogTarget{nullptr};
static QtMessageHandler s_prevMessageHandler = nullptr;

static void spratMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    if (s_prevMessageHandler)
        s_prevMessageHandler(type, ctx, msg); // preserve default terminal output

    if (type == QtDebugMsg || type == QtFatalMsg)
        return;

    MainWindow* w = s_eventLogTarget.loadAcquire();
    if (!w) return;

    QString prefix;
    switch (type) {
    case QtWarningMsg:  prefix = QStringLiteral("[W] "); break;
    case QtCriticalMsg: prefix = QStringLiteral("[!] "); break;
    default: break;
    }
    constexpr auto level = MainWindow::LogLevel::Qt;
    const QString entry = QStringLiteral("[%1] %2%3")
        .arg(QTime::currentTime().toString("HH:mm:ss"), prefix, msg);

    QMetaObject::invokeMethod(w, [w, entry, level]() {
        w->appendLog(level, entry);
    }, Qt::QueuedConnection);
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
    s_prevMessageHandler = qInstallMessageHandler(spratMessageHandler);
    s_eventLogTarget.storeRelease(this);

    m_session = new ProjectSession(this);
    m_settings = CliToolsConfig::loadAppSettings();
    m_cliPaths = CliToolsConfig::loadCliPaths();

    // Initialize undo stack before setupUi so actions can reference it
    m_undoStack = new QUndoStack(this);
    m_undoStack->setUndoLimit(AppConstants::kUndoStackLimit);
    connect(m_undoStack, &QUndoStack::cleanChanged,
            this, [this](bool) { updateWindowModified(); });
    connect(m_undoStack, &QUndoStack::indexChanged,
            this, [this](int) { if (m_atlasWorkspace) m_atlasWorkspace->refreshSpriteEditor(); });

    setupUi();
    setupKeyboardShortcuts();
    {
        auto* previewCanvas = m_atlasWorkspace ? m_atlasWorkspace->spriteEditorPanel()->previewCanvas() : nullptr;
        if (previewCanvas && previewCanvas->overlay()) {
            connect(previewCanvas->overlay(), &EditorOverlayItem::pivotDragFinished,
                    this, [this](int oldX, int oldY, int newX, int newY) {
                if (!m_session->selectedSprite) return;
                const CoordUnit unit = m_settings.coordUnit;
                const QSize activeSize = m_atlasWorkspace->spriteCoordinateSpaceSize(m_session->selectedSprite);
                const double relX = (unit == CoordUnit::Percent && activeSize.width() > 0)
                    ? (double(newX) * 100.0 / activeSize.width())
                    : double(newX);
                const double relY = (unit == CoordUnit::Percent && activeSize.height() > 0)
                    ? (double(newY) * 100.0 / activeSize.height())
                    : double(newY);
                QVector<SetPivotCommand::CoTarget> coTargets;
                auto addPivotCoTarget = [&](const SpritePtr& sprite) {
                    const QPair<int, int> oldPos{sprite->pivotX, sprite->pivotY};
                    if (unit == CoordUnit::Percent) {
                        const QSize coSize = m_atlasWorkspace->spriteCoordinateSpaceSize(sprite);
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
                    if (oldPos != newPos) coTargets.append({sprite, oldPos, newPos});
                };
                if (m_settings.propagateEditsToChecked) {
                    for (const auto& sprite : m_session->selectedSprites)
                        if (sprite && sprite != m_session->selectedSprite)
                            addPivotCoTarget(sprite);
                }
                AnimationPreviewService::invalidateBounds();
                updateOnionSkinDisplay();
                m_undoStack->push(new SetPivotCommand(m_session->selectedSprite,
                                                      oldX, oldY, newX, newY, true,
                                                      std::move(coTargets)));
            });
            connect(previewCanvas->overlay(), &EditorOverlayItem::markerDragFinished,
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
                        auto* pv = m_atlasWorkspace ? m_atlasWorkspace->spriteEditorPanel()->previewCanvas() : nullptr;
                        if (pv) pv->overlay()->updateLayout();
                        auto* apL = m_frameAnimWorkspace ? m_frameAnimWorkspace->animPanel() : nullptr;
                        auto* ac = apL ? apL->animCanvas() : nullptr;
                        if (ac && ac->overlay()) ac->overlay()->updateLayout();
                        refreshHandleCombo();
                    },
                    std::move(coTargets)
                ));
            });
        }
    }

    {
        auto* ap = m_frameAnimWorkspace ? m_frameAnimWorkspace->animPanel() : nullptr;
        auto* animCanvas = ap ? ap->animCanvas() : nullptr;
        if (animCanvas && animCanvas->overlay()) {
            connect(animCanvas->overlay(), &EditorOverlayItem::pivotDragFinished,
                    this, [this](int oldX, int oldY, int newX, int newY) {
                // Use the animation frame's sprite directly — independent of the
                // global selection so that navigator clicks never affect the wrong frame.
                auto* apL = m_frameAnimWorkspace ? m_frameAnimWorkspace->animPanel() : nullptr;
                auto* ac = apL ? apL->animCanvas() : nullptr;
                const SpritePtr sprite = ac ? ac->overlaySprite() : nullptr;
                if (!sprite) return;
                AnimationPreviewService::invalidateBounds();
                updateOnionSkinDisplay();
                m_undoStack->push(new SetPivotCommand(sprite,
                                                      oldX, oldY, newX, newY, true,
                                                      {}));
                // Rebuild the composite with the new pivot so the overlay is
                // repositioned immediately and the stored coords stay consistent.
                refreshAnimationTest();
            });
            connect(animCanvas->overlay(), &EditorOverlayItem::markerDragFinished,
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
                        auto* pv = m_atlasWorkspace ? m_atlasWorkspace->spriteEditorPanel()->previewCanvas() : nullptr;
                        if (pv) pv->overlay()->updateLayout();
                        auto* apL2 = m_frameAnimWorkspace ? m_frameAnimWorkspace->animPanel() : nullptr;
                        auto* ac = apL2 ? apL2->animCanvas() : nullptr;
                        if (ac && ac->overlay()) ac->overlay()->updateLayout();
                        refreshHandleCombo();
                    },
                    std::move(coTargets)
                ));
            });
        }
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
        cfg.canvas          = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
        cfg.profileCombo    = m_atlasWorkspace ? m_atlasWorkspace->profileCombo() : nullptr;
        cfg.resolutionCombo = m_atlasWorkspace ? m_atlasWorkspace->sourceResolutionCombo() : nullptr;
        cfg.atlasViewStack       = m_atlasWorkspace ? m_atlasWorkspace->atlasViewStack() : nullptr;
        cfg.atlasMgmtWorkspace   = m_atlasesManagementWorkspace;
        cfg.layoutBinary         = m_spratLayoutBin;
        cfg.context = this;
        m_layoutOrchestrator = new LayoutOrchestrator(cfg, this);

        // Sync initial settings
        m_layoutOrchestrator->setSyncMode(m_settings.syncMode);
        m_layoutOrchestrator->setDeduplicateMode(m_settings.deduplicateMode);
        m_layoutOrchestrator->setDedupThreshold(m_settings.dedupThreshold);
        m_layoutOrchestrator->setIncrementalLayout(m_settings.incrementalLayout);
        m_layoutOrchestrator->setLayoutZoomOnChange(m_settings.layoutZoomOnChange);
        m_layoutOrchestrator->setCLIReady(m_cliReady);
        m_layoutOrchestrator->setMergeReplaceAllDuplicates(m_mergeReplaceAllDuplicates);
        m_layoutOrchestrator->setActiveWorkspace(0);

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
        connect(m_layoutOrchestrator, &LayoutOrchestrator::logMessage,
                this, [this](const QString& text) {
                    const bool isSuccess = text.contains(QLatin1String("] Exit: 0 ("));
                    const bool hasStderr = text.contains(QLatin1String("\n  stderr: "));
                    LogLevel level = LogLevel::Cli;
                    if (!isSuccess)        level = LogLevel::Error;
                    else if (hasStderr)    level = LogLevel::Warning;
                    appendLog(level, text);
                });
        connect(m_layoutOrchestrator, &LayoutOrchestrator::manualFrameLabelUpdateNeeded,
                this, &MainWindow::updateManualFrameLabel);
        connect(m_layoutOrchestrator, &LayoutOrchestrator::pendingProjectPayloadReady,
                this, &MainWindow::applyProjectPayload);
        connect(m_layoutOrchestrator, &LayoutOrchestrator::selectSpritesByPathsRequested,
                this, [this](const QStringList& paths, const QString& primary) {
                    auto* canvas = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
                    if (canvas) canvas->selectSpritesByPaths(paths, primary);
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
                    auto* profileCombo = m_atlasWorkspace ? m_atlasWorkspace->profileCombo() : nullptr;
                    if (!profileCombo) return;
                    const QString previous = m_currentProfile;
                    const QSignalBlocker blocker(profileCombo);
                    profileCombo->setCurrentIndex(profileCombo->findData(profile));
                    m_currentProfile = profile;
                    if (m_layoutOrchestrator) m_layoutOrchestrator->setCurrentProfile(profile);
                    if (!previous.isEmpty() && previous != profile)
                        showProfileFallbackNotice(previous, profile);
                });
        connect(m_layoutOrchestrator, &LayoutOrchestrator::cliReadyCheckNeeded,
                this, &MainWindow::checkCliTools);
    }

    // Create ExportCoordinator
    {
        ExportCoordinator::Config cfg;
        cfg.parentWidget       = this;
        cfg.session            = m_session;
        cfg.layoutOrchestrator = m_layoutOrchestrator;
        cfg.exportLayoutCanvas = m_exportLayoutCanvas;
        cfg.packedAtlasView    = m_packedAtlasView;
        cfg.exportWorkspace    = m_exportWorkspace;
        cfg.layoutContext      = this;
        cfg.runTool            = [this](const QString& tool, const QStringList& args,
                                        const QByteArray* in, QByteArray* out, QByteArray* err) {
            return runTool(tool, args, in, out, err);
        };
        cfg.buildProjectPayload = [this](const SaveConfig& config, bool portable) {
            return buildProjectPayload(config, m_session, portable);
        };
        cfg.promoteSourceFolderAfterSave = [this](const QString& dest) {
            promoteSourceFolderAfterSave(dest);
        };
        m_exportCoordinator = new ExportCoordinator(cfg, this);

        // Sync initial settings
        m_exportCoordinator->setAppSettings(m_settings);
        m_exportCoordinator->setCliReady(m_cliReady);
        m_exportCoordinator->setLayoutBinary(m_spratLayoutBin);
        m_exportCoordinator->setPackBinary(m_spratPackBin);
        m_exportCoordinator->setConvertBinary(m_spratConvertBin);

        connect(m_exportCoordinator, &ExportCoordinator::statusChanged,
                this, [this](const QString& msg) {
                    m_loadingUiMessage = msg;
                    if (m_statusLabel) m_statusLabel->setText(msg);
                    if (m_cliInstallOverlayLabel) m_cliInstallOverlayLabel->setText(msg);
                    if (m_welcomeLabel && m_session
                        && (m_session->activeAtlas().layoutModels.isEmpty()
                            || m_session->activeAtlas().layoutModels.first().sprites.isEmpty())) {
                        m_welcomeLabel->setText(msg);
                    }
                });
        connect(m_exportCoordinator, &ExportCoordinator::loadingStateChanged,
                this, &MainWindow::setLoading);
        connect(m_exportCoordinator, &ExportCoordinator::exportLogReady,
                this, [this](const QVector<ExportLogEntry>& entries, const QString& dest) {
                    if (m_exportWorkspace)
                        m_exportWorkspace->showExportLog(entries, dest);
                });
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
        if (m_exportCoordinator) m_exportCoordinator->setCliReady(true);
        updateUiState();
        updateCliDiagnostics();
        scheduleLayoutRebuild(true);
    });
    connect(m_cliSetup, &CliSetupController::cliFailed, this, [this]() {
        m_cliReady = false;
        if (m_layoutOrchestrator) m_layoutOrchestrator->setCLIReady(false);
        if (m_exportCoordinator) m_exportCoordinator->setCliReady(false);
        updateUiState();
    });
    connect(m_cliSetup, &CliSetupController::binaryPathsResolved, this, [this](const CliPaths& paths) {
        m_spratLayoutBin  = paths.layoutBinary;
        m_spratPackBin    = paths.packBinary;
        m_spratConvertBin = paths.convertBinary;
        m_spratFramesBin  = paths.framesBinary;
        m_spratUnpackBin  = paths.unpackBinary;
        if (m_layoutOrchestrator) m_layoutOrchestrator->updateLayoutBinary(paths.layoutBinary);
        if (m_exportCoordinator) {
            m_exportCoordinator->setLayoutBinary(paths.layoutBinary);
            m_exportCoordinator->setPackBinary(paths.packBinary);
            m_exportCoordinator->setConvertBinary(paths.convertBinary);
        }
        if (m_projectController) {
            m_projectController->setFramesBinary(paths.framesBinary);
            m_projectController->setConvertBinary(paths.convertBinary);
            m_projectController->setUnpackBinary(paths.unpackBinary);
            m_projectController->setFramesSettings({
                m_settings.framesHasRectangles,
                m_settings.framesRectangleColor,
                m_settings.framesTolerance,
                m_settings.framesMinSize,
                m_settings.framesMaxSprites
            });
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
        auto* apW = m_frameAnimWorkspace ? m_frameAnimWorkspace->animPanel() : nullptr;
        auto* animCanvas = apW ? apW->animCanvas() : nullptr;
        if (animCanvas) animCanvas->setZoomManual(false);
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
void MainWindow::updateWindowModified() {
    setWindowModified(m_hasUnsavedWorkspaceEdits || (m_undoStack && !m_undoStack->isClean()));
}

void MainWindow::markWorkspaceEdited() {
    m_hasUnsavedWorkspaceEdits = true;
    updateWindowModified();
}

void MainWindow::markProjectUnmodified(bool clearHistory) {
    m_hasUnsavedWorkspaceEdits = false;
    if (m_undoStack) {
        if (clearHistory) m_undoStack->clear();
        else m_undoStack->setClean();
    }
    updateWindowModified();
}

void MainWindow::showProfileFallbackNotice(const QString& previousProfile, const QString& newProfile) {
    // Non-modal, and reused when several profiles are tried in a row, so a layout that
    // runs on its own doesn't stack up dialogs.
    if (!m_profileFallbackNotice) {
        m_profileFallbackNotice = new QMessageBox(QMessageBox::Information, tr("Layout Profile Changed"),
                                                  QString(), QMessageBox::Ok, this);
        m_profileFallbackNotice->setWindowModality(Qt::NonModal);
        m_profileFallbackNotice->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_profileFallbackNotice->setText(
        tr("The layout failed with the \"%1\" profile (for example, a sprite is larger than its "
           "maximum atlas size), so it now uses \"%2\".\n\n"
           "Exports use the selected profile, now \"%2\". Pick another profile in the profile list, "
           "or change the profile's limits in Settings > Manage Profiles.")
            .arg(previousProfile, newProfile));
    m_profileFallbackNotice->show();
    m_profileFallbackNotice->raise();
}

bool MainWindow::confirmDiscardUnsavedChanges(const QString& question) {
    if (!isWindowModified())
        return true;
    const auto choice = MessageDialog::customQuestion(
        this, tr("Unsaved Changes"), question,
        {tr("Save"), tr("Don't Save"), tr("Cancel")},
        0,
        tr("Unsaved Changes"),
        {QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton),
         QApplication::style()->standardIcon(QStyle::SP_DialogDiscardButton),
         QApplication::style()->standardIcon(QStyle::SP_DialogCancelButton)});
    switch (choice) {
    case 0:  return onSaveClicked(); // a canceled or failed save keeps the current project
    case 1:  return true;
    default: return false;
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmDiscardUnsavedChanges(tr("You have unsaved changes. Save before closing?"))) {
        event->ignore();
        return;
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
    s_eventLogTarget.storeRelease(nullptr);
    qInstallMessageHandler(s_prevMessageHandler);

    m_isCanceled = true;

    // Ensure all background tasks are stopped/finished before we destroy members
    if (m_projectController) {
        m_projectController->cancelAll();
    }
    // ExportCoordinator owns its own watchers and cleans up via QObject parent chain

    if (m_autosaveTimer)
        m_autosaveTimer->stop();

    if (m_session) m_session->removeFrameListFile();

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
            if (m_frameAnimWorkspace) m_frameAnimWorkspace->handleResize();
            // Update canvas overlay if visible
            auto* resizeCanvas = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
            if (m_canvasOverlay && m_canvasOverlay->isVisible() && resizeCanvas) {
                m_canvasOverlay->resize(resizeCanvas->size());
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

// hasDuplicateTimelineName and getUniqueTimelineName moved to TimelineEditorPanel (Phase 7).
// MainWindow wrappers delegate to the panel via MainWindow.TimelineEditor.cpp.



void MainWindow::appendLog(LogLevel level, const QString& text)
{
    if (!m_logWidget) return;

    // Check filter: skip entry if its level toggle is off
    auto* btn = (level == LogLevel::Cli)       ? m_logFilterCli
              : (level == LogLevel::Info)      ? m_logFilterInfo
              : (level == LogLevel::Warning)   ? m_logFilterWarn
              : (level == LogLevel::Qt)        ? m_logFilterQt
              : (level == LogLevel::Diagnosis) ? m_logFilterDiag
              :                                  m_logFilterError;
    const QString filterText = m_logFilterEdit ? m_logFilterEdit->text() : QString();
    const bool passesFilter = (!btn || btn->isChecked())
                           && (filterText.isEmpty() || text.contains(filterText, Qt::CaseInsensitive));

    m_logEntries.append({level, text});

    if (passesFilter) {
        QTextCursor cursor(m_logWidget->document());
        cursor.movePosition(QTextCursor::End);
        if (!m_logWidget->document()->isEmpty())
            cursor.insertText(QStringLiteral("\n"));
        QTextCharFormat fmt;
        switch (level) {
        case LogLevel::Info:      fmt.setForeground(QColor(100, 160, 240)); break;
        case LogLevel::Warning:   fmt.setForeground(QColor(220, 140,   0)); break;
        case LogLevel::Error:     fmt.setForeground(QColor(200,  60,  60)); break;
        case LogLevel::Qt:        fmt.setForeground(QColor(160, 110, 220)); break;
        case LogLevel::Diagnosis: fmt.setForeground(QColor( 80, 180, 160)); break;
        default: break;
        }
        cursor.insertText(text, fmt);
        m_logWidget->verticalScrollBar()->setValue(m_logWidget->verticalScrollBar()->maximum());
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
        QMetaObject::invokeMethod(this, [this, logEntry]() {
            appendLog(LogLevel::Error, logEntry);
        }, Qt::QueuedConnection);
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
    const bool hasStderr = !stderrStr.isEmpty();
    QMetaObject::invokeMethod(this, [this, logEntry, ok, hasStderr]() {
        LogLevel level = LogLevel::Cli;
        if (!ok)           level = LogLevel::Error;
        else if (hasStderr) level = LogLevel::Warning;
        appendLog(level, logEntry);
    }, Qt::QueuedConnection);

    return ok;
}

// ===== Source Folder Sync Slots =====

void MainWindow::onUndo() {
    if (!m_undoStack) return;
    if (m_atlasWorkspace) m_atlasWorkspace->clearCoordinateOverride();
    m_undoStack->undo();
    if (m_atlasWorkspace) m_atlasWorkspace->refreshSpriteEditor();
}

void MainWindow::onRedo() {
    if (!m_undoStack) return;
    if (m_atlasWorkspace) m_atlasWorkspace->clearCoordinateOverride();
    m_undoStack->redo();
    if (m_atlasWorkspace) m_atlasWorkspace->refreshSpriteEditor();
}

void MainWindow::onQuickStart() {
    QString content = tr(
        "<h2>Quick Start Guide</h2>"

        "<h3>Workflow Overview</h3>"
        "<ol>"
        "<li><b>Load sprites</b> &mdash; drag a folder, archive, or image onto the window.</li>"
        "<li><b>Sprites workspace</b> &mdash; select sprites to edit pivots and markers.</li>"
        "<li><b>Atlases workspace</b> &mdash; organize sprites across named atlases.</li>"
        "<li><b>Nine-Slice workspace</b> &mdash; define scalable nine-patch regions for UI sprites.</li>"
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

        "<h3>3 &mdash; Atlases Workspace</h3>"
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

        "<h3>4 &mdash; Nine-Slice Workspace</h3>"
        "<p>Open from the <i>Nine-Slice</i> tab (Alt+N). Pick the sprites to nine-slice and define "
        "their slice regions:</p>"
        "<ul>"
        "<li>The left list shows only nine-sliced sprites. Click <b>Add</b> and tick one or more "
        "sprites; their slice lines start at image thirds.</li>"
        "<li>Drag the slice lines on the canvas, or type values in the "
        "<b>L / T / R / B</b> spinboxes.</li>"
        "<li>Choose <b>horizontal</b> and <b>vertical fill modes</b>: "
        "<i>stretch</i>, <i>repeat</i>, or <i>mirror</i>.</li>"
        "<li>Set a <b>target size</b> (W / H) or drag the canvas resize handles to preview "
        "the nine-slice result at the intended render dimensions.</li>"
        "<li><b>Remove</b> (or Delete) stops nine-slicing the selected sprites. Their slice lines "
        "are kept if you add them again.</li>"
        "</ul>"
        "<p>Nine-slice definitions are saved in the project and exported with the atlas metadata.</p>"

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
    // Shortcuts are shown in the platform's notation (e.g. ⌘S on macOS) and, where an
    // action exists, read from it, so this list can't drift from the real bindings.
    auto keys = [](const QKeySequence& sequence) {
        return sequence.toString(QKeySequence::NativeText).toHtmlEscaped();
    };
    auto standardKeys = [&keys](QKeySequence::StandardKey key) {
        const QList<QKeySequence> bindings = QKeySequence::keyBindings(key);
        return bindings.isEmpty() ? QString() : keys(bindings.first());
    };
    auto actionKeys = [&keys](const QAction* action) {
        return action ? keys(action->shortcut()) : QString();
    };
    // "Ctrl+" on Windows/Linux, "⌘" on macOS, for combinations with the mouse.
    QString ctrl = keys(QKeySequence(Qt::CTRL | Qt::Key_A));
    ctrl.chop(1);

    QString content = QStringLiteral("<h2>%1</h2>").arg(tr("Keyboard Shortcuts"));
    auto beginSection = [&content, this](const QString& title) {
        content += QStringLiteral("<h3>%1</h3><table border='1' cellpadding='4' cellspacing='0' width='100%'>"
                                  "<tr><th width='60%'>%2</th><th>%3</th></tr>")
                       .arg(title, tr("Action"), tr("Shortcut"));
    };
    auto row = [&content](const QString& action, const QString& shortcut) {
        if (!shortcut.isEmpty())
            content += QStringLiteral("<tr><td>%1</td><td><b>%2</b></td></tr>").arg(action, shortcut);
    };
    auto endSection = [&content]() { content += QStringLiteral("</table>"); };
    const QString orText = tr(" or ");

    beginSection(tr("General"));
    row(tr("Open Project"), actionKeys(m_loadProjectAction));
    row(tr("Save Project"), actionKeys(m_saveAction));
    row(tr("Save Project As"), actionKeys(m_saveAsAction));
    row(tr("Export Again"), actionKeys(m_exportAction));
    row(tr("Undo"), standardKeys(QKeySequence::Undo));
    row(tr("Redo"), standardKeys(QKeySequence::Redo));
    row(tr("Import from Clipboard"), standardKeys(QKeySequence::Paste));
    row(tr("Quit"), standardKeys(QKeySequence::Quit));
    endSection();

    beginSection(tr("Workspaces"));
    row(tr("Sprites"), actionKeys(m_atlasWorkspaceAction));
    row(tr("Atlases"), actionKeys(m_atlasesManagementWorkspaceAction));
    row(tr("Nine-Slice"), actionKeys(m_nineSliceWorkspaceAction));
    row(tr("Frame Animation"), actionKeys(m_frameAnimWorkspaceAction));
    row(tr("Export"), actionKeys(m_exportationWorkspaceAction));
    endSection();

    beginSection(tr("Canvas Views (Layout, Preview, Animation)"));
    row(tr("Zoom In"), keys(QKeySequence(Qt::CTRL | Qt::Key_Plus)) + orText + ctrl + tr("Scroll Up"));
    row(tr("Zoom Out"), keys(QKeySequence(Qt::CTRL | Qt::Key_Minus)) + orText + ctrl + tr("Scroll Down"));
    row(tr("100% Zoom"), keys(QKeySequence(Qt::CTRL | Qt::Key_1)));
    row(tr("Fit to Window"), keys(QKeySequence(Qt::CTRL | Qt::Key_0)));
    row(tr("Pan View"), tr("Space+Drag") + orText + tr("Middle Mouse Drag"));
    endSection();

    beginSection(tr("Layout Canvas"));
    row(tr("Navigate Sprites"), tr("Arrow Keys"));
    row(tr("First / Last Sprite in Row"), tr("Home / End"));
    row(tr("Extend Selection"), tr("Shift+Arrow Keys"));
    row(tr("Select All Sprites"), standardKeys(QKeySequence::SelectAll));
    row(tr("Remove Selected Sprites"), standardKeys(QKeySequence::Delete));
    row(tr("Quick Split"), tr("Alt+Click on a sprite"));
    endSection();
    content += QStringLiteral("<p><i>%1</i></p>")
                   .arg(tr("To find a sprite, use the Search field in the Sprites workspace."));

    beginSection(tr("Navigation View"));
    row(tr("Remove / Exclude Selected Sprites"), standardKeys(QKeySequence::Delete));
    row(tr("Toggle Sprite Checkbox"), ctrl + tr("Click"));
    row(tr("Check a Range of Sprites"), tr("Shift+Click"));
    row(tr("Context Menu (Atlases workspace)"), tr("Right-Click a folder, group or source"));
    endSection();

    beginSection(tr("Timeline"));
    row(tr("Navigate Frames"), tr("Arrow Keys"));
    row(tr("Extend Frame Selection"), tr("Shift+Arrow Keys"));
    row(tr("Select All Frames"), standardKeys(QKeySequence::SelectAll));
    row(tr("Delete Selected Frames"), standardKeys(QKeySequence::Delete));
    endSection();

    beginSection(tr("Sprite Preview"));
    row(tr("Move Selected Marker or Pivot 1 px"), tr("Arrow Keys"));
    row(tr("Move Selected Marker or Pivot 10 px"), tr("Shift+Arrow Keys"));
    row(tr("Copy Markers"), standardKeys(QKeySequence::Copy));
    row(tr("Paste Markers"), standardKeys(QKeySequence::Paste));
    row(tr("Delete Selected Marker / Vertex"), standardKeys(QKeySequence::Delete));
    endSection();

    beginSection(tr("Animation Playback"));
    row(tr("Play / Pause Animation"), tr("Space"));
    endSection();

    beginSection(tr("Debug"));
    row(tr("Toggle Debug Panel"), keys(QKeySequence(Qt::Key_F12)));
    endSection();

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

void MainWindow::refreshUiAfterUndo() {
    // Refresh UI
    updateMainContentView();
    updateUiState();
    refreshSpriteTree();
    refreshTimelineList();
    refreshTimelineFrames();
    refreshAnimationTest();
    auto* canvas = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
    if (canvas) canvas->update();
    auto* previewCanvas = m_atlasWorkspace ? m_atlasWorkspace->spriteEditorPanel()->previewCanvas() : nullptr;
    if (previewCanvas) previewCanvas->overlay()->updateLayout();
    refreshHandleCombo();
    updateManualFrameLabel();
    updateOpenSourceFolderAction();
}

void MainWindow::setSourceFolderIsTemp(bool isTemp) {
    if (m_projectController) m_projectController->setSourceFolderIsTemp(isTemp);
}

#ifndef Q_OS_WASM
void MainWindow::checkForUpdates() {
    if (!m_updateCheckEnabled) return;
    auto* checker = new UpdateChecker(this);
    connect(checker, &UpdateChecker::updateAvailable, this,
            [this](const UpdateChecker::ReleaseInfo& info) {
                auto* dlg = new UpdateDialog(info, this);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->exec();
            });
    checker->check();
}
#endif
