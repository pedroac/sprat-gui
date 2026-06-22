#include "MainWindow.h"
#include "AnimationPreviewPanel.h"
#include "FrameAnimationWorkspace.h"
#include "LayoutOrchestrator.h"
#include "CliSetupController.h"
#ifdef Q_OS_WASM
#include "WasmFileDialog.h"
#include "ViewUtils.h"
#endif
#include "AnimationCanvas.h"

#include "CliToolsUi.h"
#include "ImageDiscoveryService.h"
#include "ImageFolderSelectionDialog.h"
#include "MessageDialog.h"

#include <QApplication>
#include <QStyle>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QElapsedTimer>
#include <QDebug>
#include <QMessageBox>
#include <QProgressDialog>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QtConcurrent>

// === Thin stubs delegating to CliSetupController ===

void MainWindow::checkCliTools() {
#ifdef Q_OS_WASM
    if (jsIsAsyncBusy()) {
        QTimer::singleShot(100, this, &MainWindow::checkCliTools);
        return;
    }
#endif
    if (m_cliSetup) m_cliSetup->check();
}

void MainWindow::updateCliDiagnostics() {
    if (!m_cliSetup) return;
    // Skip if a previous update is still in progress — the result would be the same.
    if (m_cliDiagnosticsWatcher.isRunning()) return;

    const QString spritesFolder = m_session ? m_session->currentFolder : QString();

    // buildDiagnosticsText spawns subprocesses (waitForFinished 2 s each).
    // Run it off the main thread; append as a diagnosis log entry when done.
    CliSetupController* cliSetup = m_cliSetup; // raw ptr safe: cliSetup outlives the watcher
    disconnect(&m_cliDiagnosticsWatcher, &QFutureWatcher<QString>::finished, this, nullptr);
    connect(&m_cliDiagnosticsWatcher, &QFutureWatcher<QString>::finished, this, [this]() {
        appendLog(LogLevel::Diagnosis, m_cliDiagnosticsWatcher.result());
    });
    m_cliDiagnosticsWatcher.setFuture(
        QtConcurrent::run([cliSetup, spritesFolder]() {
            return cliSetup->buildDiagnosticsText(spritesFolder);
        }));
}

void MainWindow::installCliTools() {
    if (m_cliSetup) m_cliSetup->install();
}

// === UI-only overlay methods ===

