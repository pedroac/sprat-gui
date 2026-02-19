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
#include "ProfilesDialog.h"
#include "SpratProfilesConfig.h"
#include <QToolBar>
#include <QSplitter>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QStatusBar>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTimer>
#include <QPushButton>
#include <QDirIterator>
#include <QApplication>
#include <algorithm>
#include <QSet>
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
#include <QProgressBar>
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
#include <QFile>
#include <QTextStream>
#include <QIODevice>
#include <QTextEdit>
#include <QUuid>
#include <QDateTime>

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
    m_loadingOverlayDelayTimer = new QTimer(this);
    m_loadingOverlayDelayTimer->setSingleShot(true);

    connect(m_animTimer, &QTimer::timeout, this, &MainWindow::onAnimTimerTimeout);
    connect(m_loadingOverlayDelayTimer, &QTimer::timeout, this, [this]() {
        if (!m_isLoading || m_cliInstallInProgress || !m_cliInstallOverlay || !m_cliInstallOverlayLabel) {
            return;
        }
        m_cliInstallOverlayLabel->setText(m_loadingUiMessage);
        if (m_cliInstallProgress) {
            m_cliInstallProgress->hide();
        }
        m_cliInstallOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_cliInstallOverlay->show();
        m_cliInstallOverlay->raise();
        m_loadingOverlayVisible = true;
    });

    // Autosave setup
    m_autosaveTimer = new QTimer(this);
    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::onAutosaveTimer);
     m_autosaveTimer->start(300000); // Autosave every 5 minutes

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
    if (!m_frameListPath.isEmpty()) {
        QFile::remove(m_frameListPath);
        m_frameListPath.clear();
    }
    clearZipTempDir();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    updateCliOverlayGeometry();
}

/**
 * @brief Parses the output from spratlayout into a LayoutModel.
 */
LayoutModel MainWindow::parseLayoutOutput(const QString& output, const QString& folderPath) {
    return LayoutParser::parse(output, folderPath);
}

QString MainWindow::layoutParserFolder() const {
    if (m_layoutSourceIsList && !m_layoutSourcePath.isEmpty()) {
        return QFileInfo(m_layoutSourcePath).dir().absolutePath();
    }
    return m_layoutSourcePath;
}

bool MainWindow::ensureFrameListInput() {
    if (m_activeFramePaths.isEmpty()) {
        return false;
    }
    QString fileName = QString("sprat-gui-frames-%1.txt").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString newFrameListPath = QDir::temp().filePath(fileName);
    QFile file(newFrameListPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&file);
    for (const QString& path : m_activeFramePaths) {
        out << path << "\n";
    }
    out.flush();
    file.close();

    const QString oldFrameListPath = m_frameListPath;
    m_frameListPath = newFrameListPath;
    m_layoutSourcePath = m_frameListPath;
    m_layoutSourceIsList = true;
    if (!m_activeFramePaths.isEmpty()) {
        m_currentFolder = QFileInfo(m_activeFramePaths.first()).absoluteDir().absolutePath();
    }
    if (!oldFrameListPath.isEmpty() && oldFrameListPath != m_frameListPath) {
        QFile::remove(oldFrameListPath);
    }
    updateManualFrameLabel();
    return true;
}

void MainWindow::populateActiveFrameListFromModel() {
    m_activeFramePaths.clear();
    m_activeFramePaths.reserve(m_layoutModel.sprites.size());
    for (const auto& sprite : m_layoutModel.sprites) {
        m_activeFramePaths.append(sprite->path);
    }
}

void MainWindow::updateManualFrameLabel() {
    if (!m_folderLabel) {
        return;
    }
    if (m_activeFramePaths.isEmpty()) {
        m_folderLabel->setText(tr("Folder: none"));
    } else {
        m_folderLabel->setText(QString(tr("Frames: %1 (manual selection)")).arg(m_activeFramePaths.size()));
    }
}

void MainWindow::appendDebugLog(const QString& message) {
    if (!m_debugLogEdit) {
        return;
    }
    const QString stamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    m_debugLogEdit->append(QString("[%1] %2").arg(stamp, message));
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

    if (!effectiveProfiles.contains(m_lastSuccessfulProfile)) {
        m_lastSuccessfulProfile.clear();
    }
}

void MainWindow::onAddFramesRequested() {
    QString startDir = m_currentFolder;
    if (startDir.isEmpty() && !m_activeFramePaths.isEmpty()) {
        startDir = QFileInfo(m_activeFramePaths.first()).absoluteDir().absolutePath();
    }
    QString filter = tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tga *.dds)");
    QStringList files = QFileDialog::getOpenFileNames(this, tr("Add Frames"), startDir, filter);
    if (files.isEmpty()) {
        return;
    }

    QSet<QString> existing(m_activeFramePaths.begin(), m_activeFramePaths.end());
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

    const QStringList previousFramePaths = m_activeFramePaths;
    m_activeFramePaths.append(added);
    m_statusLabel->setText(QString(tr("Adding %1 frame(s)...")).arg(added.size()));
    if (!ensureFrameListInput()) {
        m_activeFramePaths = previousFramePaths;
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
        if (m_activeFramePaths.contains(path)) {
            targets.append(path);
        }
    }
    if (targets.isEmpty()) {
        return;
    }

    QSet<QString> timelineNames;
    for (const auto& timeline : m_timelines) {
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

    QStringList remainingFramePaths = m_activeFramePaths;
    for (const QString& path : targets) {
        remainingFramePaths.removeAll(path);
    }

    if (remainingFramePaths.isEmpty()) {
        bool timelineChanged = false;
        for (auto& timeline : m_timelines) {
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

        m_activeFramePaths.clear();
        m_layoutSourcePath.clear();
        m_layoutSourceIsList = false;
        if (!m_frameListPath.isEmpty()) {
            QFile::remove(m_frameListPath);
            m_frameListPath.clear();
        }
        m_layoutModel.sprites.clear();
        m_canvas->clearCanvas();
        m_selectedSprites.clear();
        m_selectedSprite.reset();
        m_statusLabel->setText(tr("No frames loaded"));
        m_folderLabel->setText(tr("Folder: none"));
        m_cachedLayoutOutput.clear();
        m_cachedLayoutScale = 1.0;
        updateMainContentView();
        updateUiState();
        return;
    }

    const QStringList previousFramePaths = m_activeFramePaths;
    m_activeFramePaths = remainingFramePaths;
    if (!ensureFrameListInput()) {
        m_activeFramePaths = previousFramePaths;
        QMessageBox::warning(this, tr("Remove Frames"), tr("Could not refresh the frame list after removal."));
        return;
    }

    bool timelineChanged = false;
    for (auto& timeline : m_timelines) {
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
        applySettings();
        CliPaths chosen = dlg.getCliPaths();
        CliToolsConfig::saveOverride("cli/spratlayout", chosen.layoutBinary);
        CliToolsConfig::saveOverride("cli/spratpack", chosen.packBinary);
        CliToolsConfig::saveOverride("cli/spratconvert", chosen.convertBinary);
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
    if (!m_layoutSourcePath.isEmpty() && m_profileCombo && !m_profileCombo->currentText().trimmed().isEmpty()) {
        onRunLayout();
    }
}


/**
 * @brief Applies application settings.
 */
void MainWindow::applySettings() {
    SettingsCoordinator::apply(m_settings, m_canvas, m_previewView, m_animPreviewLabel);
}
