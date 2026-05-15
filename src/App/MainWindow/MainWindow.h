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
#include <QMutex>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <memory>
#include <vector>

#include "LayoutCanvas.h"
#include "PreviewCanvas.h"
#include "TimelineListWidget.h"
#include "CliToolInstaller.h"
#include "LayoutRunner.h"
#include "SourceFolderWatcher.h"
#include "ProjectSession.h"
#include "SaveDialog.h"
#include "SpratProfilesConfig.h"
#include "models.h"
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
class NavigatorTreeWidget;
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
class QTextEdit;
class QPlainTextEdit;
class QAction;
class QToolButton;
class QMenu;
class QUndoStack;
class QMimeData;
class QImage;
class QUrl;

// Forward declarations for custom classes
class FrameDetectionDialog;
class AnimationCanvas;
class SourceFolderWatcher;

/**
 * @class MainWindow
 * @brief Main application window for sprat-gui.
 * 
 * This class manages the entire user interface and coordinates
 * between different components including layout canvas, timeline editor,
 * sprite editor, and animation preview.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    enum class DropAction {
        Replace,
        Merge,
        Cancel
    };

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
    // === Layout Canvas Events ===
    /**
     * @brief Handles when a path is dropped onto the layout canvas.
     * 
     * @param path Path that was dropped
     */
    void onLayoutCanvasPathDropped(const QString& path);

    /**
     * @brief Handles request to add frames to the layout.
     */
    void onAddFramesRequested();

    /**
     * @brief Handles request to remove frames from the layout.
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
     * @brief Handles when layout process finishes execution.
     * 
     * @param result Result of the layout process
     */
    void onLayoutFinished(const LayoutResult& result);

    /**
     * @brief Handles errors from layout process.
     * 
     * @param error Description of the error
     */
    void onLayoutError(const QString& error);

    /**
     * @brief Handles autosave of the current project.
     */
    void autosaveProject();

    /**
     * @brief Handles loading of an autosaved project.
     */
    void loadAutosavedProject();

    /**
     * @brief Handles save action from the user.
     */
    void onSaveClicked();

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

    // === CLI Tool Management Events ===
    /**
     * @brief Handles when CLI tool installation finishes.
     *
     * @param exitCode Exit code of the installation process
     * @param exitStatus Exit status of the installation process
     */
#ifndef SPRAT_EMBEDDED_CLI
    void onInstallFinished(int exitCode, QProcess::ExitStatus exitStatus);
#else
    void onInstallFinished(int exitCode, int exitStatus);
#endif

    /**
     * @brief Handles download progress of CLI tools.
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

    // === Sprite Editor Events ===
    /**
     * @brief Handles changes to preview zoom level.
     * 
     * @param value New zoom level
     */
    void onPreviewZoomChanged(double value);

    /**
     * @brief Handles changes to pivot spin controls.
     */
    void onPivotSpinChanged();

    /**
     * @brief Handles changes to canvas pivot position.
     * 
     * @param x New X position
     * @param y New Y position
     */
    void onCanvasPivotChanged(int x, int y);

    /**
     * @brief Handles changes to handle combo box selection.
     * 
     * @param index Selected index
     */
    void onHandleComboChanged(int index);

    /**
     * @brief Handles request to configure points.
     */
    void onPointsConfigClicked();

    /**
     * @brief Handles marker selection from canvas.
     * 
     * @param name Name of selected marker
     */
    void onMarkerSelectedFromCanvas(const QString& name);

    /**
     * @brief Handles changes to markers from canvas.
     */
    void onMarkerChangedFromCanvas();

    /**
     * @brief Handles applying pivot to selected timeline frames.
     */
    void onApplyPivotToSelectedTimelineFrames();

    /**
     * @brief Handles applying marker to selected timeline frames.
     * 
     * @param markerName Name of marker to apply
     */
    void onApplyMarkerToSelectedTimelineFrames(const QString& markerName);

    // === Profile Management Events ===
    /**
     * @brief Handles changes to profile selection.
     */
    void onProfileChanged();
    void onLayoutZoomChanged(double value);

    /**
     * @brief Handles request to manage profiles.
     */
    void onManageProfiles();

    // === Timeline Management Events ===
    /**
     * @brief Handles adding a new timeline.
     */
    void onTimelineAddClicked();

    /**
     * @brief Handles removing the current timeline.
     */
    void onTimelineRemoveClicked();

    /**
     * @brief Handles changes to timeline selection.
     */
    void onTimelineSelectionChanged();

    /**
     * @brief Handles changes to timeline name.
     */
    void onTimelineNameChanged();

    /**
     * @brief Handles when a frame is dropped onto the timeline.
     * 
     * @param path Path of dropped frame
     * @param index Index where frame was dropped
     */
    void onFrameDropped(const QString& path, int index);

    /**
     * @brief Handles when a frame is moved within the timeline.
     * 
     * @param from Original index
     * @param to New index
     */
    void onFrameMoved(int from, int to);

    /**
     * @brief Handles request to remove selected frames.
     */
    void onFrameRemoveRequested();

    /**
     * @brief Handles request to duplicate a frame.
     * 
     * @param index Index of frame to duplicate
     */
    void onFrameDuplicateRequested(int index);

    /**
     * @brief Handles changes to timeline frame selection.
     */
    void onTimelineFrameSelectionChanged();

    // === Animation Events ===
    /**
     * @brief Handles changes to animation zoom level.
     * 
     * @param value New zoom level
     */
    void onAnimZoomChanged(double value);

    /**
     * @brief Handles previous frame button click.
     */
    void onAnimPrevClicked();

    /**
     * @brief Handles play/pause button click.
     */
    void onAnimPlayPauseClicked();

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
    QJsonObject buildProjectPayload(SaveConfig config, ProjectSession* session);

    /**
     * @brief Handles autosave timer timeout.
     */
    void onAutosaveTimer();

    /**
     * @brief Handles cancel button click on loading overlay.
     */
    void onCancelLoading();

    /**
     * @brief Handles next frame button click.
     */
    void onAnimNextClicked();

    /**
     * @brief Handles changes to timeline FPS.
     * 
     * @param fps New frames per second
     */
    void onTimelineFpsChanged(int fps);

    /**
     * @brief Handles animation timer timeout.
     */
    void onAnimTimerTimeout();
    void onPasteImport();

