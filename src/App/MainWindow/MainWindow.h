#pragma once
#include <QMainWindow>
#include <QProcess>
#include <QTimer>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QIcon>
#include <QJsonObject>
#include <QFutureWatcher>
#include <QPair>
#include <QMutex>
#include <functional>
#include <QElapsedTimer>
#include <QUndoStack>

#include "SourceFolderWatcher.h"
#include "ProjectSession.h"
#include "SpratProfilesConfig.h"
#include "models.h"
#include "DropAction.h"
#include "SettingsDialog.h"

// Forward declarations for Qt classes
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QLabel;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QListWidget;
class QLineEdit;
class QActionGroup;
class QGroupBox;
class QGraphicsView;
class QScrollArea;
class QPushButton;
class QDragEnterEvent;
class QDropEvent;
class QWheelEvent;
class QContextMenuEvent;
class QResizeEvent;
class QMouseEvent;
class QKeyEvent;
class QTemporaryDir;
class QWidget;
class QProgressBar;
class QDockWidget;
class QSplitter;
class QTextEdit;
class QPlainTextEdit;
class QAction;
class QToolButton;
class QMenu;
class QVariantAnimation;
class QMimeData;
class QImage;
class QUrl;
class QSize;

// Forward declarations for custom classes
class UpdateChecker;
class PackedAtlasView;
class LayoutOrchestrator;
class CliSetupController;
class LayoutCanvas;
class ExportWorkspace;
class FrameAnimationWorkspace;
class NineSliceWorkspace;
#include "ILayoutContext.h"
#include "IWorkspace.h"
#include "AtlasWorkspace.h"
#include "ProjectController.h"
#include "ExportCoordinator.h"
#include "UndoCommands.h"
#ifdef Q_OS_WASM
class WasmFolderBrowserDialog;
#endif

/**
 * @class MainWindow
 * @brief Main application window for sprat-gui.
 * 
 * This class manages the entire user interface and coordinates
 * between different components including layout canvas, timeline editor,
 * sprite editor, and animation preview.
 */
class MainWindow : public QMainWindow, public ILayoutContext, public IMainWindowUndoHost {
    Q_OBJECT
public:
    /**
     * @brief Constructor for MainWindow.
     * 
     * @param parent Parent widget (optional)
     */
    explicit MainWindow(QWidget* parent = nullptr);

    /**
     * @brief Destructor for MainWindow.
     */
    ~MainWindow() override;

#ifdef Q_OS_WASM
    static MainWindow* wasmInstance();
    void onWasmFilePicked(const QString& path, int mode);
#endif

private slots:
    /**
     * @brief Handles request to open a recent project.
     */
    void onOpenRecentProjectRequested();

#ifndef Q_OS_WASM
    /**
     * @brief Saves project state to a new folder chosen by the user.
     */
    void onSaveAsClicked();
#endif

    // === Layout Canvas Events ===
    /**
     * @brief Handles when a path is dropped onto the layout canvas.
     * 
     * @param path Path that was dropped
     */
    void onLayoutCanvasPathDropped(const QString& path);

    /**
     * @brief Handles request to add frames from canvas context menu (immediate rebuild).
     */
    void onCanvasAddFramesRequested();

    /**
     * @brief Handles request to add frames (lazy loading, used by Navigator and other deferred operations).
     */
    void onAddFramesRequested();

    /**
     * @brief Handles request to remove frames from canvas (immediate rebuild).
     *
     * @param paths List of paths to remove
     */
    void onCanvasRemoveFramesRequested(const QStringList& paths);

    /**
     * @brief Handles request to remove frames (lazy loading, used by Navigator and other deferred operations).
     *
     * @param paths List of paths to remove
     */
    void onRemoveFramesRequested(const QStringList& paths);

    /**
     * @brief Handles sprite split request from layout canvas.
     *
     * @param sprite The sprite to split
     * @param orientation Split orientation (Horizontal or Vertical)
     * @param localPos Position of the split in local sprite coordinates
     */
    void onSplitSpriteRequested(SpritePtr sprite, Qt::Orientation orientation, int localPos);

    // === Project Management Events ===
    /**
     * @brief Handles loading of a folder with images.
     */
    void onLoadFolder();

    /**
     * @brief Handles adding a local source file such as an image or archive.
     */
    void onAddSourceFile();

