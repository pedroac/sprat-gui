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
#include <QElapsedTimer>
#include <QUndoStack>
#include <memory>
#include <vector>

#include "LayoutCanvas.h"
#include "PreviewCanvas.h"
#include "TimelineListWidget.h"
#include "SourceFolderWatcher.h"
#include "ProjectSession.h"
#include "ExportWorkspace.h"
#include "SpratProfilesConfig.h"
#include "models.h"
#include "SettingsDialog.h"
#include "NavigatorPanel.h"
#include "FrameAnimationWorkspace.h"
#include "SpriteEditorPanel.h"
#include "AnimationPreviewPanel.h"
#include "TimelineEditorPanel.h"

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
#include "TimelineTreeWidget.h"
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
class QUndoStack;
class QVariantAnimation;
class QMimeData;
class QImage;
class QUrl;
class QSize;

// Forward declarations for custom classes
class FrameDetectionDialog;
class AnimationCanvas;
class SourceFolderWatcher;
class ElidedLabel;
class PackedAtlasView;
class LayoutOrchestrator;
class CliSetupController;
#include "ProjectController.h"
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
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    enum class DropAction {
        Replace,
        Merge,
        Cancel
    };

    enum class Workspace { Atlas, FrameAnimation, Exportation, AtlasesManagement };

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

    void showExportWorkspace();
    void leaveExportWorkspace();
    void onExportWorkspaceRequested(SaveConfig config);
    void schedulePreviewPack(const QString& profileName, const QString& scaleFilter);
    void runPreviewPack();
    void onPreviewPackFinished();
    void showAtlasesManagementWorkspace();
    void leaveAtlasesManagementWorkspace();
    void switchToAtlasWorkspace();
    void switchToFrameAnimWorkspace();

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
     * @brief Handles changes to the coordinate unit combo box (px / %).
     */
    void onCoordUnitChanged();

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
    QJsonObject buildProjectPayload(SaveConfig config, ProjectSession* session, bool portable = false);

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
     * @brief Creates an alias of the currently selected timeline.
     */
    void onTimelineCreateAlias();

    /**
     * @brief Handles changes to the flip combo box for alias timelines.
     *
     * @param index Selected flip mode index
     */
    void onTimelineFlipChanged(int index);

    /**
     * @brief Handles animation timer timeout.
     */
    void onAnimTimerTimeout();
    void onPasteImport();

private:
    struct ExportResult;
    struct PackPreviewResult;