private:
    struct ProjectSaveResult;

private slots:
    // === Undo/Redo ===
    void onUndo();
    void onRedo();

    // === Asynchronous Loading Slots ===
    void onFolderDiscoveryFinished();
    void onProjectLoadFinished();
    void onZipDiscoveryFinished();
    void onFrameDetectionFinished();
    void onTarExtractionFinished();
    void onFrameExtractionFinished();
    void onProjectSaveFinished();
    void handleProjectSaveResult(const ProjectSaveResult& result);

    // === CLI Installation Logging ===
    void onCliInstallLog(const QString& message);

    // === Source Folder Sync ===
    void onSpriteTreeContextMenu(const QPoint& pos);
    void onFolderWatcherFilesAdded(const QStringList& paths);
    void onFolderWatcherFilesRemoved(const QStringList& paths);
    void onFolderWatcherFilesModified(const QStringList& paths);
    void onSyncNowRequested();
    void performFolderSync();
    void onOpenSourceFolderClicked();
    void ensureSourceFolder();             // creates temp dir if needed
    void promoteSourceFolderAfterSave(const QString& saveDestination);
    void updateOpenSourceFolderAction();   // enable/disable based on state
    void onWatchModePeriodicCheck();       // periodic file removal detection in Watch mode

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
     * Call this instead of onRunLayout() for user-triggered changes.
     * If the Navigator view is active, sets m_layoutDirty and defers
     * the rebuild until the user switches back to Layout view.
     */
    void scheduleLayoutRebuild();

    /**
     * @brief Gets the layout parser folder path.
     */
    QString layoutParserFolder() const;

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
    bool activeFramesAreInSourceFolder() const;

    /**
     * @brief Copies active frames to the source folder.
     *
     * @param overwriteDuplicates If true, replace existing files with the same name; if false, rename to avoid conflicts
     */
    void copyActiveFramesToSourceFolder(bool overwriteDuplicates = true);

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
    QVector<SpratProfile> configuredProfiles();

    /**
     * @brief Gets the selected profile definition.
     * 
     * @param out Reference to store profile definition
     * @return bool True if profile was found
     */
    bool selectedProfileDefinition(SpratProfile& out) const;

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
     * @brief Handles profile failure.
     *
     * @param failedProfile Name of failed profile
     */
    void handleProfileFailure(const QString& failedProfile);

    /**
     * @brief Handles sprite dimensions exceed error with auto-retry.
     *
     * @param failedProfile Name of failed profile
     */
    void handleDimensionsError(const QString& failedProfile);

    /**
     * @brief Checks if a profile is enabled.
     * 
     * @param profile Name of profile to check
     * @return bool True if profile is enabled
     */
    bool isProfileEnabled(const QString& profile) const;

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
     * @brief Syncs pivot spin boxes with current sprite pivot.
     */
    void syncPivotSpinsFromSprite();

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
     * @brief Checks CLI tools availability.
     */
    void checkCliTools();

    /**
     * @brief Resolves CLI binaries and checks for missing ones.
     * 
     * @param missing List to store missing binaries
     * @return bool True if all binaries are resolved
     */
    bool resolveCliBinaries(QStringList& missing);

    /**
     * @brief Shows dialog for missing CLI tools.
     * 
     * @param missing List of missing binaries
     */
    void showMissingCliDialog(const QStringList& missing);
    void showCliExecutionError(const QString& tool);

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
     * @brief Installs CLI tools.
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
     * @brief Saves project with given configuration.
     * 
     * @param config Save configuration
     * @return bool True if save was successful
     */
    bool saveProjectWithConfig(SaveConfig config);

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
    void finishImportedPath(const QString& path, DropAction action);
    void openSettingsDialogForSection(SettingsDialog::Section section);

    /**
     * @brief Handles animation preview events.
     * 
     * @param event Event to handle
     * @return bool True if event was handled
     */
    bool handleAnimPreviewEvent(QEvent* event);

    /**
     * @brief Handles mouse press events in animation preview.
     * 
     * @param mouseEvent Mouse event
     * @return bool True if event was handled
     */
    bool handleAnimPreviewMousePress(QMouseEvent* mouseEvent);

    /**
     * @brief Handles mouse move events in animation preview.
     * 
     * @param mouseEvent Mouse event
     * @return bool True if event was handled
     */
    bool handleAnimPreviewMouseMove(QMouseEvent* mouseEvent);

    /**
     * @brief Handles mouse release events in animation preview.
     * 
     * @param mouseEvent Mouse event
     * @return bool True if event was handled
     */
    bool handleAnimPreviewMouseRelease(QMouseEvent* mouseEvent);

    /**
     * @brief Handles key press events in animation preview.
     * 
     * @param keyEvent Key event
     * @return bool True if event was handled
     */
    bool handleAnimPreviewKeyPress(QKeyEvent* keyEvent);

    /**
     * @brief Handles key release events in animation preview.
     * 
     * @param keyEvent Key event
     * @return bool True if event was handled
     */
    bool handleAnimPreviewKeyRelease(QKeyEvent* keyEvent);

    /**
     * @brief Handles wheel events in animation preview.
     * 
     * @param wheelEvent Wheel event
     * @return bool True if event was handled
     */
    bool handleAnimPreviewWheel(QWheelEvent* wheelEvent);

    /**
     * @brief Handles context menu events in animation preview.
     * 
     * @param contextEvent Context menu event
     * @return bool True if event was handled
     */
    bool handleAnimPreviewContextMenu(QContextMenuEvent* contextEvent);

    /**
     * @brief Handles resize events in animation preview.
     */
    void handleAnimPreviewResize();

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

    // Navigator context menu helpers
    QStringList collectCheckedSpritePaths() const;
    QStringList collectDescendantSpritePaths(QTreeWidgetItem* item) const;
    QString folderPathForTreeItem(QTreeWidgetItem* item) const;
    void onNavigatorDeleteFrames(const QStringList& paths);
    void onNavigatorAddFrames(const QString& subfolder);
    void onNavigatorAddToTimeline(const QStringList& paths);
    void onNavigatorCreateTimeline(const QStringList& paths, QTreeWidgetItem* contextItem = nullptr);
    void onNavigatorCreateGroup(const QStringList& paths, const QString& parentFolder);
    void onNavigatorDeleteGroup(QTreeWidgetItem* groupItem);
    void onNavigatorUngroup(QTreeWidgetItem* groupItem);
    void onNavigatorAutoCreateTimelines(QTreeWidgetItem* parentGroup);

    // Helper: Check for duplicate timeline names
    bool hasDuplicateTimelineName(const QString& timelineName) const;

    // Helper: Get unique timeline name (with path)
    QString getUniqueTimelineName(const QString& baseName, const QString& folderPath = QString());

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
    void onSettingsStylesClicked();
    void onSettingsSpritesheetClicked();
    void onSettingsCliToolsClicked();

    /**
     * @brief Applies settings to the UI.
     */
    void applySettings();

    /**
     * @brief Loads an image file and performs frame detection.
     * 
     * @param imagePath Path to the image file
     * @param action Action to take (Replace or Merge)
     */
    void loadImageWithFrameDetection(const QString& imagePath, DropAction action);

    /**
     * @brief Loads a tar file and extracts frames.
     * 
     * @param tarPath Path to the tar file
     * @param action Action to take (Replace or Merge)
     */
    void loadTarFile(const QString& tarPath, DropAction action);

    struct FrameDetectionResult {
        QVector<QRect> frames;
        QColor backgroundColor;
    };

    /**
     * @brief Detects frames in an image using spratframes.
     * 
     * @param imagePath Path to the image file
     * @return FrameDetectionResult Detected frames and background color
     */
    FrameDetectionResult detectFramesInImage(const QString& imagePath);

    /**
     * @brief Generates spratframes format from frame rectangles.
     * 
     * @param frames Frame rectangles
     * @param imagePath Path to the source image
     * @return QString spratframes format string
     */
    QString generateSpratFramesFormat(const QVector<QRect>& frames, const QString& imagePath);

    /**
     * @brief Handles layout for a single image used as a frame.
     * 
     * @param imagePath Path to the image file
     * @param action Action to take (Replace or Merge)
     */
    void handleSingleImageLayout(const QString& imagePath, DropAction action = DropAction::Replace, const QColor& backgroundColor = QColor());
    void applyTransparencyToImage(QImage& image, const QColor& backgroundColor);

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

    void appendCliLog(const QString& text);