void MainWindow::setupCliInstallOverlay() {
    if (m_cliInstallOverlay) {
        return;
    }
    m_cliInstallOverlay = new QWidget(this);
    m_cliInstallOverlay->setStyleSheet("background: rgba(40, 40, 40, 200); border-radius: 8px; color: white;");
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
    m_cliInstallProgress->setStyleSheet("color: white;");
    m_cliInstallProgress->hide();
    layout->addWidget(m_cliInstallProgress);

    m_cliInstallLog = new QPlainTextEdit(m_cliInstallOverlay);
    m_cliInstallLog->setReadOnly(true);
    m_cliInstallLog->document()->setMaximumBlockCount(100);
    m_cliInstallLog->setFixedSize(340, 100);
    m_cliInstallLog->setStyleSheet("background: rgba(0, 0, 0, 90); color: #e0e0e0; border: 1px solid rgba(255, 255, 255, 60); font-size: 10px;");
    m_cliInstallLog->hide();
    layout->addWidget(m_cliInstallLog);

    m_cancelLoadingButton = new QPushButton(
        QApplication::style()->standardIcon(QStyle::SP_DialogCancelButton), tr("Cancel"), m_cliInstallOverlay);
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
    if (m_layoutOrchestrator) {
        m_layoutOrchestrator->stop();
    }
    if (m_projectController) {
        m_projectController->cancelAll();
    }
    if (m_exportCoordinator) {
        m_exportCoordinator->cancelExport();
    }

    m_statusLabel->setText(m_cliInstallInProgress ? tr("Installation canceled") : tr("Layout canceled"));
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
        m_cliInstallLog->show();
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

// === Download progress and install log UI updates (connected to controller signals) ===

void MainWindow::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (m_cliInstallProgress) {
        if (bytesTotal > 0) {
            const int pct = static_cast<int>(bytesReceived * 100 / bytesTotal);
            if (pct >= 100) {
                // Download finished — extraction/compilation begins; switch to indeterminate.
                m_cliInstallProgress->setRange(0, 0);
                m_cliInstallOverlayLabel->setText(tr("Building CLI tools..."));
            } else {
                m_cliInstallProgress->setRange(0, 100);
                m_cliInstallProgress->setValue(pct);
                m_cliInstallOverlayLabel->setText(tr("Downloading CLI tools (%1%)...").arg(pct));
            }
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

// === Folder loading and related UI (stays in MainWindow.Cli.cpp for Phase 2) ===

void MainWindow::loadFolder(const QString& path, DropAction action) {
    if (action == DropAction::Cancel) {
        return;
    }

    const QString folderPath = QDir(path).absolutePath();
    qInfo() << "[loadFolder] path=" << folderPath << "action=" << (int)action;

    // Clean up any previous frame list file
    if (!m_session->frameListPath.isEmpty()) {
        QFile::remove(m_session->frameListPath);
        m_session->frameListPath.clear();
    }

    if (action == DropAction::Merge) {
        // If this source is already loaded, sync it instead of re-importing.
        for (const ProjectSource& s : m_session->sources) {
            if (s.originalPath == folderPath) {
                onRunLayout(true);
                return;
            }
        }

        // Enumerate the new folder's images so we can copy them into sourceFolder.
        // spratlayout will traverse sourceFolder itself; no .txt list is needed.
        const QStringList newImages = ImageDiscoveryService::collectImagesRecursive({folderPath});
        if (newImages.isEmpty()) {
            MessageDialog::warning(this, tr("Merge Failed"), tr("No image files found in the selected folder."));
            return;
        }

        m_loadingUiMessage = tr("Copying files...");
        setLoading(true);

        ensureSourceFolder();

        // Each merged folder gets its own subfolder so sources stay isolated.
        const QString subName = m_projectController->computeSourceSubfolderName(folderPath);
        const QString subfolderPath = QDir(m_session->sourceFolder).filePath(subName);
        QDir dstRoot(subfolderPath);
        dstRoot.mkpath(QStringLiteral("."));

        QDir srcRoot(folderPath);
        QStringList copiedPaths;
        for (const QString& imgPath : newImages) {
            const QString relPath = srcRoot.relativeFilePath(imgPath);
            const QString dstPath = dstRoot.filePath(relPath);
            const QFileInfo dstInfo(dstPath);
            QDir().mkpath(dstInfo.absolutePath());
            QString finalDst = dstPath;
            if (QFile::exists(dstPath)) {
                if (m_mergeReplaceAllDuplicates) {
                    QFile::remove(dstPath);
                } else {
                    int n = 2;
                    do {
                        finalDst = dstInfo.absolutePath() + QLatin1Char('/') +
                                   dstInfo.baseName() + QLatin1Char('_') +
                                   QString::number(n++) + QLatin1Char('.') + dstInfo.suffix();
                    } while (QFile::exists(finalDst));
                }
            }
            if (QFile::copy(imgPath, finalDst)) {
                copiedPaths.append(finalDst);
            }
        }
        m_session->activeFramePaths.append(copiedPaths);

        if (m_projectController) m_projectController->setShouldClearSpritesFolder(false);
        m_session->layoutSourcePath = m_session->sourceFolder;
        m_session->layoutSourceIsList = false;

        ProjectSource src;
        src.name = m_projectController->makeUniqueSourceName(QFileInfo(folderPath).fileName());
        src.type = SourceType::Folder;
        src.originalPath = folderPath;
        src.cachedFolderPath = subfolderPath;
        m_session->sources.append(src);
        m_projectController->syncFramePathsToNeutralAtlas(DropAction::Merge);

        m_statusLabel->setText(QString(tr("Merging %1 image frame(s) from %2"))
                               .arg(newImages.size()).arg(folderPath));
        m_loadingUiMessage = tr("Building layout...");
        if (m_cliInstallOverlayLabel) m_cliInstallOverlayLabel->setText(m_loadingUiMessage);
    } else {
        // Replace: copy into a fresh temp source folder so the original is never modified.
        // spratlayout traverses the temp folder; activeFramePaths is rebuilt from its output.
        const QStringList newImages = ImageDiscoveryService::collectImagesRecursive({folderPath});
        if (newImages.isEmpty()) {
            MessageDialog::warning(this, tr("Load Failed"), tr("No image files found in the selected folder."));
            return;
        }

        m_projectController->clearSourceFolderTempDir();
        m_session->sourceFolder.clear();
        m_session->activeAtlas().timelines.clear();
        m_session->selectedTimelineIndex = -1;
        refreshTimelineList();
        refreshAnimationTest();
        if (m_projectController && m_projectController->isSourceFolderTemp()) {
            m_projectController->setProjectFilePath(QString());
        }
        m_session->currentFolder = folderPath;
        updateFolderLabel(folderPath);
        m_session->cachedLayoutOutput.clear();
        m_session->cachedLayoutScale = 1.0;
        m_session->activeFramePaths.clear();
        if (m_projectController) m_projectController->setShouldClearSpritesFolder(false);
        if (m_undoStack) m_undoStack->clear();

        m_loadingUiMessage = tr("Copying files...");
        setLoading(true);
        m_statusLabel->setText(QString(tr("Loading %1...")).arg(folderPath));

        ensureSourceFolder();

        // Copy into a dedicated subfolder so all sources are consistently isolated.
        const QString subName = m_projectController->computeSourceSubfolderName(folderPath);
        const QString subfolderPath = QDir(m_session->sourceFolder).filePath(subName);
        QDir dstRoot(subfolderPath);
        dstRoot.mkpath(QStringLiteral("."));

        QDir srcRoot(folderPath);
        QStringList copiedPaths;
        for (const QString& imgPath : newImages) {
            const QString relPath = srcRoot.relativeFilePath(imgPath);
            const QString dstPath = dstRoot.filePath(relPath);
            QFileInfo dstInfo(dstPath);
            QDir().mkpath(dstInfo.absolutePath());
            if (QFile::copy(imgPath, dstPath)) {
                copiedPaths.append(dstPath);
            }
        }
        m_session->activeFramePaths = copiedPaths;

        m_session->layoutSourcePath = m_session->sourceFolder;
        m_session->layoutSourceIsList = false;

        // Register the loaded folder as a ProjectSource
        m_session->sources.clear();
        {
            ProjectSource src;
            src.name = QFileInfo(folderPath).fileName();
            src.type = SourceType::Folder;
            src.originalPath = folderPath;
            src.cachedFolderPath = subfolderPath;
            m_session->sources.append(src);
        }
        m_projectController->syncFramePathsToNeutralAtlas(DropAction::Replace);

        m_loadingUiMessage = tr("Building layout...");
        if (m_cliInstallOverlayLabel) m_cliInstallOverlayLabel->setText(m_loadingUiMessage);
    }

    if (m_layoutOrchestrator) m_layoutOrchestrator->markCenterPivotsOnNextLayout();
#ifdef Q_OS_WASM
    // Defer on WASM so the loading overlay can paint before the synchronous layout run.
    QTimer::singleShot(0, this, [this]() { scheduleLayoutRebuild(true); });
#else
    scheduleLayoutRebuild(true);
#endif
}

bool MainWindow::confirmLayoutReplacement() {
    bool hasLayout = !m_session->activeAtlas().layoutModels.isEmpty() && m_session->activeAtlas().layoutModels.first().sprites.size() > 0;
    if (!hasLayout) {
        return true;
    }

    const QMessageBox::StandardButton answer = MessageDialog::question(
        this,
        tr("A layout is already loaded."),
        tr("Do you want to replace it?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No,
        tr("Layout Already Loaded")
    );

    return answer == QMessageBox::Yes;
}

void MainWindow::onLoadFolder() {
#ifdef Q_OS_WASM
    wasmOpenFileDialog(true);
    return;
#endif
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Frames Folder"));
    if (!dir.isEmpty()) {
        auto* apC = m_frameAnimWorkspace ? m_frameAnimWorkspace->animPanel() : nullptr;
        auto* animCanvas = apC ? apC->animCanvas() : nullptr;
        if (animCanvas) animCanvas->setZoomManual(false);
        loadFolder(dir, confirmDropAction(dir));
    }
}

void MainWindow::onAddSourceFile() {
#ifdef Q_OS_WASM
    wasmOpenFileDialog(false);
    return;
#endif
    const QString filter = tr(
        "All Supported Source Files (*.zip *.tar *.tar.gz *.tar.bz2 *.tar.xz *.png *.jpg *.jpeg *.bmp *.gif *.webp *.tga *.dds);;"
        "Archives (*.zip *.tar *.tar.gz *.tar.bz2 *.tar.xz);;"
        "Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tga *.dds)");
    const QString startDir = m_session ? m_session->currentFolder : QString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Add Source File"), startDir, filter);
    if (path.isEmpty()) {
        return;
    }

    const DropAction action = confirmDropAction(path);
    if (action == DropAction::Cancel) {
        return;
    }
    tryHandleDroppedPath(path, action);
}

#ifndef SPRAT_EMBEDDED_CLI
void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    Q_UNUSED(exitCode);
    Q_UNUSED(exitStatus);
    // General process finished handler if needed
}

void MainWindow::onProcessError(QProcess::ProcessError error) {
    Q_UNUSED(error);
    // General process error handler
}
#endif