private slots:
    // === Undo/Redo ===
    void onUndo();
    void onRedo();

    // === Sprite Name ===
    void onSpriteNameEditingFinished();
    void onEditAliases();

    // === Asynchronous Loading Slots ===
    void onProjectLoadFinished();
    void onZipDiscoveryFinished();
    void onFrameDetectionFinished();
    void onTarExtractionFinished();
    void onFrameExtractionFinished();
    void onExportFinished();
    void handleExportResult(const ExportResult& result);
    void handlePackPreviewResult(const PackPreviewResult& result);
    void onTransparencyProcessingFinished();

    // === CLI Installation Logging ===
    void onCliInstallLog(const QString& message);

    // === Sources ===
    void removeSource(int index);
    void onSyncSourceRequested(int sourceIndex);
    void onSyncLayoutRequested(int sourceIndex);

    // === Source Folder Sync ===
    void onSpriteTreeContextMenu(const QPoint& pos);
    void filterSpriteTree(const QString& text);
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
    QString layoutParserFolder() const;

    /**
     * @brief Returns a sanitized subfolder name for a source being merged.
     *
     * Derived from the pending import URL (if set) or the basename of sourcePath.
     * The returned name is guaranteed unique among existing source subfolders.
     */
    QString computeSourceSubfolderName(const QString& sourcePath) const;

    /**
     * @brief Returns baseName made unique among the existing source names.
     *
     * If a source named baseName already exists, suffixes _2, _3, … are tried
     * until a free slot is found.
     */
    QString makeUniqueSourceName(const QString& baseName) const;

    /**
     * @brief Copies frames into sourceFolder/<subfolderPath>, returns the new destination paths.
     *
     * Preserves relative structure relative to m_session->currentFolder.
     */
    QStringList copyFramesToSourceSubfolder(const QStringList& frames,
                                            const QString& subfolderPath,
                                            bool overwriteDuplicates = true);

    /**
     * @brief Registers a newly loaded archive, image, or URL as a ProjectSource.
     *
     * If a pending import URL is set on the ProjectController, it takes precedence
     * over sourcePath (URL case). For Replace, existing sources are cleared first.
     * For Merge, the new source is appended. cachedFolderPath, when provided,
     * overrides the default (sourceFolder for Replace, empty for Merge).
     */
    void registerLoadedSource(const QString& sourcePath, DropAction action,
                              const QString& cachedFolderPath = QString());

    /**
     * @brief Routes activeFramePaths that have no atlas owner into the neutral atlas.
     * On Replace, clears the neutral atlas sprite paths first.
     */
    void syncFramePathsToNeutralAtlas(DropAction action);

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
     * @brief Checks if the source folder contains exactly the current active frames.
     *
     * @return bool True if the source folder image set matches the active frame list
     */
    bool sourceFolderMatchesActiveFrames() const;

    /**
     * @brief Returns whether removing sprites should also remove their backing files.
     */
    bool shouldDeleteRemovedSpritesFromSource() const;

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
     * @brief Syncs coordinate spin boxes from the currently selected pivot or marker.
     */
    void syncCoordinateSpinsFromSelection();

    /**
     * @brief Returns the editable coordinate-space size for a sprite.
     */
    QSize spriteCoordinateSpaceSize(const SpritePtr& sprite) const;

    /**
     * @brief Clears any pending typed coordinate override for the spin boxes.
     */
    void clearCoordinateFieldOverride();

    /**
     * @brief Stores the currently typed coordinate values for the active selection.
     */
    void storeCoordinateFieldOverride();

    /**
     * @brief Whether a typed coordinate override should be shown instead of recalculated values.
     */
    bool coordinateFieldOverrideApplies() const;

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
     * @brief Runs the export pipeline with the given configuration.
     *
     * @param config Save configuration
     * @return bool True if export was started successfully
     */
    bool runExport(SaveConfig config);

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
    bool syncLayoutToGif(const ProjectSource& src, QString& error);
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
    QStringList collectCheckedSpritePaths() const;
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

    // Timeline tree context menu / key / drop
    void onTimelineContextMenu(const QPoint& pos);
    void onTimelineDeleteKey();
    void onTimelineTreeDropCompleted(int draggedIndex,
                                     const QString& draggedFolder,
                                     const QString& targetFolder);
    void onTimelineItemChanged(QTreeWidgetItem* item, int column);

    // Updates m_folderLabel text; appends "(watching)" when sync mode is Watch
    void updateFolderLabel(const QString& folder);

    // Helper: Check for duplicate timeline names
    bool hasDuplicateTimelineName(const QString& timelineName) const;

    // Helper: Get unique timeline name (with path)
    QString getUniqueTimelineName(const QString& baseName, const QString& folderPath = QString());

    // Timeline tree helpers
    QString timelineItemFolderPath(QTreeWidgetItem* item) const;
    QVector<int> collectCheckedTimelineIndices() const;

    // Helper: Returns the tree item for a given m_session->timelines index, or nullptr if not found.
    QTreeWidgetItem* timelineItemForIndex(int timelineIndex) const;

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
    void onSettingsFramesEditorClicked();
    void onSettingsAtlasLayoutClicked();
    void onSettingsExportationClicked();
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
     * @brief Selects the sprite corresponding to the current animation frame index.
     */
    void syncSelectedSpriteToAnimFrame();

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
    void loadImageWithFrameDetection(const QString& imagePath, DropAction action);

    /**
     * @brief Loads a tar file and extracts frames.
     * 
     * @param tarPath Path to the tar file
     * @param action Action to take (Replace or Merge)
     */
    void loadTarFile(const QString& tarPath, DropAction action);

    // detectFramesInImage, generateSpratFramesFormat, applyTransparencyToImage moved to ProjectController

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
    ExportWorkspace* m_exportWorkspace = nullptr;
    bool m_exportWorkspaceActive = false;
    bool       m_frameAnimFirstLoad     = true;
    QList<int> m_atlasSplitterHSizes;   // saved before orientation→Vertical; restored on return
    double     m_savedPreviewZoom = -1.0;
    QPointF    m_savedPreviewCenter;    // scene-coord center saved alongside zoom
    class AtlasesManagementWorkspace* m_atlasesManagementWorkspace = nullptr;
    bool m_atlasesManagementWorkspaceActive = false;
    PackedAtlasView*           m_packedAtlasView          = nullptr;
    LayoutCanvas*              m_exportLayoutCanvas       = nullptr;
    struct PackPreviewResult {
        QByteArray imageData;
        QString    errorMsg;
        QByteArray layoutUsed;      // empty on cache hit; used to update cache
        QString    scaleFilterUsed;
        int        dilateUsed = -1;
        QVector<LayoutModel> layoutModels;  // populated on non-cache-hit runs; empty otherwise
    };
    QFutureWatcher<PackPreviewResult> m_previewPackWatcher;
    QTimer*                    m_previewPackDebounceTimer  = nullptr;
    std::atomic<bool>          m_previewPackCanceled{false};
    QString                    m_previewPackProfile;
    QString                    m_previewPackScaleFilter;
    QByteArray                 m_cachedPackedImage;
    QByteArray                 m_cachedPackLayout;
    QString                    m_cachedPackScaleFilter;
    int                        m_cachedPackDilate = -1;
    std::shared_ptr<std::atomic<bool>> m_previewPackLayoutUpdateCanceled;
    QVector<LayoutModel>       m_cachedPackModels;           // layout from last successful preview pack
    QString                    m_cachedPackModelsProfile;    // profile name the cached models were built for
    int                        m_exportPreviewAtlasIndex = -1; // -1 = all atlases
    Workspace m_activeWorkspace = Workspace::Atlas;
    QLabel* m_welcomeLabel;
    QPushButton* m_recentProjectBtn;
    QLabel* m_folderLabel;

    QDockWidget* m_atlasDock = nullptr;
    QSplitter*   m_atlasSplitter = nullptr;
    QDockWidget* m_animationDock = nullptr;
    QDockWidget* m_debugDock = nullptr;
    QPlainTextEdit* m_cliLog = nullptr;
    QTabWidget*     m_debugTabs = nullptr;
    QPlainTextEdit* m_cliInfoText = nullptr;
    QMenu* m_viewMenu = nullptr;

    // Navigator panel (wraps tree + filter + atlas combo)
    NavigatorPanel* m_navigatorPanel      = nullptr;
    FrameAnimationWorkspace* m_frameAnimWorkspace = nullptr;
    // Sprite editor panel (wraps selected-sprite controls + preview canvas)
    SpriteEditorPanel* m_spriteEditorPanel = nullptr;
    // Animation preview panel (wraps animation canvas + playback controls)
    AnimationPreviewPanel* m_animPreviewPanel = nullptr;
    // Timeline editor panel (wraps timeline list + selected-timeline editor)
    TimelineEditorPanel* m_timelineEditorPanel = nullptr;

    // Atlas view stack (Layout / Navigator)
    QStackedWidget* m_atlasViewStack      = nullptr;
    QWidget*        m_editorContent       = nullptr;  // Frame editor panel (right slot of atlasSplitter)
    NavigatorTreeWidget* m_spriteTree      = nullptr;
    QLineEdit*      m_spriteFilterEdit    = nullptr;
    QLabel*         m_spriteFilterResultLabel = nullptr;
    QCheckBox*      m_showHiddenToggleBtn = nullptr;
    bool            m_showHiddenItems     = false;
    QWidget*        m_navigatorAtlasRow   = nullptr;  // Atlas combo row (FrameAnim workspace only)
    QComboBox*      m_navigatorAtlasCombo = nullptr;
    QAction*        m_atlasWorkspaceAction            = nullptr;
    QAction*        m_frameAnimWorkspaceAction        = nullptr;
    QAction*        m_exportationWorkspaceAction      = nullptr;
    QAction*        m_atlasesManagementWorkspaceAction = nullptr;

    // Layout Canvas Area
    LayoutCanvas* m_canvas = nullptr;
    QStackedWidget* m_profileSelectorStack = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QPushButton* m_addProfilesBtn = nullptr;
    QComboBox* m_sourceResolutionCombo = nullptr;
    double          m_layoutZoom     = 100.0;
    QTimer* m_sourceResolutionDebounceTimer = nullptr;

    // Timelines Area
    QLineEdit* m_timelineCreateEdit;
    QLineEdit* m_timelineNameEdit;
    TimelineTreeWidget* m_timelineList;
    QWidget* m_timelineEditorContainer;
    QWidget* m_selectedTimelineGroup = nullptr;
    QWidget* m_timelineDropArea;
    QLabel* m_timelineDragHintLabel;
    TimelineListWidget* m_timelineFramesList;
    QHash<QString, QIcon> m_timelineFrameIconCache;
    QHash<QString, QIcon> m_timelineListIconCache;

    // Alias UI
    QLabel*    m_timelineAliasLabel  = nullptr;
    QLabel*    m_timelineFlipLabel   = nullptr;
    QComboBox* m_timelineFlipCombo   = nullptr;

    // Selected Frame Editor Area
    QLineEdit*   m_spriteNameEdit   = nullptr;
    QPushButton* m_editAliasesBtn   = nullptr;
    QLabel* m_multiSelectionLabel = nullptr;
    QComboBox* m_handleCombo;
    QPushButton* m_configPointsBtn;
    QDoubleSpinBox* m_pivotXSpin;
    QDoubleSpinBox* m_pivotYSpin;
    QComboBox* m_coordUnitCombo = nullptr;
    QDoubleSpinBox* m_previewZoomSpin;
    PreviewCanvas* m_previewView;

    struct CoordinateFieldOverride {
        bool active = false;
        const Sprite* sprite = nullptr;
        QString markerName;
        CoordUnit unit = CoordUnit::Pixels;
        bool showTrimRect = false;
        double x = 0.0;
        double y = 0.0;
    };
    CoordinateFieldOverride m_coordinateFieldOverride;

    // Animation Test Area
    QDoubleSpinBox* m_animZoomSpin;
    QPushButton* m_animPrevBtn;
    QPushButton* m_animPlayPauseBtn;
    QPushButton* m_animNextBtn;
    QToolButton* m_animOverlayBtn  = nullptr;  // Toggles pivot/marker overlay on animation canvas
    QSpinBox* m_timelineFpsSpin;
    QLabel* m_animStatusLabel;
    AnimationCanvas* m_animCanvas = nullptr;

    QAction* m_loadAction;
    QAction* m_loadProjectAction = nullptr;
    QAction* m_addSourceFileAction = nullptr;
    QAction* m_addSourceUrlAction = nullptr;
    QAction* m_saveAction;
    QAction* m_saveAsAction = nullptr;
    QAction* m_exportAction = nullptr;
    QLabel* m_statusLabel;
    QProgressBar* m_statusProgressBar = nullptr;
    QToolButton* m_recentProjectsBtn = nullptr;
    QMenu* m_recentProjectsMenu = nullptr;

    // === Data Models ===
    ProjectSession* m_session;

    CliSetupController* m_cliSetup = nullptr;
    QString m_spratLayoutBin;
    QString m_spratPackBin;
    QString m_spratConvertBin;
    QString m_spratFramesBin;
    QString m_spratUnpackBin;
    SourceFolderWatcher* m_folderWatcher = nullptr;
    QAction* m_openSourceFolderAction = nullptr;
    // m_projectFilePath and m_sourceFolderIsTemp moved to ProjectController
    SyncMode m_appliedSyncMode = SyncMode::None;