    /**
     * @brief Handles loading of a saved project.
     */
    void onLoadProject();
    void onLoadFromUrl();

    /**
     * @brief Handles running the layout generation process.
     *
     * @param quiet If true, shows only the progress bar instead of the full loading overlay.
     */
    void onRunLayout(bool quiet = false);

    /**
     * @brief Handles autosave of the current project.
     */
    void autosaveProject();

    /**
     * @brief Handles loading of an autosaved project.
     */
    void loadAutosavedProject();

    /**
     * @brief Handles save action from the user (silent, uses last destination).
     */
    void onSaveClicked();

    /**
     * @brief Re-runs export to the last-used destination; opens dialog on first use.
     */
    void onExportClicked();

    /**
     * @brief Always opens the export workspace to choose a destination.
     */
    void onExportAsClicked();

    void onExportWorkspaceRequested(SaveConfig config);
    bool runExport(SaveConfig config);
    void refreshPreview(const QString& profileName, const QString& scaleFilter);
    void schedulePreviewPack(const QString& profileName, const QString& scaleFilter);
    void switchWorkspace(IWorkspace* next);
    void applyWorkspaceLayout(IWorkspace* ws);

    /**
     * @brief Handles when a generic process finishes execution.
     * 
     * @param exitCode Exit code of the process
     * @param exitStatus Exit status of the process
     */
#ifndef SPRAT_EMBEDDED_CLI
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
#endif

    /**
     * @brief Handles download progress of CLI tools (UI update only).
     *
     * @param bytesReceived Bytes received so far
     * @param bytesTotal Total bytes to receive
     */
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);

    /**
     * @brief Handles sprite selection from the layout canvas.
     * 
     * @param sprite Selected sprite
     */
    void onSpriteSelected(SpritePtr sprite);

    // === Profile Management Events ===
    /**
     * @brief Handles changes to profile selection.
     */
    void onProfileChanged();
    void onLayoutZoomChanged(double value);

    /**
     * @brief Pauses the layout rebuild timer when user hovers over canvas.
     */
    void pauseLayoutRebuild();

    /**
     * @brief Resumes the layout rebuild timer when user stops hovering over canvas.
     */
    void resumeLayoutRebuild();

    /**
     * @brief Captures current sprite positions before layout rebuild.
     */
    void captureOldSpritePositions();

    /**
     * @brief Handles request to manage profiles.
     */
    void onManageProfiles();

    /**
     * @brief Handles generating timelines from frame names.
     */
    void onGenerateTimelinesFromFrames();

    /**
     * @brief Builds project payload for saving.
     * 
     * @param config Save configuration
     * @param session Project session
     * @return QJsonObject Project payload
     */
    QJsonObject buildProjectPayload(SaveConfig config, ProjectSession* session, bool portable = false);

    /**
     * @brief Handles autosave timer timeout.
     */
    void onAutosaveTimer();

    /**
     * @brief Handles cancel button click on loading overlay.
     */
    void onCancelLoading();

    void onGifSyncFinished();
    void onPasteImport();

    // === Undo/Redo ===
    void onUndo();
    void onRedo();

    // === Sprite Name ===
    void onEditAliases();

    // === Asynchronous Loading Slots ===
    void onProjectLoadFinished();
    void onZipDiscoveryFinished();
    void onFrameDetectionFinished();
    void onTarExtractionFinished();
    void onFrameExtractionFinished();
    void onTransparencyProcessingFinished();

    // === CLI Installation Logging ===
    void onCliInstallLog(const QString& message);

    // === Sources ===
    void removeSource(int index);
    void onDetectFramesRequested(const QString& imagePath);
    void onRemoveSourceRequested(int sourceIndex);
    void onRetrySourceRequested(int sourceIndex);
    void onSyncSourceRequested(int sourceIndex);
    void onSyncLayoutRequested(int sourceIndex);

    // === Source Folder Sync ===
    void onFolderWatcherFilesAdded(const QStringList& paths);
    void onFolderWatcherFilesRemoved(const QStringList& paths);
    void onFolderWatcherFilesModified(const QStringList& paths);
    void onSyncNowRequested();
    void performFolderSync();
    void onOpenSourceFolderClicked();
    void ensureSourceFolder();             // creates temp dir if needed
    void promoteSourceFolderAfterSave(const QString& saveDestination);
    void copySpriteToProjectFolder(const QString& projectDir);
    void syncNewSpritesToProjectFolder(const QString& projectDir);
    void updateOpenSourceFolderAction();   // enable/disable based on state
    void onWatchModePeriodicCheck();       // periodic file removal detection in Watch mode
