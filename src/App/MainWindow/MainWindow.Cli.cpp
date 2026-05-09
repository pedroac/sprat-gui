#include "MainWindow.h"
#ifdef Q_OS_WASM
#include "WasmFileDialog.h"
#endif
#include "AnimationCanvas.h"

#include "CliToolsConfig.h"
#include "CliToolsUi.h"
#include "ImageDiscoveryService.h"
#include "ImageFolderSelectionDialog.h"

#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QElapsedTimer>
#include <QDebug>
#include <QMessageBox>
#include <QProgressDialog>
#include <QProgressBar>
#include <QPushButton>
#include <QtConcurrent>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QScrollBar>

#include "ViewUtils.h"

void MainWindow::checkCliTools() {
#ifdef Q_OS_WASM
    if (jsIsAsyncBusy()) {
        QTimer::singleShot(100, this, &MainWindow::checkCliTools);
        return;
    }
#endif
    static bool inCheck = false;
    if (inCheck) return;
    inCheck = true;

    QStringList missing;
    bool allFound = resolveCliBinaries(missing);
    
    if (allFound) {
        QString currentVersion = CliToolsConfig::checkBinaryVersion(m_cliPaths.layoutBinary);
        QString requiredVersion = SPRAT_CLI_VERSION;

        if (currentVersion.isEmpty()) {
            m_cliReady = false;
            m_statusLabel->setText(tr("CLI error (failed to execute layout)"));
            showCliExecutionError("spratlayout");
            inCheck = false;
            return;
        }

        // Verify other binaries can also execute (might be missing DLLs for some but not all)
        if (CliToolsConfig::checkBinaryVersion(m_cliPaths.packBinary).isEmpty()) {
            showCliExecutionError("spratpack");
            inCheck = false;
            return;
        }
        if (CliToolsConfig::checkBinaryVersion(m_cliPaths.convertBinary).isEmpty()) {
            showCliExecutionError("spratconvert");
            inCheck = false;
            return;
        }
        if (CliToolsConfig::checkBinaryVersion(m_cliPaths.framesBinary).isEmpty()) {
            showCliExecutionError("spratframes");
            inCheck = false;
            return;
        }
        if (CliToolsConfig::checkBinaryVersion(m_cliPaths.unpackBinary).isEmpty()) {
            showCliExecutionError("spratunpack");
            inCheck = false;
            return;
        }

        if (currentVersion != requiredVersion) {
            if (CliToolsUi::askUpgrade(this, currentVersion, requiredVersion)) {
                m_cliReady = false;
                updateUiState();
                installCliTools();
                inCheck = false;
                return;
            }
            // User declined upgrade; version is confirmed readable, persist it
            CliToolsConfig::saveInstalledCliVersion(currentVersion);
        }
        m_cliReady = true;
        CliToolsConfig::saveInstalledCliVersion(currentVersion);
        m_statusLabel->setText(tr("CLI ready (%1)").arg(currentVersion));
    } else {
        m_cliReady = false;
        m_statusLabel->setText(tr("CLI missing"));
#ifdef Q_OS_WIN
        QDir appDir(QCoreApplication::applicationDirPath());
        if (!appDir.exists("cli")) {
            m_statusLabel->setText(tr("CLI folder missing"));
        }
#endif
        showMissingCliDialog(missing);
    }
    updateUiState();
    inCheck = false;
}