#ifndef SPRAT_EMBEDDED_CLI
    QProcess* m_process;
#endif
    bool m_cliReady = false;
    bool m_isLoading = false;
    QTimer* m_animTimer;
    int m_animFrameIndex = 0;
    bool m_animPlaying = false;
    QElapsedTimer m_animElapsed; // wall-clock time since last rendered frame
    bool m_cliInstallInProgress = false;
    bool m_loadingOverlayVisible = false;
    std::atomic<bool> m_isCanceled{false};
    AppSettings m_settings;
    CliPaths m_cliPaths;
    SaveConfig m_lastSaveConfig;
    QWidget* m_cliInstallOverlay = nullptr;
    QLabel* m_cliInstallOverlayLabel = nullptr;
    QLabel* m_atlasDimsLabel = nullptr;
    ElidedLabel* m_spriteNameFooterLabel = nullptr;
    QLabel* m_spriteDimsLabel = nullptr;
    QProgressBar* m_cliInstallProgress = nullptr;
    QPushButton* m_cancelLoadingButton = nullptr;
    QPlainTextEdit* m_cliInstallLog = nullptr;
    QString m_loadingUiMessage = "Loading...";
    // m_shouldClearSpritesFolder moved to ProjectController
    bool m_mergeReplaceAllDuplicates = true;
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

    // === Async Loading Helpers (watchers moved to ProjectController) ===

    void processProjectLoadResult(const ProjectController::ProjectLoadResult& result);
    void processZipDiscoveryResult(const ProjectController::ZipDiscoveryResult& result);