private:
    // === UI Components ===
    QStackedWidget* m_mainStack;
    QWidget* m_welcomePage;
    QLabel* m_welcomeLabel;
    QLabel* m_folderLabel;

    QDockWidget* m_atlasDock = nullptr;
    QDockWidget* m_animationDock = nullptr;
    QDockWidget* m_debugDock = nullptr;
    QPlainTextEdit* m_cliLog = nullptr;
    QMenu* m_viewMenu = nullptr;

    // Atlas view stack (Layout / Navigator)
    QStackedWidget* m_atlasViewStack      = nullptr;
    NavigatorTreeWidget* m_spriteTree      = nullptr;
    QAction*        m_showLayoutAction    = nullptr;
    QAction*        m_showNavigatorAction = nullptr;

    // Layout Canvas Area
    LayoutCanvas* m_canvas = nullptr;
    QStackedWidget* m_profileSelectorStack = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QPushButton* m_addProfilesBtn = nullptr;
    QPushButton* m_manageProfilesBtn = nullptr;
    QComboBox* m_sourceResolutionCombo = nullptr;
    QDoubleSpinBox* m_layoutZoomSpin = nullptr;
    QTimer* m_sourceResolutionDebounceTimer = nullptr;

    // Timelines Area
    QLineEdit* m_timelineCreateEdit;
    QLineEdit* m_timelineNameEdit;
    QListWidget* m_timelineList;
    QWidget* m_timelineEditorContainer;
    QGroupBox* m_selectedTimelineGroup;
    QWidget* m_timelineDropArea;
    QLabel* m_timelineDragHintLabel;
    TimelineListWidget* m_timelineFramesList;
    QHash<QString, QIcon> m_timelineFrameIconCache;
    QHash<QString, QIcon> m_timelineListIconCache;

    // Selected Frame Editor Area
    QLineEdit* m_spriteNameEdit;
    QLabel* m_multiSelectionLabel;
    QComboBox* m_handleCombo;
    QPushButton* m_configPointsBtn;
    QSpinBox* m_pivotXSpin;
    QSpinBox* m_pivotYSpin;
    QDoubleSpinBox* m_previewZoomSpin;
    PreviewCanvas* m_previewView;

    // Animation Test Area
    QDoubleSpinBox* m_animZoomSpin;
    QPushButton* m_animPrevBtn;
    QPushButton* m_animPlayPauseBtn;
    QPushButton* m_animNextBtn;
    QSpinBox* m_timelineFpsSpin;
    QLabel* m_animStatusLabel;
    AnimationCanvas* m_animCanvas = nullptr;

    QAction* m_loadAction;
    QAction* m_saveAction;
    QLabel* m_statusLabel;
    QProgressBar* m_statusProgressBar = nullptr;
    QToolButton* m_recentProjectsBtn = nullptr;
    QMenu* m_recentProjectsMenu = nullptr;

    // === Data Models ===
    ProjectSession* m_session;
    
    CliToolInstaller* m_cliToolInstaller;
    QString m_spratLayoutBin;
    QString m_spratPackBin;
    QString m_spratConvertBin;
    QString m_spratFramesBin;
    QString m_spratUnpackBin;
    LayoutRunner* m_layoutRunner;
    SourceFolderWatcher* m_folderWatcher;
    QAction* m_openSourceFolderAction = nullptr;
    QString  m_projectFilePath;            // path of the last loaded project file
    bool     m_sourceFolderIsTemp = false; // true when sourceFolder is a QTemporaryDir
    SyncMode m_appliedSyncMode = SyncMode::None;