#ifdef Q_OS_WASM
    void onSpritesDeleted(const QStringList& paths);
#endif

protected:
    // === Event Handling ===
    /**
     * @brief Handles drag enter events.
     * 
     * @param event Drag enter event
     */
    void dragEnterEvent(QDragEnterEvent* event) override;

    /**
     * @brief Handles drop events.
     * 
     * @param event Drop event
     */
    void dropEvent(QDropEvent* event) override;

    /**
     * @brief Handles event filtering.
     * 
     * @param watched Watched object
     * @param event Event to filter
     * @return bool True if event was handled
     */
    bool eventFilter(QObject* watched, QEvent* event) override;

    /**
     * @brief Handles window close events.
     *
     * @param event Close event
     */
    void closeEvent(QCloseEvent* event) override;

private:
    // === Helper Methods ===
    /**
     * @brief Schedules a debounced layout rebuild (1000 ms).
     *
     * Delegates to m_layoutOrchestrator->schedule().
     *
     * @param immediate If true, rebuild immediately without debounce
     * @param skipCapture If true, skip capturing sprite positions (already captured elsewhere)
     */
    void scheduleLayoutRebuild(bool immediate = false, bool skipCapture = false);

    /**
     * @brief Gets the layout parser folder path.
     */
    QString layoutParserFolder() const override;


    /**
     * @brief Ensures frame list input is valid.
     *
     * @return bool True if input is valid
     */
    bool ensureFrameListInput();

    /**
     * @brief Checks if active frames are in the source folder.
     *
     * @return bool True if all active frames are in the source folder
     */
    bool activeFramesAreInSourceFolder() const override;

    /**
     * @brief Checks if the source folder contains exactly the current active frames.
     *
     * @return bool True if the source folder image set matches the active frame list
     */
    bool sourceFolderMatchesActiveFrames() const override;

    /**
     * @brief Returns whether removing sprites should also remove their backing files.
     */
    bool shouldDeleteRemovedSpritesFromSource() const;

    /**
     * @brief Copies active frames to the source folder.
     *
     * @param overwriteDuplicates If true, replace existing files with the same name; if false, rename to avoid conflicts
     */
    void copyActiveFramesToSourceFolder(bool overwriteDuplicates = true) override;

    /**
     * @brief Clears image files from the source folder.
     *
     * @param excludePath If provided, skips clearing if the folder path matches (prevents erasing folder we're about to load from)
     */
    void clearSourceFolderImages(const QString& excludePath = QString());

    /**
     * @brief Performs manual sync: compares layout sprites with folder images.
     * Removes missing sprites, updates modified ones, adds new ones.
     */
    void performManualSync();

    /**
     * @brief Gets configured profiles.
     * 
     * @return QVector<SpratProfile> List of configured profiles
     */
    QVector<SpratProfile> configuredProfiles() override;

    /**
     * @brief Gets the selected profile definition.
     * 
     * @param out Reference to store profile definition
     * @return bool True if profile was found
     */
    bool selectedProfileDefinition(SpratProfile& out) const override;

    /**
     * @brief Applies configured profiles to the layout.
     * 
     * @param profiles List of profiles to apply
     * @param preferred Preferred profile name (optional)
     */
    void applyConfiguredProfiles(const QVector<SpratProfile>& profiles, const QString& preferred = QString());

    /**
     * @brief Populates active frame list from model.
     */
    void populateActiveFrameListFromModel();

    /**
     * @brief Updates manual frame label.
     */
    void updateManualFrameLabel();


    /**
     * @brief Initializes the source folder watcher based on sync mode.
     */
    void initializeSourceFolderWatcher();

    /**
     * @brief Stops and cleans up the source folder watcher.
     */
    void cleanupSourceFolderWatcher();

    /**
     * @brief Shows a notification about folder sync changes.
     */
    void showSyncNotification(const QString& message);

    // === UI Setup Methods ===
    /**
     * @brief Sets up the main user interface.
     */
    void setupUi();

    /**
     * @brief Sets up the toolbar.
     */
    void setupToolbar();

    /**
     * @brief Sets up the status bar UI.
     */
    void setupStatusBarUi();

    /**
     * @brief Sets up zoom shortcuts.
     */
    void setupZoomShortcuts();

    /**
     * @brief Sets up keyboard shortcuts (Ctrl+S, Ctrl+Z/Y).
     */
    void setupKeyboardShortcuts();

    /**
     * @brief Adds project to recent projects list.
     *
     * @param path Path to project file
     */
    void addToRecentProjects(const QString& path);

    /**
     * @brief Updates the recent projects menu.
     */
    void updateRecentProjectsMenu();

    /**
     * @brief Checks CLI tools availability (delegates to m_cliSetup).
     */
    void checkCliTools();