public:
    struct SessionUndoState {
        QString currentFolder;
        QString layoutSourcePath;
        bool layoutSourceIsList = false;
        QString sourceFolder;
        QVector<ProjectSource> sources;
        QStringList activeFramePaths;
        QString frameListPath;
        QVector<AtlasEntry> atlases;
        int activeAtlasIndex = 0;
        QString cachedLayoutOutput;
        double cachedLayoutScale = 1.0;
        QString lastSuccessfulProfile;
        bool lastRunUsedTrim = false;
        int selectedTimelineIndex = -1;
        QString selectedPointName;
        QStringList selectedSpritePaths;
        QString primarySelectedSpritePath;
        bool sourceFolderIsTemp = false;
    };

    struct PendingSessionUndoCommand {
        QString text;
        SessionUndoState before;
    };

    SessionUndoState captureSessionUndoState() const;
    void applySessionUndoState(const SessionUndoState& state);
    void pushSessionUndoCommand(const QString& text,
                                const SessionUndoState& before,
                                const SessionUndoState& after,
                                bool alreadyApplied = true);
    void beginPendingSessionUndoCommand(const QString& text);
    void finalizePendingSessionUndoCommand();
    void discardPendingSessionUndoCommand();
    bool hasMeaningfulLayout(const SessionUndoState& state) const;

private:

    std::optional<PendingSessionUndoCommand> m_pendingSessionUndoCommand;
    void processFrameDetectionResult(const ProjectController::FrameDetectionTaskResult& result);
    void processTarExtractionResult(const ProjectController::TarExtractionResult& result);
    void processFrameExtractionResult(const ProjectController::FrameExtractionResult& result);

    struct ExportResult {
        QString savedDestination;
        QString error;
        bool success;
        bool canceled = false;
    };
    QFutureWatcher<ExportResult> m_exportWatcher;
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
    // hidden folders are now stored per-source in ProjectSource::hiddenFolders

#ifdef Q_OS_WASM
    static MainWindow* s_wasmInstance;
#endif
};