bool MainWindow::resolveCliBinaries(QStringList& missing) {
#ifdef SPRAT_EMBEDDED_CLI
    m_spratLayoutBin = "/spratlayout";
    m_spratPackBin = "/spratpack";
    m_spratConvertBin = "/spratconvert";
    m_spratFramesBin = "/spratframes";
    m_spratUnpackBin = "/spratunpack";
    return true;
#else
    m_cliPaths.layoutBinary = CliToolsConfig::resolveBinary("spratlayout", m_cliPaths.baseDir);
    if (m_cliPaths.layoutBinary.isEmpty()) {
        missing << "spratlayout";
    }

    m_cliPaths.packBinary = CliToolsConfig::resolveBinary("spratpack", m_cliPaths.baseDir);
    if (m_cliPaths.packBinary.isEmpty()) {
        missing << "spratpack";
    }

    m_cliPaths.convertBinary = CliToolsConfig::resolveBinary("spratconvert", m_cliPaths.baseDir);
    if (m_cliPaths.convertBinary.isEmpty()) {
        missing << "spratconvert";
    }

    m_cliPaths.framesBinary = CliToolsConfig::resolveBinary("spratframes", m_cliPaths.baseDir);
    if (m_cliPaths.framesBinary.isEmpty()) {
        missing << "spratframes";
    }

    m_cliPaths.unpackBinary = CliToolsConfig::resolveBinary("spratunpack", m_cliPaths.baseDir);
    if (m_cliPaths.unpackBinary.isEmpty()) {
        missing << "spratunpack";
    }

    m_spratLayoutBin = m_cliPaths.layoutBinary;
    m_spratPackBin = m_cliPaths.packBinary;
    m_spratConvertBin = m_cliPaths.convertBinary;
    m_spratFramesBin = m_cliPaths.framesBinary;
    m_spratUnpackBin = m_cliPaths.unpackBinary;

    return missing.isEmpty();
#endif
}

void MainWindow::showCliExecutionError(const QString& tool) {
    m_cliReady = false;
    m_statusLabel->setText(tr("CLI error (failed to execute %1)").arg(tool));
    QMessageBox::critical(this, tr("CLI Execution Failed"),
        tr("The CLI tool '%1' was found but failed to execute.\n"
           "This is usually caused by missing dependencies like 'archive.dll' or 'zlib1.dll'.\n"
           "Please ensure all required DLLs are in the 'cli' folder.").arg(tool));
}

void MainWindow::showMissingCliDialog(const QStringList& missing) {
    MissingCliAction action = CliToolsUi::askMissingCliAction(this, missing);
    if (action == MissingCliAction::Install) {
        installCliTools();
    } else if (action == MissingCliAction::ProvidePath) {
        QString dir = QFileDialog::getExistingDirectory(
            this, tr("Select CLI Tools Folder"), QDir::homePath());
        if (dir.isEmpty()) {
            m_statusLabel->setText(tr("CLI missing"));
            QApplication::quit();
            return;
        }
        m_cliPaths.baseDir = dir;
        CliToolsConfig::saveAppSettings(CliToolsConfig::loadAppSettings(), m_cliPaths);
        QStringList stillMissing;
        if (resolveCliBinaries(stillMissing)) {
            checkCliTools();
        } else {
            QMessageBox::warning(this, tr("Tools Not Found"),
                tr("Some CLI tools were not found in the selected folder:\n%1")
                .arg(stillMissing.join(", ")));
            m_statusLabel->setText(tr("CLI missing"));
            QApplication::quit();
        }
    } else {
        m_statusLabel->setText(tr("CLI missing"));
        QApplication::quit();
    }
}

void MainWindow::installCliTools() {
    if (!m_cliToolInstaller) {
        return;
    }
    m_statusLabel->setText(tr("Installing CLI tools..."));
    m_cliToolInstaller->installCliTools();
}

void MainWindow::loadFolder(const QString& path, DropAction action) {
    if (action == DropAction::Replace && !confirmLayoutReplacement()) {
        return;
    }
    if (action == DropAction::Cancel) {
        return;
    }

    if (m_folderDiscoveryWatcher.isRunning()) {
        return;
    }

    m_loadingUiMessage = tr("Scanning folder...");
    setLoading(true);

    auto discoveryTask = [path, action]() {
        QElapsedTimer timer;
        timer.start();
        qInfo() << "[WASM] loadFolder scan start path=" << path;
        FolderDiscoveryResult result;
        result.root = path;
        result.action = action;
        result.directories = ImageDiscoveryService::imageDirectoriesOneLevel(path);
        qInfo() << "[WASM] loadFolder scan done path=" << path
                << "dirs=" << result.directories.size()
                << "ms=" << timer.elapsed();
        return result;
    };

#ifdef Q_OS_WASM
    // QtConcurrent can be unreliable without pthreads/COOP+COEP; run synchronously.
    processFolderDiscoveryResult(discoveryTask());
#else
    m_folderDiscoveryWatcher.setFuture(QtConcurrent::run(discoveryTask));
#endif
}