#ifndef Q_OS_WASM
    /**
     * @brief Checks for application updates via the GitHub Releases API.
     */
    void checkForUpdates();
#endif
    void updateCliDiagnostics();

    /**
     * @brief Confirms the action to take when a file/folder is dropped.
     * 
     * @param path Path of the dropped item
     * @return DropAction The selected action
     */
    DropAction confirmDropAction(const QString& path);

    /**
     * @brief Sets loading state of the application.
     * 
     * @param loading True to show loading state
     */
    void setLoading(bool loading);

    /**
     * @brief Updates the UI state based on current application state.
     */
    void updateUiState();

    /**
     * @brief Updates the main content view.
     */
    void updateMainContentView();

    /**
     * @brief Installs CLI tools (delegates to m_cliSetup).
     */
    void installCliTools();

    /**
     * @brief Loads a folder with images.
     * 
     * @param path Path to folder
     * @param action Action to take (Replace or Merge)
     */
    void loadFolder(const QString& path, DropAction action = DropAction::Replace);

    /**
     * @brief Loads a saved project.
     * 
     * @param path Path to project file
     * @param action Action to take (Replace or Merge)
     */
    void loadProject(const QString& path, DropAction action = DropAction::Replace);

    /**
     * @brief Confirms layout replacement with the user.
     * 
     * @return bool True if user confirmed replacement
     */
    bool confirmLayoutReplacement();

    /**
     * @brief Checks if a drop path is supported.
     * 
     * @param path Path to check
     * @return bool True if path is supported
     */
    bool isSupportedDropPath(const QString& path) const;

    /**
     * @brief Tries to handle a dropped path.
     *
     * @param path Path that was dropped
     * @param action Action to take (Replace or Merge)
     * @return bool True if path was handled
     */
    bool tryHandleDroppedPath(const QString& path, DropAction action = DropAction::Replace);
    bool tryHandleRemoteUrl(const QUrl& url, DropAction action = DropAction::Replace);
    bool tryImportClipboard(const QMimeData* mimeData, DropAction action = DropAction::Replace);
    QString createManagedImportFile(const QString& suggestedName, const QByteArray& data, QString& error);
    QString createManagedImportImageFile(const QImage& image, QString& error);
    bool syncLayoutToImage(const ProjectSource& src, QString& error);
    void syncLayoutToGif(const ProjectSource& src, std::function<void(bool, const QString&)> onDone);
    void finishImportedPath(const QString& path, DropAction action);
    void openSettingsDialogForSection(SettingsDialog::Section section);

    /**
     * @brief Shows CLI installation overlay.
     */
    void showCliInstallOverlay();

    /**
     * @brief Hides CLI installation overlay.
     */
    void hideCliInstallOverlay();

    /**
     * @brief Updates CLI overlay geometry.
     */
    void updateCliOverlayGeometry();

    /**
     * @brief Sets up CLI installation overlay.
     */
    void setupCliInstallOverlay();

    /**
     * @brief Handles resize events.
     * 
     * @param event Resize event
     */
    void resizeEvent(QResizeEvent* event) override;

    /**
     * @brief Loads images from a ZIP file.
     * 
     * @param zipPath Path to ZIP file
     * @param action Action to take (Replace or Merge)
     * @return bool True if images were loaded successfully
     */
    bool loadImagesFromZip(const QString& zipPath, DropAction action = DropAction::Replace);

    /**
     * @brief Caches layout output from project payload.
     * 
     * @param payload Project payload
     */
    void cacheLayoutOutputFromPayload(const QJsonObject& payload);

    /**
     * @brief Refreshes handle combo box.
     */
    void refreshHandleCombo();
    void refreshSpriteTree();
    void updateNavigatorAtlasCombo();
    void syncExcludedAtlas();
    void addToExcludedFiles(const QString& absPath);
    void removeFromExcludedFiles(const QString& absPath);

    /**
     * @brief Updates the Aliases button label to reflect the current alias count.
     */
    void updateAliasesButton();

    /** Moves @p paths from atlas @p srcIdx to atlas @p tgtIdx.
     *  Normalizes paths, removes from source, deduplicates in target,
     *  handles excluded-atlas sync and cleans up source layoutModels.
     *  Does NOT emit atlasesChanged() or update the UI. */
    void moveAtlasSprites(const QStringList& paths, int srcIdx, int tgtIdx);

    // Navigator context menu helpers
    QStringList collectDescendantSpritePaths(QTreeWidgetItem* item) const;
    QString folderPathForTreeItem(QTreeWidgetItem* item) const;
    void onNavigatorDeleteFrames(const QStringList& paths);
    void onNavigatorExcludeKey(QTreeWidgetItem* item);
    void onNavigatorExcludeFromSmartFolder(const QString& absolutePath, int smartFolderIndex);
    void onNavigatorAddSmartFolder();
    void onNavigatorAddFrames(const QString& subfolder);
    void onNavigatorAddToTimeline(const QStringList& paths);
    void onNavigatorCreateTimeline(const QStringList& paths, QTreeWidgetItem* contextItem = nullptr);
    void onNavigatorCreateGroup(const QStringList& paths, const QString& parentFolder);
    void onNavigatorDeleteGroup(QTreeWidgetItem* groupItem);
    void onNavigatorHideGroupOnly(QTreeWidgetItem* groupItem);
    void onNavigatorUnhideGroup(int sourceIdx, const QString& relPath);
    void onNavigatorExcludeGroup(QTreeWidgetItem* groupItem);
    void onNavigatorReincludeFromSource(int sourceIdx, const QString& relPath);
    QString absolutePathForNavItem(QTreeWidgetItem* item) const;
    void onNavigatorAutoCreateTimelines(QTreeWidgetItem* parentGroup);
    void onNavigatorAutoCreateTimelinesForSource(int sourceIndex);

    void onSpritesDroppedToTimeline(const QStringList& paths, const QString& targetFolderPath);

    // Updates m_folderLabel text; appends "(watching)" when sync mode is Watch
    void updateFolderLabel(const QString& folder);

    /**
     * @brief Applies project payload to the UI.
     */
    void applyProjectPayload();

    /**
     * @brief Parses layout output.
     * 
     * @param output Layout output
     * @param folderPath Folder path
     * @return LayoutModel Parsed layout model
     */
    LayoutModel parseLayoutOutput(const QString& output, const QString& folderPath);

    /**
     * @brief Gets autosave file path.
     * 
     * @return QString Autosave file path
     */
    QString getAutosaveFilePath() const;

    /**
     * @brief Refreshes timeline list.
     */
    void refreshTimelineList();

