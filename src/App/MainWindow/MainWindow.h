#pragma once
#include <QMainWindow>
#include <QWidget>
#include <QProcess>
#include <QTimer>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QIcon>
#include "LayoutCanvas.h"
#include "PreviewCanvas.h"
#include "TimelineListWidget.h"
#include "CliToolInstaller.h"
#include "SaveDialog.h"
#include "SpratProfilesConfig.h"
#include <QJsonObject>
#include "models.h"

// Forward declarations for Qt classes
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QLabel;
class QStackedWidget;
class QListWidget;
class QLineEdit;
class QGroupBox;
class QGraphicsView;
class QScrollArea;
class QPushButton;
class QSplitter;
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
class QAction;

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

    // === Project Management Events ===
    /**
     * @brief Handles loading of a folder with images.
     */
    void onLoadFolder();

    /**
     * @brief Handles loading of a saved project.
     */
    void onLoadProject();

    /**
     * @brief Handles running the layout generation process.
     */
    void onRunLayout();

    /**
     * @brief Handles when a process finishes execution.
     * 
     * @param exitCode Exit code of the process
     * @param exitStatus Exit status of the process
     */
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

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
     * @brief Handles errors from processes.
     * 
     * @param error Type of process error
     */
    void onProcessError(QProcess::ProcessError error);

    // === CLI Tool Management Events ===
    /**
     * @brief Handles when CLI tool installation finishes.
     * 
     * @param exitCode Exit code of the installation process
     * @param exitStatus Exit status of the installation process
     */
    void onInstallFinished(int exitCode, QProcess::ExitStatus exitStatus);

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

    /**
     * @brief Handles request to manage profiles.
     */
    void onManageProfiles();

    /**
     * @brief Handles changes to layout zoom level.
     * 
     * @param value New zoom level
     */
    void onLayoutZoomChanged(double value);

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
     * @return QJsonObject Project payload
     */
    QJsonObject buildProjectPayload(SaveConfig config);

    /**
     * @brief Handles autosave timer timeout.
     */
    void onAutosaveTimer();

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

