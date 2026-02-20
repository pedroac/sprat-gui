#include "MainWindow.h"

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
    m_cliReady = resolveCliBinaries(missing);
    m_statusLabel->setText(m_cliReady ? tr("CLI ready") : tr("CLI missing"));
    if (!m_cliReady) {
        showMissingCliDialog(missing);
    }
    updateUiState();
}

bool MainWindow::resolveCliBinaries(QStringList& missing) {
    QString binDir = CliToolsConfig::loadBinDir();
    CliPaths overrides = CliToolsConfig::loadOverrides();

    m_spratLayoutBin = CliToolsConfig::resolveBinary("spratlayout", overrides.layoutBinary, binDir);
    if (m_spratLayoutBin.isEmpty()) {
        missing << "spratlayout";
    }

    m_spratPackBin = CliToolsConfig::resolveBinary("spratpack", overrides.packBinary, binDir);
    if (m_spratPackBin.isEmpty()) {
        missing << "spratpack";
    }

    m_spratConvertBin = CliToolsConfig::resolveBinary("spratconvert", overrides.convertBinary, binDir);
    if (m_spratConvertBin.isEmpty()) {
        missing << "spratconvert";
    }

    m_cliPaths.layoutBinary = overrides.layoutBinary.isEmpty() ? m_spratLayoutBin : overrides.layoutBinary;
    m_cliPaths.packBinary = overrides.packBinary.isEmpty() ? m_spratPackBin : overrides.packBinary;
    m_cliPaths.convertBinary = overrides.convertBinary.isEmpty() ? m_spratConvertBin : overrides.convertBinary;

    return missing.isEmpty();
}

void MainWindow::showMissingCliDialog(const QStringList& missing) {
    MissingCliAction action = CliToolsUi::askMissingCliAction(this, missing);
    if (action == MissingCliAction::Install) {
        installCliTools();
    } else if (action == MissingCliAction::ProvidePath) {
        openCliPathDialog();
    } else {
        m_statusLabel->setText(tr("CLI missing"));
        QApplication::quit();
    }
}

void MainWindow::openCliPathDialog() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select folder containing sprat binaries"));
    if (!dir.isEmpty()) {
        CliToolsConfig::saveBinDir(dir);
        checkCliTools();
    }
}

void MainWindow::installCliTools() {
    if (!m_cliToolInstaller) {
        return;
    }
    m_statusLabel->setText(tr("Installing CLI tools..."));
    m_cliToolInstaller->installCliTools();
}

void MainWindow::loadFolder(const QString& path, bool confirmReplace) {
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
    m_currentFolder = targetPath;
    m_folderLabel->setText(tr("Folder: ") + QDir(targetPath).absolutePath());
    m_layoutSourcePath = QDir(targetPath).absolutePath();
    m_layoutSourceIsList = false;
    m_cachedLayoutOutput.clear();
    m_cachedLayoutScale = 1.0;
    if (!m_frameListPath.isEmpty()) {
        QFile::remove(m_frameListPath);
        m_frameListPath.clear();
    }
    const QStringList absolutePaths = ImageDiscoveryService::imagesInDirectory(targetPath);
    const int imageCount = absolutePaths.size();
    m_activeFramePaths = absolutePaths;
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
    bool hasLayout = m_layoutModel.sprites.size() > 0;
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
    if (m_loadingOverlayDelayTimer && m_loadingOverlayDelayTimer->isActive()) {
        m_loadingOverlayDelayTimer->stop();
    }
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