public slots:
    /**
     * @brief Refreshes animation test.
     */
    void refreshAnimationTest();

    /**
     * @brief Fits the animation preview to the scroll area viewport.
     */
    void fitAnimationToViewport();

    /**
     * @brief Saves animation to file.
     */
    void saveAnimationToFile();

    /**
     * @brief Refreshes timeline frames.
     */
    void refreshTimelineFrames();

public:
    /**
     * @brief Exports animation to file.
     * 
     * @param outPath Output file path
     * @return bool True if export was successful
     */
    bool exportAnimation(const QString& outPath);

    /**
     * @brief Handles settings button click.
     */
    void onSettingsClicked();
    void onSettingsSpritesheetClicked();
    void onSettingsSpritesNavigatorClicked();
    void onSettingsFramesEditorClicked();
    void onSettingsAtlasLayoutClicked();
    void onSettingsExportationClicked();
    void onSettingsNineSliceClicked();
#ifndef Q_OS_WASM
    void onSettingsCliToolsClicked();
#endif

    /**
     * @brief Applies settings to the UI.
     */
    void applySettings();

    /**
     * @brief Updates ghost (onion skin) display in the preview canvas.
     */
    void updateOnionSkinDisplay();

    /**
     * @brief Shows Quick Start guide dialog.
     */
    void onQuickStart();

    /**
     * @brief Shows keyboard hotkeys reference dialog.
     */
    void onShowHotkeys();

    /**
     * @brief Shows About dialog.
     */
    void onAboutClicked();

    /**
     * @brief Loads an image file and performs frame detection.
     * 
     * @param imagePath Path to the image file
     * @param action Action to take (Replace or Merge)
     */
    void loadImageWithFrameDetection(const QString& imagePath, DropAction action, bool hideSingleFrame = false);

    /**
     * @brief Loads a tar file and extracts frames.
     * 
     * @param tarPath Path to the tar file
     * @param action Action to take (Replace or Merge)
     */
    void loadTarFile(const QString& tarPath, DropAction action);

    /**
     * @brief Handles layout for a single image used as a frame.
     *
     * @param imagePath Path to the image file
     * @param action Action to take (Replace or Merge)
     */
    void handleSingleImageLayout(const QString& imagePath, DropAction action = DropAction::Replace, const QColor& backgroundColor = QColor());

    /**
     * @brief Processes frames extracted to a temporary directory.
     * 
     * Lists files, sorts them numerically, and updates internal state.
     * 
     * @param tempPath Path to the temporary directory
     * @param sourcePath Original source path for reference
     * @param action Action to take (Replace or Merge)
     * @param backgroundColor Background color to make transparent (optional)
     * @return bool True if frames were found and processed
     */
    bool processExtractedFrames(const QString& tempPath, const QString& sourcePath, DropAction action = DropAction::Replace, const QColor& backgroundColor = QColor());

    /**
     * @brief Runs a CLI tool and captures its output.
     * 
     * @param tool Path to tool
     * @param args Arguments for the tool
     * @param input Optional input data for stdin
     * @param output Optional buffer to store stdout
     * @param error Optional buffer to store stderr
     * @return bool True if tool exited normally with code 0
     */
    bool runTool(const QString& tool, const QStringList& args, const QByteArray* input = nullptr, QByteArray* output = nullptr, QByteArray* error = nullptr);

    enum class LogLevel { Cli, Info, Warning, Error, Qt, Diagnosis };
    void appendLog(LogLevel level, const QString& text);