void MainWindow::onFolderDiscoveryFinished() {
    processFolderDiscoveryResult(m_folderDiscoveryWatcher.result());
}

void MainWindow::processFolderDiscoveryResult(const FolderDiscoveryResult& result) {
    const QString path = result.root;
    const DropAction action = result.action;
    const QStringList directories = result.directories;

    QString targetPath = path;
    if (directories.isEmpty()) {
        setLoading(false);
        QMessageBox::warning(this, tr("Load Failed"), tr("No image files found in the selected folder."));
        return;
    }

    if (directories.size() > 1) {
        // We still have to show a dialog, which is blocking, but at least the scan happened async.
        // For a better UX, we could have a custom non-blocking dialog, but this is a good first step.
        QDir base(path);
        QStringList labels;
        labels.reserve(directories.size());
        for (const QString& dirPath : directories) {
            labels.append(base.relativeFilePath(dirPath));
        }

        bool ok = false;
        const QString chosen = QInputDialog::getItem(
            this,
            tr("Select frame folder"),
            tr("Folders with images:"),
            labels,
            0,
            false,
            &ok);
        
        if (!ok) {
            m_statusLabel->setText(tr("Load canceled"));
            setLoading(false);
            return;
        }

        const int index = labels.indexOf(chosen);
        if (index >= 0 && index < directories.size()) {
            targetPath = directories[index];
        }
    } else {
        targetPath = directories.first();
    }
    
    m_loadingUiMessage = tr("Loading images...");
    setLoading(true);

    QElapsedTimer loadTimer;
    loadTimer.start();
    const QStringList absolutePaths = ImageDiscoveryService::imagesInDirectory(targetPath);
    qInfo() << "[WASM] loadFolder imagesInDirectory done"
            << "count=" << absolutePaths.size()
            << "ms=" << loadTimer.elapsed();
    if (action == DropAction::Merge) {
        m_session->activeFramePaths.append(absolutePaths);
    } else {
        m_projectFilePath.clear();
        m_sourceFolderIsTemp = false;
        m_session->currentFolder = targetPath;
        m_folderLabel->setText(tr("Folder: ") + QDir(targetPath).absolutePath());
        m_session->layoutSourcePath = QDir(targetPath).absolutePath();
        m_session->layoutSourceIsList = false;
        m_session->cachedLayoutOutput.clear();
        m_session->cachedLayoutScale = 1.0;
        m_session->activeFramePaths = absolutePaths;
    }
    
    if (!m_session->frameListPath.isEmpty()) {
        QFile::remove(m_session->frameListPath);
        m_session->frameListPath.clear();
    }
    
    const int imageCount = absolutePaths.size();
    QProgressDialog progress(tr("Loading image frames..."), tr("Cancel"), 0, imageCount, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(1000);
    progress.setAutoClose(true);
    progress.setAutoReset(true);

    qInfo() << "[WASM] loadFolder loading frames start"
            << "count=" << imageCount;
    for (int i = 0; i < absolutePaths.size(); ++i) {
        const QString absolutePath = absolutePaths[i];
        progress.setValue(i);
        progress.setLabelText(QString("%1: %2\n%3").arg(tr("Loading"), QFileInfo(absolutePath).fileName(), absolutePath));
        m_statusLabel->setText(tr("Loading frame: ") + absolutePath);
#ifndef Q_OS_WASM
        QApplication::processEvents();
#endif
        if (progress.wasCanceled()) {
            m_statusLabel->setText(tr("Frame loading canceled"));
            setLoading(false);
            return;
        }
    }
    progress.setValue(imageCount);
    qInfo() << "[WASM] loadFolder loading frames done"
            << "ms=" << loadTimer.elapsed();
    m_statusLabel->setText(QString(tr("Loaded %1 image frame(s) from %2")).arg(absolutePaths.size()).arg(QDir(targetPath).absolutePath()));

    onRunLayout();
}

bool MainWindow::confirmLayoutReplacement() {
    bool hasLayout = !m_session->layoutModels.isEmpty() && m_session->layoutModels.first().sprites.size() > 0;
    if (!hasLayout) {
        return true;
    }
    QMessageBox msg(this);
    msg.setWindowTitle(tr("Layout Already Loaded"));
    msg.setText(tr("A layout is already loaded. Do you want to replace it?"));
    QPushButton* replaceBtn = msg.addButton(tr("Replace"), QMessageBox::AcceptRole);
    msg.addButton(tr("Ignore"), QMessageBox::RejectRole);
    msg.exec();
    return msg.clickedButton() == replaceBtn;
}

void MainWindow::onLoadFolder() {
#ifdef Q_OS_WASM
    wasmOpenFileDialog(true);
    return;
#endif
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Frames Folder"));
    if (!dir.isEmpty()) {
        if (m_animCanvas) m_animCanvas->setZoomManual(false);
        loadFolder(dir);
    }
}

void MainWindow::onInstallFinished(int exitCode,
#ifndef SPRAT_EMBEDDED_CLI
    QProcess::ExitStatus exitStatus
#else
    int exitStatus
#endif
) {
    hideCliInstallOverlay();
#ifndef SPRAT_EMBEDDED_CLI
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
#else
    if (exitCode == 0) {
#endif
        m_statusLabel->setText(tr("CLI installation finished"));
        checkCliTools();
        return;
    }
    m_statusLabel->setText(tr("CLI installation failed"));

    // Show error dialog with logs
    QMessageBox errorDialog(this);
    errorDialog.setWindowTitle(tr("CLI Installation Failed"));
    errorDialog.setIcon(QMessageBox::Critical);
    errorDialog.setText(tr("Could not install CLI tools automatically."));

    // Get the installation logs
    QString logContent;
    if (m_cliInstallLog) {
        logContent = m_cliInstallLog->toPlainText();
    }

    if (!logContent.isEmpty()) {
        errorDialog.setDetailedText(logContent);
    }

    errorDialog.exec();
}

void MainWindow::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (m_cliInstallProgress) {
        if (bytesTotal > 0) {
            m_cliInstallProgress->setRange(0, 100);
            m_cliInstallProgress->setValue(static_cast<int>(bytesReceived * 100 / bytesTotal));
            m_cliInstallOverlayLabel->setText(QString(tr("Downloading CLI tools (%1%)..."))
                .arg(m_cliInstallProgress->value()));
        } else {
            m_cliInstallProgress->setRange(0, 0);
            m_cliInstallOverlayLabel->setText(tr("Downloading CLI tools..."));
        }
    }
}

