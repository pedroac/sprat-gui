#include "MainWindow.h"
#include "AnimationCanvas.h"

#include "CliToolsConfig.h"
#include "CliToolsUi.h"
#include "ImageDiscoveryService.h"
#include "ImageFolderSelectionDialog.h"

#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

void MainWindow::checkCliTools() {
    QStringList missing;
    bool allFound = resolveCliBinaries(missing);
    
    if (allFound) {
        QString currentVersion = CliToolsConfig::checkBinaryVersion(m_cliPaths.layoutBinary);
        QString requiredVersion = SPRAT_CLI_VERSION;

        if (currentVersion.isEmpty()) {
            m_cliReady = false;
            m_statusLabel->setText(tr("CLI error (failed to execute)"));
            QMessageBox::critical(this, tr("CLI Execution Failed"),
                tr("The CLI tool 'spratlayout' was found but failed to execute.\n"
                   "This is usually caused by missing dependencies like 'archive.dll'.\n"
                   "Please ensure all required DLLs are in the 'cli' folder."));
            return;
        }

        if (currentVersion != requiredVersion) {
            if (CliToolsUi::askUpgrade(this, currentVersion, requiredVersion)) {
                installCliTools();
                return;
            }
        }
        m_cliReady = true;
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
}

bool MainWindow::resolveCliBinaries(QStringList& missing) {
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
}

void MainWindow::showMissingCliDialog(const QStringList& missing) {
    MissingCliAction action = CliToolsUi::askMissingCliAction(this, missing);
    if (action == MissingCliAction::Install) {
        installCliTools();
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
    m_loadingUiMessage = tr("Loading images...");
    setLoading(true);
    QString targetPath = path;
    QString selection;
    bool selectionCanceled = false;
    if (ImageFolderSelectionDialog::pickSingleFolderWithImages(this, path, selection, &selectionCanceled)) {
        targetPath = selection;
    } else if (selectionCanceled) {
        m_statusLabel->setText(tr("Load canceled"));
        setLoading(false);
        return;
    } else if (!ImageDiscoveryService::hasImageFiles(path)) {
        setLoading(false);
        QMessageBox::warning(this, tr("Load Failed"), tr("No image files found in the selected folder."));
        return;
    }
    
    const QStringList absolutePaths = ImageDiscoveryService::imagesInDirectory(targetPath);
    if (action == DropAction::Merge) {
        m_session->activeFramePaths.append(absolutePaths);
    } else {
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

    for (int i = 0; i < absolutePaths.size(); ++i) {
        const QString absolutePath = absolutePaths[i];
        progress.setValue(i);
        progress.setLabelText(QString("%1: %2\n%3").arg(tr("Loading"), QFileInfo(absolutePath).fileName(), absolutePath));
        m_statusLabel->setText(tr("Loading frame: ") + absolutePath);
        QApplication::processEvents();
        if (progress.wasCanceled()) {
            m_statusLabel->setText(tr("Frame loading canceled"));
            setLoading(false);
            return;
        }
    }
    progress.setValue(imageCount);
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
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Frames Folder"));
    if (!dir.isEmpty()) {
        if (m_animCanvas) m_animCanvas->setZoomManual(false);
        loadFolder(dir);
    }
}

void MainWindow::onInstallFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    hideCliInstallOverlay();
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        m_statusLabel->setText(tr("CLI installation finished"));
        checkCliTools();
        return;
    }
    m_statusLabel->setText(tr("CLI installation failed"));
    QMessageBox::warning(this, tr("Install Failed"), tr("Could not install CLI tools automatically."));
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

void MainWindow::setupCliInstallOverlay() {
    if (m_cliInstallOverlay) {
        return;
    }
    m_cliInstallOverlay = new QWidget(this);
    m_cliInstallOverlay->setStyleSheet("background: rgba(0, 0, 0, 180);");
    m_cliInstallOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    QVBoxLayout* layout = new QVBoxLayout(m_cliInstallOverlay);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(12);
    m_cliInstallOverlayLabel = new QLabel(tr("Installing CLI tools..."), m_cliInstallOverlay);
    m_cliInstallOverlayLabel->setStyleSheet("color: white; font-weight: bold;");
    layout->addWidget(m_cliInstallOverlayLabel);
    m_cliInstallProgress = new QProgressBar(m_cliInstallOverlay);
    m_cliInstallProgress->setRange(0, 0);
    m_cliInstallProgress->setFixedWidth(220);
    layout->addWidget(m_cliInstallProgress);
    m_cliInstallOverlay->hide();
    updateCliOverlayGeometry();
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
    m_cliInstallOverlay->setGeometry(rect());
    m_cliInstallOverlay->raise();
}

void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    // General process finished handler if needed
}

void MainWindow::onProcessError(QProcess::ProcessError error) {
    // General process error handler
}