private:
    // === UI Components ===
    QStackedWidget* m_mainStack = nullptr;
    QWidget* m_welcomePage = nullptr;
    ExportWorkspace* m_exportWorkspace = nullptr;
    ExportCoordinator* m_exportCoordinator = nullptr;
    class AtlasesManagementWorkspace* m_atlasesManagementWorkspace = nullptr;
    AtlasWorkspace* m_atlasWorkspace = nullptr;
    IWorkspace* m_currentWorkspace   = nullptr;
    PackedAtlasView*           m_packedAtlasView          = nullptr;
    LayoutCanvas*              m_exportLayoutCanvas       = nullptr;
    QLabel* m_welcomeLabel = nullptr;
    QPushButton* m_recentProjectBtn = nullptr;
    QLabel* m_folderLabel = nullptr;

    QDockWidget* m_atlasDock = nullptr;
    QDockWidget* m_animationDock = nullptr;
    QDockWidget* m_debugDock = nullptr;
    QPlainTextEdit* m_logWidget = nullptr;
    QLineEdit*      m_logFilterEdit = nullptr;
    QPushButton*    m_logFilterCli = nullptr;
    QPushButton*    m_logFilterInfo = nullptr;
    QPushButton*    m_logFilterWarn = nullptr;
    QPushButton*    m_logFilterError = nullptr;
    QPushButton*    m_logFilterQt = nullptr;
    QPushButton*    m_logFilterDiag = nullptr;

    struct LogEntry { LogLevel level; QString text; };
    QVector<LogEntry> m_logEntries;
    QMenu* m_viewMenu = nullptr;

    FrameAnimationWorkspace* m_frameAnimWorkspace = nullptr;

    QAction*        m_atlasWorkspaceAction            = nullptr;
    QAction*        m_frameAnimWorkspaceAction        = nullptr;
    QAction*        m_exportationWorkspaceAction      = nullptr;
    QAction*        m_atlasesManagementWorkspaceAction = nullptr;
    NineSliceWorkspace* m_nineSliceWorkspace = nullptr;
    QAction*        m_nineSliceWorkspaceAction = nullptr;

    // Layout Canvas Area
    QStackedWidget* m_profileSelectorStack = nullptr;
    double          m_layoutZoom     = 100.0;
    int             m_savedAtlasDockW  = -1;
    int             m_savedAnimDockW   = -1;

    // Animation Test Area
    QFutureWatcher<bool> m_gifSyncWatcher;
    std::function<void(bool)> m_gifSyncOnDone;
    QFutureWatcher<QString> m_cliDiagnosticsWatcher;

    QAction* m_loadAction = nullptr;
    QAction* m_loadProjectAction = nullptr;
    QAction* m_addSourceFileAction = nullptr;
    QAction* m_addSourceUrlAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_exportAction = nullptr;
    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_statusProgressBar = nullptr;
    QToolButton* m_recentProjectsBtn = nullptr;
    QMenu* m_recentProjectsMenu = nullptr;

    // === Data Models ===
    ProjectSession* m_session = nullptr;

    CliSetupController* m_cliSetup = nullptr;
    QString m_spratLayoutBin;
    QString m_spratPackBin;
    QString m_spratConvertBin;
    QString m_spratFramesBin;
    QString m_spratUnpackBin;
    SourceFolderWatcher* m_folderWatcher = nullptr;
    QAction* m_openSourceFolderAction = nullptr;
    SyncMode m_appliedSyncMode = SyncMode::None;