void MainWindow::onCliInstallLog(const QString& message) {
    if (!m_cliInstallLog) {
        return;
    }
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    m_cliInstallLog->appendPlainText(trimmed);
    if (QScrollBar* bar = m_cliInstallLog->verticalScrollBar()) {
        bar->setValue(bar->maximum());
    }
}

void MainWindow::setupCliInstallOverlay() {
    if (m_cliInstallOverlay) {
        return;
    }
    m_cliInstallOverlay = new QWidget(this);
    m_cliInstallOverlay->setStyleSheet("background: rgba(40, 40, 40, 200); border-radius: 8px;");
    m_cliInstallOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    QVBoxLayout* layout = new QVBoxLayout(m_cliInstallOverlay);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(6);
    layout->setContentsMargins(12, 12, 12, 12);

    m_cliInstallOverlayLabel = new QLabel(tr("Installing CLI tools..."), m_cliInstallOverlay);
    m_cliInstallOverlayLabel->setStyleSheet("color: white; font-weight: bold; padding: 0px;");
    layout->addWidget(m_cliInstallOverlayLabel);

    m_cliInstallProgress = new QProgressBar(m_cliInstallOverlay);
    m_cliInstallProgress->setRange(0, 0);
    m_cliInstallProgress->setFixedWidth(200);
    m_cliInstallProgress->setMaximumHeight(6);
    layout->addWidget(m_cliInstallProgress);

    m_cliInstallLog = new QPlainTextEdit(m_cliInstallOverlay);
    m_cliInstallLog->setReadOnly(true);
    m_cliInstallLog->document()->setMaximumBlockCount(100);
    m_cliInstallLog->setFixedSize(340, 100);
    m_cliInstallLog->setStyleSheet("background: rgba(0, 0, 0, 90); color: #e0e0e0; border: 1px solid rgba(255, 255, 255, 60); font-size: 10px;");
    layout->addWidget(m_cliInstallLog);

    m_cancelLoadingButton = new QPushButton(tr("Cancel"), m_cliInstallOverlay);
    m_cancelLoadingButton->setStyleSheet("background: #d32f2f; color: white; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 11px;");
    m_cancelLoadingButton->setCursor(Qt::PointingHandCursor);
    m_cancelLoadingButton->setMaximumWidth(80);
    layout->addWidget(m_cancelLoadingButton, 0, Qt::AlignCenter);
    connect(m_cancelLoadingButton, &QPushButton::clicked, this, &MainWindow::onCancelLoading);

    m_cliInstallOverlay->hide();
    updateCliOverlayGeometry();
}

