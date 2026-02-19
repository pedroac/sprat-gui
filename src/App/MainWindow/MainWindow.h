#pragma once
#include <QMainWindow>
#include <QWidget>
#include <QProcess>
#include <QTimer>
#include <QStringList>
#include "LayoutCanvas.h"
#include "PreviewCanvas.h"
#include "TimelineListWidget.h"
#include "CliToolInstaller.h"
#include "SaveDialog.h"
#include <QJsonObject>
#include "models.h"

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
class QPushButton;
class QSplitter;
class QDragEnterEvent;
class QDropEvent;
class QWheelEvent;
class QContextMenuEvent;
class QResizeEvent;
class QTemporaryDir;
class QWidget;
class QProgressBar;
class QDockWidget;
class QTextEdit;
class QAction;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onLayoutCanvasPathDropped(const QString& path);
    void onAddFramesRequested();
    void onRemoveFramesRequested(const QStringList& paths);
    void onLoadFolder();
    void onLoadProject();
    void onRunLayout();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void autosaveProject();
    void loadAutosavedProject();
    void onSaveClicked();
    void onProcessError(QProcess::ProcessError error);
    void onInstallFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onSpriteSelected(SpritePtr sprite);
    void onPreviewZoomChanged(double value);
    void onPivotSpinChanged();
    void onCanvasPivotChanged(int x, int y);
    void onHandleComboChanged(int index);
    void onPointsConfigClicked();
    void onMarkerSelectedFromCanvas(const QString& name);
    void onMarkerChangedFromCanvas();
    void onProfileChanged();
    void onPaddingChanged();
    void onTrimChanged();
    void onLayoutZoomChanged(double value);
    void onTimelineAddClicked();
    void onTimelineRemoveClicked();
    void onTimelineSelectionChanged();
    void onTimelineNameChanged();
    void onFrameDropped(const QString& path, int index);
    void onFrameMoved(int from, int to);
    void onFrameRemoveRequested();
    void onFrameDuplicateRequested(int index);
    void onAnimPrevClicked();
    void onAnimPlayPauseClicked();
    void onGenerateTimelinesFromFrames();
    QJsonObject buildProjectPayload(SaveConfig config);
    void onAutosaveTimer();

    void onAnimNextClicked();
    void onAnimFpsChanged(int fps);
    void onAnimTimerTimeout();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QString layoutParserFolder() const;
    bool ensureFrameListInput();
    void populateActiveFrameListFromModel();
    void updateManualFrameLabel();
    void handleProfileFailure(const QString& failedProfile);
    bool isProfileEnabled(const QString& profile) const;

    void setupUi();
    void setupToolbar();
    void setupStatusBarUi();
    void setupZoomShortcuts();
    void checkCliTools();
    bool resolveCliBinaries(QStringList& missing);
    void showMissingCliDialog(const QStringList& missing);
    void setLoading(bool loading);
    void updateUiState();
    void updateMainContentView();
    void openCliPathDialog();
    void installCliTools();
    void loadFolder(const QString& path, bool confirmReplace = true);
    void loadProject(const QString& path, bool confirmReplace = true);
    bool confirmLayoutReplacement();
    bool saveProjectWithConfig(SaveConfig config);
    bool isSupportedDropPath(const QString& path) const;
    bool tryHandleDroppedPath(const QString& path, bool confirmReplace = true);
    bool handleAnimPreviewEvent(QEvent* event);
    void handleAnimPreviewMousePress();
    bool handleAnimPreviewWheel(QWheelEvent* wheelEvent);
    bool handleAnimPreviewContextMenu(QContextMenuEvent* contextEvent);
    void handleAnimPreviewResize();
    void showCliInstallOverlay();
    void hideCliInstallOverlay();
    void updateCliOverlayGeometry();
    void setupCliInstallOverlay();
    void resizeEvent(QResizeEvent* event) override;
    bool pickImageSubdirectory(const QString& root, QString& selection, bool* canceled = nullptr) const;
    bool hasImageFiles(const QString& path) const;
    bool loadImagesFromZip(const QString& zipPath, bool confirmReplace = true);
    void clearZipTempDir();
    void cacheLayoutOutputFromPayload(const QJsonObject& payload);
    void appendDebugLog(const QString& message);
    void refreshHandleCombo();
    void applyProjectPayload();
    LayoutModel parseLayoutOutput(const QString& output, const QString& folderPath);
    QString getAutosaveFilePath() const;
    void refreshTimelineList();
    void refreshAnimationTest();
    void saveAnimationToFile();
    void refreshTimelineFrames();
    bool exportAnimation(const QString& outPath);
    QTimer* m_autosaveTimer = nullptr;
    void onSettingsClicked();
    void applySettings();

    // UI Components
    QStackedWidget* m_mainStack;
    QWidget* m_welcomePage;
    QLabel* m_welcomeLabel;
    QWidget* m_editorPage;
    QLabel* m_folderLabel;
    QSplitter* m_leftSplitter;
    QSplitter* m_rightSplitter;
    bool m_syncingSplitters = false;

    // Layout Canvas Area
    LayoutCanvas* m_canvas;
    QComboBox* m_profileCombo;
    QSpinBox* m_paddingSpin;
    QCheckBox* m_trimCheck;
    QDoubleSpinBox* m_layoutZoomSpin;

    // Timelines Area
    QLineEdit* m_timelineCreateEdit;
    QLineEdit* m_timelineNameEdit;
    QListWidget* m_timelineList;
    QWidget* m_timelineEditorContainer;
    QGroupBox* m_timelineDropArea;
    QLabel* m_timelineDragHintLabel;
    TimelineListWidget* m_timelineFramesList;

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
    QSpinBox* m_fpsSpin;
    QLabel* m_animStatusLabel;
    QLabel* m_animPreviewLabel;

    QAction* m_loadAction;
    QAction* m_saveAction;
    QAction* m_showDebugAction = nullptr;
    QLabel* m_statusLabel;
    QDockWidget* m_debugDock = nullptr;
    QTextEdit* m_debugLogEdit = nullptr;
    
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
    bool m_layoutRunPending = false;
};
