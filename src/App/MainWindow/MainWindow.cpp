#include "MainWindow.h"
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
    m_session = new ProjectSession(this);
    m_settings = CliToolsConfig::loadAppSettings();
    m_cliPaths = CliToolsConfig::loadCliPaths();
    setupUi();
    setupCliInstallOverlay();
    setAcceptDrops(true);

    m_layoutRunner = new LayoutRunner(this);
    connect(m_layoutRunner, &LayoutRunner::finished, this, &MainWindow::onLayoutFinished);
    connect(m_layoutRunner, &LayoutRunner::errorOccurred, this, &MainWindow::onLayoutError);

    m_process = new QProcess(this);
    m_cliToolInstaller = new CliToolInstaller(this);
    connect(m_process, &QProcess::finished, this, &MainWindow::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &MainWindow::onProcessError);
    connect(m_cliToolInstaller, &CliToolInstaller::installFinished, this, &MainWindow::onInstallFinished);
    connect(m_cliToolInstaller, &CliToolInstaller::installStarted, this, &MainWindow::showCliInstallOverlay);
    connect(m_cliToolInstaller, &CliToolInstaller::downloadProgress, this, &MainWindow::onDownloadProgress);
    QTimer::singleShot(100, this, &MainWindow::checkCliTools);
    m_animTimer = new QTimer(this);
    connect(m_animTimer, &QTimer::timeout, this, &MainWindow::onAnimTimerTimeout);
    
    // Autosave setup
    m_autosaveTimer = new QTimer(this);
    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::onAutosaveTimer);
    m_autosaveTimer->start(300000); // Autosave every 5 minutes

    m_isRestoringProject = false;
}

/**
 * @brief Destroy the Main Window:: Main Window object
 * 
 */
MainWindow::~MainWindow() {
    if (m_autosaveTimer) {
        m_autosaveTimer->stop();
        delete m_autosaveTimer;
    }
    if (m_session && !m_session->frameListPath.isEmpty()) {
        QFile::remove(m_session->frameListPath);
        m_session->frameListPath.clear();
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    updateCliOverlayGeometry();
}

/**
 * @brief Parses the output from spratlayout into a LayoutModel.
 */
LayoutModel MainWindow::parseLayoutOutput(const QString& output, const QString& folderPath) {
    QVector<LayoutModel> models = LayoutParser::parse(output, folderPath);
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
        return false;
    }
    QTextStream out(&file);
    for (const QString& path : m_session->activeFramePaths) {
        out << path << "\n";
    }
    out.flush();
    file.close();

    const QString oldFrameListPath = m_session->frameListPath;
    m_session->frameListPath = newFrameListPath;
    m_session->layoutSourcePath = m_session->frameListPath;
    m_session->layoutSourceIsList = true;
    if (!m_session->activeFramePaths.isEmpty()) {
        m_session->currentFolder = QFileInfo(m_session->activeFramePaths.first()).absoluteDir().absolutePath();
    }
    if (!oldFrameListPath.isEmpty() && oldFrameListPath != m_session->frameListPath) {
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
    onRunLayout();
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

    m_statusLabel->setText(QString(tr("Removed %1 frame(s)")).arg(targets.size()));
    onRunLayout();
}

/**
 * @brief Opens the settings dialog.
 */
void MainWindow::onSettingsClicked() {
    SettingsDialog dlg(m_settings, m_cliPaths, this);
    connect(&dlg, &SettingsDialog::installCliToolsRequested, this, &MainWindow::installCliTools);
    if (dlg.exec() == QDialog::Accepted) {
        m_settings = dlg.getSettings();
        m_cliPaths = dlg.getCliPaths();
        applySettings();
        CliToolsConfig::saveAppSettings(m_settings, m_cliPaths);
        checkCliTools();
    }
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
        onRunLayout();
    }
}


/**
 * @brief Applies application settings.
 */
void MainWindow::applySettings() {
    if (m_canvas) {
        SettingsCoordinator::apply(m_settings, m_canvas, m_previewView, m_animCanvas);
    }
}

bool MainWindow::runTool(const QString& tool, const QStringList& args, const QByteArray* input, QByteArray* output, QByteArray* error) {
    QProcess process;
    process.setProgram(tool);
    process.setArguments(args);
    process.start();

    if (!process.waitForStarted()) {
        return false;
    }

    if (input && !input->isEmpty()) {
        process.write(*input);
        process.closeWriteChannel();
    }

    while (process.state() == QProcess::Running || process.bytesAvailable() > 0 || process.bytesToWrite() > 0) {
        if (process.waitForReadyRead(50)) {
            if (output) {
                output->append(process.readAllStandardOutput());
            } else {
                process.readAllStandardOutput(); // Drain it
            }
            if (error) {
                error->append(process.readAllStandardError());
            } else {
                process.readAllStandardError(); // Drain it
            }
        }
        
        // If we are writing and it's taking time, wait for bytes written
        if (process.bytesToWrite() > 0) {
            process.waitForBytesWritten(50);
        }

        QCoreApplication::processEvents();
        if (process.state() == QProcess::NotRunning && process.bytesAvailable() == 0) break;
    }

    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