void MainWindow::onCancelLoading() {
    m_isCanceled = true;
    if (m_layoutRunner && m_layoutRunner->isRunning()) {
        m_layoutRunner->stop();
    }
    m_folderDiscoveryWatcher.cancel();
    m_projectLoadWatcher.cancel();
    m_zipDiscoveryWatcher.cancel();
    m_frameDetectionWatcher.cancel();
    m_tarExtractionWatcher.cancel();
    m_frameExtractionWatcher.cancel();
    m_projectSaveWatcher.cancel();
    
    m_statusLabel->setText(tr("Operation canceled"));
    setLoading(false);
}

void MainWindow::showCliInstallOverlay() {
    if (!m_cliInstallOverlay) {
        setupCliInstallOverlay();
    }
    if (!m_cliInstallOverlay) {
        return;
    }
    m_cliInstallInProgress = true;
    if (m_cliInstallProgress) {
        m_cliInstallProgress->show();
    }
    m_cliInstallOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_cliInstallOverlayLabel->setText(tr("Installing CLI tools..."));
    if (m_cliInstallLog) {
        m_cliInstallLog->clear();
        m_cliInstallLog->appendPlainText(tr("Starting CLI install..."));
    }
    updateCliOverlayGeometry();
    m_cliInstallOverlay->show();
    m_loadingOverlayVisible = true;
}

void MainWindow::hideCliInstallOverlay() {
    m_cliInstallInProgress = false;
    if (m_cliInstallOverlay) {
        m_cliInstallOverlay->hide();
        if (m_cliInstallProgress) {
            m_cliInstallProgress->show();
        }
        m_cliInstallOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    }
    m_loadingOverlayVisible = false;
}

void MainWindow::updateCliOverlayGeometry() {
    if (!m_cliInstallOverlay) {
        return;
    }
    // Position the overlay dialog in the center with minimal size
    int width = 380;
    int height = 200;
    int x = (rect().width() - width) / 2;
    int y = (rect().height() - height) / 2;
    m_cliInstallOverlay->setGeometry(x, y, width, height);
    m_cliInstallOverlay->raise();
}

#ifndef SPRAT_EMBEDDED_CLI
void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    // General process finished handler if needed
}

void MainWindow::onProcessError(QProcess::ProcessError error) {
    // General process error handler
}
#endif