private:
    // === Helper Methods ===
    /**
     * @brief Gets the layout parser folder path.
     * 
     * @return QString Layout parser folder path
     */
    QString layoutParserFolder() const;

    /**
     * @brief Ensures frame list input is valid.
     * 
     * @return bool True if input is valid
     */
    bool ensureFrameListInput();

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
     * @brief Checks if a profile is enabled.
     * 
     * @param profile Name of profile to check
     * @return bool True if profile is enabled
     */
    bool isProfileEnabled(const QString& profile) const;

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
     * @brief Opens dialog for CLI path configuration.
     */
    void openCliPathDialog();

    /**
     * @brief Installs CLI tools.
     */
    void installCliTools();

    /**
     * @brief Loads a folder with images.
     * 
     * @param path Path to folder
     * @param confirmReplace Whether to confirm replacement
     */
    void loadFolder(const QString& path, bool confirmReplace = true);

    /**
     * @brief Loads a saved project.
     * 
     * @param path Path to project file
     * @param confirmReplace Whether to confirm replacement
     */
    void loadProject(const QString& path, bool confirmReplace = true);

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
     * @param confirmReplace Whether to confirm replacement
     * @return bool True if path was handled
     */
    bool tryHandleDroppedPath(const QString& path, bool confirmReplace = true);

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
     * @param confirmReplace Whether to confirm replacement
     * @return bool True if images were loaded successfully
     */
    bool loadImagesFromZip(const QString& zipPath, bool confirmReplace = true);

    /**
     * @brief Clears temporary ZIP directory.
     */
    void clearZipTempDir();

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

    /**
     * @brief Refreshes animation test.
     */
    void refreshAnimationTest();

    /**
     * @brief Saves animation to file.
     */
    void saveAnimationToFile();

    /**
     * @brief Refreshes timeline frames.
     */
    void refreshTimelineFrames();

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

    /**
     * @brief Applies settings to the UI.
     */
    void applySettings();

    // === UI Components ===
    QStackedWidget* m_mainStack;
    QWidget* m_welcomePage;
    QLabel* m_welcomeLabel;
    QWidget* m_editorPage;
    QLabel* m_folderLabel;
    QSplitter* m_leftSplitter;
    QSplitter* m_rightSplitter;
    bool m_syncingSplitters;

    // Layout Canvas Area
    LayoutCanvas* m_canvas;
    QStackedWidget* m_profileSelectorStack = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QPushButton* m_addProfilesBtn = nullptr;
    QPushButton* m_manageProfilesBtn = nullptr;
    QComboBox* m_sourceResolutionCombo = nullptr;
    QDoubleSpinBox* m_layoutScaleSpin = nullptr;
    QDoubleSpinBox* m_layoutZoomSpin = nullptr;
    QTimer* m_sourceResolutionDebounceTimer = nullptr;

    // Timelines Area
    QLineEdit* m_timelineCreateEdit;
    QLineEdit* m_timelineNameEdit;
    QListWidget* m_timelineList;
    QWidget* m_timelineEditorContainer;
    QGroupBox* m_timelineDropArea;
    QLabel* m_timelineDragHintLabel;
    TimelineListWidget* m_timelineFramesList;
    QHash<QString, QIcon> m_timelineFrameIconCache;

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
    QSpinBox* m_animPaddingSpin = nullptr;
    QPushButton* m_animPrevBtn;
    QPushButton* m_animPlayPauseBtn;
    QPushButton* m_animNextBtn;
    QSpinBox* m_timelineFpsSpin;
    QLabel* m_animStatusLabel;
    QScrollArea* m_animPreviewScroll = nullptr;
    QLabel* m_animPreviewLabel;

    QAction* m_loadAction;
    QAction* m_saveAction;
    QLabel* m_statusLabel;

    // === Data Models ===
    LayoutModel m_layoutModel;
    QJsonObject m_pendingProjectPayload;
    QVector<AnimationTimeline> m_timelines;
    int m_selectedTimelineIndex = -1;
    SpritePtr m_selectedSprite;
    QList<SpritePtr> m_selectedSprites;
    QString m_selectedPointName;
    QString m_currentFolder;
    QString m_spratLayoutBin;
    QString m_spratPackBin;
    CliToolInstaller* m_cliToolInstaller;
    QString m_spratConvertBin;
    QProcess* m_process;
    bool m_cliReady = false;
    bool m_isLoading = false;
    QTimer* m_animTimer;
    QTimer* m_loadingOverlayDelayTimer = nullptr;
    int m_animFrameIndex = 0;
    bool m_animPlaying = false;
    bool m_animPreviewPanning = false;
    bool m_animPreviewSpacePressed = false;
    QPoint m_animPreviewLastMousePos;
    bool m_cliInstallInProgress = false;
    bool m_loadingOverlayVisible = false;
    bool m_forceImmediateLoadingOverlay = false;
    AppSettings m_settings;
    CliPaths m_cliPaths;
    QTemporaryDir* m_zipTempDir = nullptr;
    QWidget* m_cliInstallOverlay = nullptr;
    QLabel* m_cliInstallOverlayLabel = nullptr;
    QProgressBar* m_cliInstallProgress = nullptr;
    QString m_loadingUiMessage = "Loading...";
    QStringList m_activeFramePaths;
    QString m_layoutSourcePath;
    bool m_layoutSourceIsList = false;
    QString m_frameListPath;
    QString m_cachedLayoutOutput;
    double m_cachedLayoutScale = 1.0;
    QString m_lastSuccessfulProfile;
    QString m_runningLayoutProfile;
    bool m_lastRunUsedTrim = false;
    bool m_layoutRunPending = false;
    bool m_layoutFailureDialogShown = false;
    bool m_retryWithoutTrimOnFailure = false;
    QTimer* m_autosaveTimer = nullptr;
};
