#include "MainWindow.h"
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
#include <QToolBar>
#include <QSplitter>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QStatusBar>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QStandardPaths>
#include <QTimer>
#include <QPushButton>
#include <QDirIterator>
#include <QApplication>
#include <algorithm>
#include <QStackedWidget>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QDoubleSpinBox>
#include <QScrollArea>
#include <QEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QProcess>
#include <QPixmap>
#include <QClipboard>
#include <QMenu>
#include <QAction>
#include <QShortcut>
#include <QPixmapCache>

Q_LOGGING_CATEGORY(mainWindow, "mainWindow")
Q_LOGGING_CATEGORY(cli, "cli")
Q_LOGGING_CATEGORY(project, "project")
Q_LOGGING_CATEGORY(autosave, "autosave")
/**
 * @brief Constructs the MainWindow.
 * 
 * Initializes the UI, processes, and timers.
 */
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setupUi();
    setupCliInstallOverlay();
    setAcceptDrops(true);
    m_process = new QProcess(this);
    m_cliToolInstaller = new CliToolInstaller(this);
    connect(m_process, &QProcess::finished, this, &MainWindow::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &MainWindow::onProcessError);
    connect(m_cliToolInstaller, &CliToolInstaller::installFinished, this, &MainWindow::onInstallFinished);
    connect(m_cliToolInstaller, &CliToolInstaller::installStarted, this, &MainWindow::showCliInstallOverlay);
    QTimer::singleShot(100, this, &MainWindow::checkCliTools);
    m_animTimer = new QTimer(this);

    connect(m_animTimer, &QTimer::timeout, this, &MainWindow::onAnimTimerTimeout);

    // Autosave setup
    m_autosaveTimer = new QTimer(this);
    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::onAutosaveTimer);
     m_autosaveTimer->start(300000); // Autosave every 5 minutes

}

MainWindow::~MainWindow() {
    if (m_autosaveTimer) {
        m_autosaveTimer->stop();
        delete m_autosaveTimer;
    }
    clearZipTempDir();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    updateCliOverlayGeometry();
}

/**
 * @brief Sets up the user interface components.
 */
/**
 * @brief Handles drag enter events for files (folders, zip, json).
 */
/**
 * @brief Handles drop events to load projects or folders.
 */
/**
 * @brief Checks if the required CLI tools are available.
 */
/**
 * @brief Resolves the paths to the CLI binaries.
 */
/**
 * @brief Loads a folder of images.
 */
/**
 * @brief Opens a dialog to select a folder to load.
 */
/**
 * @brief Runs the spratlayout CLI tool to generate the layout.
 */
/**
 * @brief Handles the completion of the layout process.
 */
/**
 * @brief Handles errors from the layout process.
 */
/**
 * @brief Sets the loading state of the UI.
 */
/**
 * @brief Updates the enabled state of UI controls based on application state.
 */
/**
 * @brief Updates the main content view (Welcome vs Editor).
 */
/**
 * @brief Parses the output from spratlayout into a LayoutModel.
 */
LayoutModel MainWindow::parseLayoutOutput(const QString& output, const QString& folderPath) {
    return LayoutParser::parse(output, folderPath);
}

/**
 * @brief Opens a dialog to load a project file.
 */
/**
 * @brief Handles the Save button click to export the project.
 */
/**
 * @brief Updates the UI when a sprite is selected.
 */
/**
 * @brief Handles zoom changes in the preview canvas.
 */
/**
 * @brief Handles changes in the pivot spin boxes.
 */
/**
 * @brief Handles pivot changes from the canvas interaction.
 */
/**
 * @brief Handles selection changes in the handle (pivot/marker) combobox.
 */
/**
 * @brief Opens the markers configuration dialog.
 */
/**
 * @brief Handles marker selection from the canvas overlay.
 */
/**
 * @brief Handles marker changes from the canvas overlay.
 */
/**
 * @brief Handles profile changes.
 */
/**
 * @brief Handles padding changes.
 */
/**
 * @brief Handles trim transparency toggle changes.
 */
void MainWindow::onTrimChanged() {
    onRunLayout();
}

/**
 * @brief Handles layout zoom changes.
 */
/**
 * @brief Adds a new animation timeline.
 */
/**
 * @brief Removes the selected timeline.
 */
/**
 * @brief Handles selection change in the timeline list.
 */
/**
 * @brief Handles timeline name changes.
 */
/**
 * @brief Refreshes the timeline list widget.
 */
/**
 * @brief Refreshes the list of frames in the selected timeline.
 */
/**
 * @brief Handles dropping a frame into a timeline.
 */
/**
 * @brief Handles moving a frame within a timeline.
 */
/**
 * @brief Handles duplicating a frame in the timeline.
 */
/**
 * @brief Handles removing selected frames from the timeline.
 */
/**
 * @brief Handles the previous frame button click.
 */
/**
 * @brief Handles the play/pause button click.
 */
/**
 * @brief Handles the next frame button click.
 */
/**
 * @brief Gets the autosave file path.
 */
/**
 * @brief Autosaves the project data.
 */
/**
 * @brief Handles the animation timer timeout.
 */
/**
 * @brief Exports the current animation to a file (GIF/Video).
 */
/**
 * @brief Opens a dialog to save the animation.
 */
/**
 * @brief Refreshes the animation preview widget.
 */
/**
 * @brief Refreshes the handle (pivot/marker) combobox.
 */
/**
 * @brief Loads a project from a JSON file or ZIP archive.
 */
/**
 * @brief Applies the loaded project data to the application state.
 */
/**
 * @brief Opens the settings dialog.
 */
void MainWindow::onSettingsClicked() {
    SettingsDialog dlg(m_settings, m_cliPaths, this);
    connect(&dlg, &SettingsDialog::installCliToolsRequested, this, &MainWindow::installCliTools);
    if (dlg.exec() == QDialog::Accepted) {
        m_settings = dlg.getSettings();
        applySettings();
        CliPaths chosen = dlg.getCliPaths();
        CliToolsConfig::saveOverride("cli/spratlayout", chosen.layoutBinary);
        CliToolsConfig::saveOverride("cli/spratpack", chosen.packBinary);
        CliToolsConfig::saveOverride("cli/spratconvert", chosen.convertBinary);
        checkCliTools();
    }
}


/**
 * @brief Applies application settings.
 */
void MainWindow::applySettings() {
    SettingsCoordinator::apply(m_settings, m_canvas, m_previewView, m_animPreviewLabel);
}


/**
 * @brief Called when the autosave timer times out.
 */
/**
 * @brief Called when CLI installation finishes.
 */