#ifndef SPRAT_EMBEDDED_CLI
    QProcess* m_process;
#endif
    bool m_cliReady = false;
    bool m_isLoading = false;
    QTimer* m_animTimer;
    int m_animFrameIndex = 0;
    bool m_animPlaying = false;
    bool m_cliInstallInProgress = false;
    bool m_loadingOverlayVisible = false;
    std::atomic<bool> m_isCanceled{false};
    AppSettings m_settings;
    CliPaths m_cliPaths;
    SaveConfig m_lastSaveConfig;
    QWidget* m_cliInstallOverlay = nullptr;
    QLabel* m_cliInstallOverlayLabel = nullptr;
    QProgressBar* m_cliInstallProgress = nullptr;
    QPushButton* m_cancelLoadingButton = nullptr;
    QPlainTextEdit* m_cliInstallLog = nullptr;
    QString m_loadingUiMessage = "Loading...";
    bool m_shouldClearSpritesFolder = false;
    bool m_mergeReplaceAllDuplicates = true;
    QTimer* m_watchModePeriodicCheckTimer = nullptr;

    // === Undo/Redo & Recent Projects ===
    QUndoStack* m_undoStack = nullptr;
    QStringList m_recentProjects;

    // === Async Loading Helpers ===
    struct FolderDiscoveryResult {
        QString root;
        QStringList directories;
        MainWindow::DropAction action;
    };
    QFutureWatcher<FolderDiscoveryResult> m_folderDiscoveryWatcher;
    void processFolderDiscoveryResult(const FolderDiscoveryResult& result);

    struct ProjectLoadResult {
        QString path;
        QJsonObject root;
        QString error;
        MainWindow::DropAction action;
        bool success;
    };
    QFutureWatcher<ProjectLoadResult> m_projectLoadWatcher;

    struct ZipDiscoveryResult {
        QString tempPath;
        QString zipPath;
        QStringList selections;
        MainWindow::DropAction action;
        bool canceled;
        QString error;
    };
    QFutureWatcher<ZipDiscoveryResult> m_zipDiscoveryWatcher;
    void processZipDiscoveryResult(const ZipDiscoveryResult& result);

    struct FrameDetectionTaskResult {
        QString imagePath;
        MainWindow::DropAction action;
        FrameDetectionResult detection;
    };
    QFutureWatcher<FrameDetectionTaskResult> m_frameDetectionWatcher;
    void processFrameDetectionResult(const FrameDetectionTaskResult& result);

    struct TarExtractionResult {
        QString tempPath;
        QString tarPath;
        MainWindow::DropAction action;
        bool success;
    };
    QFutureWatcher<TarExtractionResult> m_tarExtractionWatcher;
    void processTarExtractionResult(const TarExtractionResult& result);

    struct FrameExtractionResult {
        QString tempPath;
        QString sourcePath;
        MainWindow::DropAction action;
        QColor backgroundColor;
        bool success;
    };
    QFutureWatcher<FrameExtractionResult> m_frameExtractionWatcher;
    void processFrameExtractionResult(const FrameExtractionResult& result);

    struct ProjectSaveResult {
        QString savedDestination;
        QString error;
        bool success;
        bool canceled = false;
    };
    QFutureWatcher<ProjectSaveResult> m_projectSaveWatcher;
    QNetworkAccessManager* m_importNetworkManager = nullptr;
    QPointer<QNetworkReply> m_activeImportReply;
    std::vector<std::unique_ptr<QTemporaryDir>> m_importTempDirs;

    QMutex m_toolMutex;
    QString m_runningLayoutProfile;
    bool m_layoutRunPending = false;
    bool m_layoutRunPendingQuiet = false;  // quiet flag for deferred pending run
    bool m_layoutDirty = false;            // rebuild needed but Navigator view is active
    int m_pendingChangeCount = 0;
    static constexpr int kLayoutBufferFullThreshold = 20;
    QTimer* m_layoutDebounceTimer = nullptr;
    bool m_layoutFailureDialogShown = false;
    bool m_retryWithoutTrimOnFailure = false;
    QTimer* m_autosaveTimer = nullptr;
    QTimer* m_resizeDebounceTimer = nullptr;
    QSize m_pendingResizeSize;
    QSize m_pendingResizeOldSize;
    bool m_inResize = false;
    bool m_isRestoringProject = false;

#ifdef Q_OS_WASM
    static MainWindow* s_wasmInstance;
#endif
};