#ifndef SPRAT_EMBEDDED_CLI
    QProcess* m_process;
#endif
    bool m_cliReady = false;
    bool m_isLoading = false;
    bool m_cliInstallInProgress = false;
    bool m_loadingOverlayVisible = false;
    std::atomic<bool> m_isCanceled{false};
    AppSettings m_settings;
    CliPaths m_cliPaths;
    SaveConfig m_lastSaveConfig;
    QWidget* m_cliInstallOverlay = nullptr;
    QLabel* m_cliInstallOverlayLabel = nullptr;
    QLabel* m_atlasDimsLabel = nullptr;
    QProgressBar* m_cliInstallProgress = nullptr;
    QPushButton* m_cancelLoadingButton = nullptr;
    QPlainTextEdit* m_cliInstallLog = nullptr;
    QString m_loadingUiMessage = "Loading...";
    bool m_mergeReplaceAllDuplicates = true;
    bool m_detectFramesHideSingleFrame = false;
    QTimer* m_watchModePeriodicCheckTimer = nullptr;

    // Temporary storage for transparency processing continuation
    QString m_pendingTransparencyTempPath;
    QString m_pendingTransparencySourcePath;
    QStringList m_pendingTransparencyFramePaths;
    DropAction m_pendingTransparencyAction;
    QColor m_pendingTransparencyBgColor;

    // === Undo/Redo & Recent Projects ===
    QUndoStack* m_undoStack = nullptr;
    QStringList m_recentProjects;

    // === Export Presets ===
    QVector<ExportPreset> m_exportPresets;

    // === IMainWindowUndoHost overrides ===
    void refreshUiAfterUndo() override;
    void setSourceFolderIsTemp(bool isTemp) override;

    void processProjectLoadResult(const ProjectController::ProjectLoadResult& result);
    void processZipDiscoveryResult(const ProjectController::ZipDiscoveryResult& result);
    void processFrameDetectionResult(const ProjectController::FrameDetectionTaskResult& result);
    void processTarExtractionResult(const ProjectController::TarExtractionResult& result);
    void processFrameExtractionResult(const ProjectController::FrameExtractionResult& result);

    QFutureWatcher<void> m_transparencyWatcher;  // For background transparency processing

    QMutex m_toolMutex;
    QWidget* m_canvasOverlay = nullptr;  // Semi-transparent overlay for canvas during loading
    QString m_currentProfile;
    QString m_currentResolution;

    // Layout orchestrator — owns all layout scheduling, animation, and profile fallback logic
    LayoutOrchestrator* m_layoutOrchestrator = nullptr;

    // Project controller — owns async watcher state, project-file path, and source-management helpers
    ProjectController* m_projectController = nullptr;
    QTimer* m_autosaveTimer = nullptr;
    QTimer* m_resizeDebounceTimer = nullptr;
    QSize m_pendingResizeSize;
    QSize m_pendingResizeOldSize;
    QSize m_singleImageDimensions;  // Cache to avoid redundant image loads
    bool m_inResize = false;
    bool m_isRestoringProject = false;

#ifdef Q_OS_WASM
    static MainWindow* s_wasmInstance;
#endif
};
