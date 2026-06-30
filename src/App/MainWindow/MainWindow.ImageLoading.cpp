#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextStream>
#include <QFile>
#include <QByteArray>
#include <QString>
#include <QRect>
#include <QVector>
#include <QLabel>
#include <QPixmap>
#include <QPoint>
#include <QDebug>
#include <QCollator>
#include <QRegularExpression>
#include <QtConcurrent>
#include <algorithm>

#include "MainWindow.h"
#include "LayoutCanvas.h"
#include "LayoutOrchestrator.h"
#include "MessageDialog.h"
#include "FrameDetectionDialog.h"
#include "AnimatedImageImport.h"
#include "ArchiveExtractor.h"
#include "ImageDiscoveryService.h"
#include "SpriteNameUtils.h"
#include "AnimationPreviewService.h"
#include "ProjectController.h"

void MainWindow::loadImageWithFrameDetection(const QString& imagePath, DropAction action, bool hideSingleFrame) {
    if (action == DropAction::Cancel) {
        return;
    }
    m_detectFramesHideSingleFrame = hideSingleFrame;

    if (AnimatedImageImport::isAnimatedGif(imagePath)) {
        if (!m_cliReady || m_isLoading) {
            MessageDialog::warning(this, tr("Error"), tr("CLI tools not ready or already loading."));
            return;
        }

        m_loadingUiMessage = tr("Decoding animated GIF...");
        setLoading(true);

        if (action == DropAction::Replace) {
            m_projectController->clearTempDirs();
        }
        auto tempDir = std::make_unique<QTemporaryDir>();
        if (!tempDir->isValid()) {
            m_statusLabel->setText(tr("Error: Could not create temporary directory"));
            setLoading(false);
            return;
        }

        const QString tempPath = tempDir->path();
        m_projectController->addTempDir(std::move(tempDir));

        QString error;
        if (!AnimatedImageImport::extractAnimatedFrames(imagePath, tempPath, &error)) {
            setLoading(false);
            MessageDialog::warning(this, tr("Load Failed"), error.isEmpty() ? tr("Could not decode animated GIF.") : error);
            return;
        }

        if (processExtractedFrames(tempPath, imagePath, action)) {
            scheduleLayoutRebuild(true);
        }
        return;
    }

#ifdef Q_OS_WASM
    if (!m_cliReady || m_isLoading) {
        MessageDialog::warning(this, tr("Error"), tr("CLI tools not ready or already loading."));
        return;
    }

    if (m_spratFramesBin.isEmpty()) {
        MessageDialog::warning(this, tr("Error"), tr("spratframes binary not found."));
        return;
    }

    m_isCanceled = false;
    m_loadingUiMessage = tr("Detecting frames in image...");
    setLoading(true);

    QTimer::singleShot(0, this, [this, imagePath, action]() {
        ProjectController::FrameDetectionTaskResult result;
        result.imagePath = imagePath;
        result.action = action;
        result.detection = m_projectController->detectFramesInImage(imagePath);
        processFrameDetectionResult(result);
    });
#else
    if (!m_cliReady || m_isLoading) {
        MessageDialog::warning(this, tr("Error"), tr("CLI tools not ready or already loading."));
        return;
    }

    if (m_spratFramesBin.isEmpty()) {
        MessageDialog::warning(this, tr("Error"), tr("spratframes binary not found."));
        return;
    }

    if (m_projectController->isFrameDetectionRunning()) {
        return;
    }

    m_isCanceled = false;
    m_loadingUiMessage = tr("Detecting frames in image...");
    setLoading(true);

    m_projectController->launchFrameDetection(imagePath,
        action);
#endif
}

void MainWindow::onFrameDetectionFinished() {
    processFrameDetectionResult(m_projectController->frameDetectionResult());
}

void MainWindow::processFrameDetectionResult(const ProjectController::FrameDetectionTaskResult& result) {
    setLoading(false);

    if (m_isCanceled) {
        m_statusLabel->setText(tr("Frame detection cancelled"));
        return;
    }

    const QString imagePath = result.imagePath;
    FrameDetectionDialog::RedetectFn redetectFn = [this, imagePath](const AppSettings& s) {
        ProjectController::FramesAdvancedSettings fs;
        fs.framesHasRectangles = s.framesHasRectangles;
        fs.framesRectangleColor = s.framesRectangleColor;
        fs.framesTolerance = s.framesTolerance;
        fs.framesMinSize = s.framesMinSize;
        fs.framesMaxSprites = s.framesMaxSprites;
        m_projectController->setFramesSettings(fs);
        return m_projectController->detectFramesInImage(imagePath).frames;
    };

    FrameDetectionDialog dialog(result.imagePath, result.detection.frames, m_settings, redetectFn, result.detection.backgroundColor, this);
    if (m_detectFramesHideSingleFrame)
        dialog.hideSingleFrameButton();
    m_detectFramesHideSingleFrame = false;
    if (dialog.exec() != QDialog::Accepted) {
        m_statusLabel->setText(tr("Frame detection cancelled"));
        return;
    }

    {
        AppSettings updated = dialog.getUpdatedSettings();
        m_settings.framesHasRectangles = updated.framesHasRectangles;
        m_settings.framesRectangleColor = updated.framesRectangleColor;
        m_settings.framesTolerance = updated.framesTolerance;
        m_settings.framesMinSize = updated.framesMinSize;
    }

    if (dialog.userAccepted()) {
        QVector<QRect> selectedFrames = dialog.getSelectedFrames();
        const DropAction action = result.action;
        const QColor backgroundColor = result.detection.backgroundColor;

        if (selectedFrames.isEmpty()) {
            m_statusLabel->setText(tr("Using image as single frame"));
            handleSingleImageLayout(imagePath, action, backgroundColor);
            return;
        }

#ifndef Q_OS_WASM
        if (m_spratUnpackBin.isEmpty()) {
            qWarning() << "[FrameExtraction] spratunpack binary not set, cannot extract frames";
            MessageDialog::warning(this, tr("Error"), tr("spratunpack binary not found."));
            return;
        }
#endif

        m_loadingUiMessage = tr("Extracting frames...");
        setLoading(true);

        if (action == DropAction::Replace) {
            m_projectController->clearTempDirs();
        }
        auto tempDir = std::make_unique<QTemporaryDir>();
        if (!tempDir->isValid()) {
            m_statusLabel->setText(tr("Error: Could not create temporary directory"));
            setLoading(false);
            return;
        }
        QString tempPath = tempDir->path();
        m_projectController->addTempDir(std::move(tempDir));

#ifdef Q_OS_WASM
        auto extractTaskWasm = [this, selectedFrames, imagePath, tempPath, action, backgroundColor]() {
            ProjectController::FrameExtractionResult res;
            res.tempPath = tempPath;
            res.sourcePath = imagePath;
            res.action = action;
            res.backgroundColor = backgroundColor;

            QString framesData = m_projectController->generateSpratFramesFormat(selectedFrames, imagePath);
            QStringList args;
            args << imagePath << QStringLiteral("--frames") << QStringLiteral("-") << QStringLiteral("--output") << tempPath;
            QByteArray framesDataBytes = framesData.toUtf8();
            res.success = runTool(m_spratUnpackBin, args, &framesDataBytes);
            return res;
        };
        QTimer::singleShot(0, this, [this, extractTaskWasm]() {
            processFrameExtractionResult(extractTaskWasm());
        });
#else
        m_projectController->launchFrameExtraction(imagePath, tempPath, action, backgroundColor, selectedFrames);
#endif
    } else {
        m_statusLabel->setText(tr("Using image as single frame"));
        handleSingleImageLayout(imagePath, result.action, result.detection.backgroundColor);
    }
}

void MainWindow::onFrameExtractionFinished() {
    processFrameExtractionResult(m_projectController->frameExtractionResult());
}

void MainWindow::processFrameExtractionResult(const ProjectController::FrameExtractionResult& result) {
    qInfo() << "[FrameExtraction] processFrameExtractionResult success=" << result.success
            << "tempPath=" << result.tempPath;
    if (result.success) {
        if (processExtractedFrames(result.tempPath, result.sourcePath,
                result.action, result.backgroundColor)) {
            scheduleLayoutRebuild(true);
        }
    } else {
        qWarning() << "[FrameExtraction] spratunpack failed for" << result.sourcePath;
        m_statusLabel->setText(tr("Error running spratunpack"));
        setLoading(false);
        MessageDialog::warning(this, tr("Load Failed"),
            tr("Failed to extract frames from image: %1").arg(result.sourcePath));
    }
}

void MainWindow::loadTarFile(const QString& tarPath, DropAction action) {
    if (action == DropAction::Cancel) {
        return;
    }
    
    if (!m_cliReady || m_isLoading) {
        MessageDialog::warning(this, tr("Error"), tr("CLI tools not ready or already loading."));
        return;
    }
    
    if (m_spratUnpackBin.isEmpty()) {
        MessageDialog::warning(this, tr("Error"), tr("spratunpack binary not found."));
        return;
    }

    if (m_projectController->isTarExtractionRunning()) {
        return;
    }

    m_loadingUiMessage = tr("Extracting frames from tar file...");
    m_isCanceled = false;
    setLoading(true);

    if (action == DropAction::Replace) {
        m_projectController->clearTempDirs();
    }
    auto tempDir = std::make_unique<QTemporaryDir>();
    if (!tempDir->isValid()) {
        m_statusLabel->setText(tr("Error: Could not create temporary directory"));
        setLoading(false);
        return;
    }
    QString tempPath = tempDir->path();
    m_projectController->addTempDir(std::move(tempDir));

#ifdef Q_OS_WASM
    auto taskWasm = [this, tarPath, tempPath, action]() {
        ProjectController::TarExtractionResult result;
        result.tempPath = tempPath;
        result.tarPath = tarPath;
        result.action = action;
        result.success = false;
        QString error;
        if (ArchiveExtractor::extractToDirectory(tarPath, tempPath, error, &m_isCanceled)) {
            result.success = !m_isCanceled;
        } else {
            qWarning() << "ArchiveExtractor (tar) error:" << error;
        }
        return result;
    };
    QTimer::singleShot(0, this, [this, taskWasm]() {
        processTarExtractionResult(taskWasm());
    });
#else
    m_projectController->launchTarExtraction(tarPath,
        action, tempPath);
#endif
}

void MainWindow::onTarExtractionFinished() {
    processTarExtractionResult(m_projectController->tarExtractionResult());
}

void MainWindow::processTarExtractionResult(const ProjectController::TarExtractionResult& result) {
    if (m_isCanceled) {
        setLoading(false);
        m_statusLabel->setText(tr("Tar extraction canceled"));
        return;
    }
    if (result.success) {
        if (processExtractedFrames(result.tempPath, result.tarPath,
                result.action)) {
            scheduleLayoutRebuild(true);
        }
    } else {
        const QString msg = tr("Failed to extract archive: %1").arg(result.tarPath);
        m_statusLabel->setText(tr("Error extracting tar file"));
        setLoading(false);
        MessageDialog::warning(this, tr("Load Failed"), msg);
        m_projectController->registerFailedSource(result.tarPath, msg);
        refreshSpriteTree();
    }
}


void MainWindow::handleSingleImageLayout(const QString& imagePath, DropAction action, const QColor& backgroundColor) {
    qInfo() << "[WASM] handleSingleImageLayout start path=" << imagePath;
    QString finalPath = imagePath;
    
    if (backgroundColor.isValid()) {
        // Create a temporary directory and save the processed image there
        auto tempDir = std::make_unique<QTemporaryDir>();
        if (tempDir->isValid()) {
            QString tempPath = tempDir->path();
            QFileInfo info(imagePath);
            QString fileName = info.fileName();
            // Force PNG for transparency support if original might not support it well or is JPEG
            if (!fileName.toLower().endsWith(".png")) {
                fileName = info.baseName() + "_transparent.png";
            }
            QString outputPath = QDir(tempPath).absoluteFilePath(fileName);

            QImage img(imagePath);
            if (!img.isNull()) {
                m_projectController->applyTransparencyToImage(img, backgroundColor);
                if (img.save(outputPath)) {
                    finalPath = outputPath;
                    m_projectController->addTempDir(std::move(tempDir));
                    // Retain image size to avoid redundant reload later
                    m_singleImageDimensions = img.size();
                }
            }
        }
    }

    if (action == DropAction::Merge) {
        const QString subName = m_projectController->computeSourceSubfolderName(imagePath);
        const QString subfolderPath = QDir(m_session->sourceFolder).filePath(subName);
        const QStringList copied = m_projectController->copyFramesToSourceSubfolder(
            {finalPath}, subfolderPath, m_mergeReplaceAllDuplicates);
        m_session->activeFramePaths.append(copied);
        m_projectController->registerLoadedSource(imagePath, action, subfolderPath);
        ensureFrameListInput();
        if (m_projectController) m_projectController->setShouldClearSpritesFolder(false);
        if (m_layoutOrchestrator) m_layoutOrchestrator->markCenterPivotsOnNextLayout();
        scheduleLayoutRebuild(true);
        return;
    }

    // On Replace, delete all contents from sprites folder (including subdirectories)
    if (!m_session->sourceFolder.isEmpty()) {
        QDir dir(m_session->sourceFolder);
        dir.removeRecursively();
        QDir().mkpath(m_session->sourceFolder);
    }

    m_session->sourceFolder.clear();
    m_projectController->clearSourceFolderTempDir();
    ensureSourceFolder();
    m_session->activeAtlas().timelines.clear();
    m_session->selectedTimelineIndex = -1;
    // Clear selection state when loading new image to avoid stale pointers
    m_session->selectedSprite.reset();
    m_session->selectedSprites.clear();
    m_session->selectedPointName.clear();
    // Update UI to clear sprite selection display
    onSpriteSelected(SpritePtr());
    refreshTimelineList();
    refreshAnimationTest();

    // Copy the single image into its own subfolder on Replace
    const QString subName = m_projectController->computeSourceSubfolderName(imagePath);
    const QString subfolderPath = QDir(m_session->sourceFolder).filePath(subName);
    const QStringList copied = m_projectController->copyFramesToSourceSubfolder({finalPath}, subfolderPath);
    m_session->activeFramePaths = copied;
    if (m_projectController) m_projectController->setShouldClearSpritesFolder(false);
    m_projectController->registerLoadedSource(imagePath, action, subfolderPath);
    // After copying, update to the copied path
    QString copiedPath = m_session->activeFramePaths.isEmpty()
                         ? finalPath : m_session->activeFramePaths.first();

    // For a single image, we need to create a simple layout model
    LayoutModel singleImageModel;
    singleImageModel.scale = 1.0;

    // Create a sprite for the single image
    SpritePtr sprite = std::make_shared<Sprite>();
    sprite->path = copiedPath;
    sprite->name = QFileInfo(imagePath).baseName();
    
    // Get image dimensions (use cached size to avoid redundant disk load)
    QSize imgSize;
    if (!m_singleImageDimensions.isEmpty()) {
        imgSize = m_singleImageDimensions;
    } else {
        QImage img(finalPath);
        if (!img.isNull()) {
            imgSize = img.size();
        }
    }

    if (!imgSize.isEmpty()) {
        sprite->rect = QRect(0, 0, imgSize.width(), imgSize.height());
        sprite->pivotX = imgSize.width() / 2;
        sprite->pivotY = imgSize.height() / 2;
    } else {
        // Fallback dimensions if image can't be loaded
        sprite->rect = QRect(0, 0, 100, 100);
        sprite->pivotX = 50;
        sprite->pivotY = 50;
    }
    
    singleImageModel.sprites.append(sprite);

    // Apply the model to the canvas
    m_session->activeAtlas().layoutModels = { singleImageModel };
    AnimationPreviewService::invalidateSpriteMap();
    ensureUniqueSpriteNames(m_session->activeAtlas().layoutModels, m_session->sourceFolder);
    auto* canvas = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
    if (canvas) {
        m_loadingUiMessage = tr("Loading image...");
        setLoading(true);
        canvas->setModelsAsync(m_session->activeAtlas().layoutModels, &m_isCanceled, [this, sprite]() {
            auto* canvas = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
            if (m_isCanceled) {
                setLoading(false);
                return;
            }
            if (canvas) canvas->setZoomManual(false);
            if (canvas) QTimer::singleShot(0, canvas, &LayoutCanvas::initialFit);
            
            m_statusLabel->setText(QString(tr("Loaded single image: %1")).arg(sprite->name));
            // Update UI state
            m_session->rebuildSpriteIndex();
            populateActiveFrameListFromModel();
            refreshSpriteTree();
            updateMainContentView();
            updateUiState();

            setLoading(false);
            qInfo() << "[WASM] handleSingleImageLayout done";
        });
    } else {
        setLoading(false);
    }
}

bool MainWindow::processExtractedFrames(const QString& tempPath, const QString& sourcePath, DropAction action, const QColor& backgroundColor) {
    qInfo() << "[FrameExtraction] processExtractedFrames tempPath=" << tempPath
            << "sourcePath=" << sourcePath;
    // Collect images recursively to preserve subfolder structure from archives
    QStringList framePaths = ImageDiscoveryService::collectImagesRecursive({tempPath});
    qInfo() << "[FrameExtraction] Found" << framePaths.size() << "images in tempPath";

    if (framePaths.isEmpty()) {
        qWarning() << "[FrameExtraction] No images found in tempPath=" << tempPath;
        m_statusLabel->setText(tr("No image files found after extraction"));
        setLoading(false);
        return false;
    }

    // If a background color is provided, make it transparent in all extracted frames
    if (backgroundColor.isValid()) {
        m_loadingUiMessage = tr("Applying transparency...");
        setLoading(true);

        // Store parameters for continuation after async processing
        m_pendingTransparencyTempPath = tempPath;
        m_pendingTransparencySourcePath = sourcePath;
        m_pendingTransparencyFramePaths = framePaths;
        m_pendingTransparencyAction = action;
        m_pendingTransparencyBgColor = backgroundColor;

        // Run transparency processing in background thread
        auto transparencyTask = [this, framePaths, backgroundColor]() {
            for (int i = 0; i < framePaths.size(); ++i) {
                if (m_isCanceled) {
                    break;
                }
                QImage img(framePaths[i]);
                if (!img.isNull()) {
                    m_projectController->applyTransparencyToImage(img, backgroundColor);
                    img.save(framePaths[i]);
                }
            }
        };

        // Execute in thread pool and connect completion
        m_transparencyWatcher.setFuture(QtConcurrent::run(transparencyTask));
        connect(&m_transparencyWatcher, &QFutureWatcher<void>::finished,
                this, &MainWindow::onTransparencyProcessingFinished);
        return true;  // Processing continues asynchronously
    }
    
    if (action == DropAction::Merge) {
        const QString subName = m_projectController->computeSourceSubfolderName(sourcePath);
        const QString subfolderPath = QDir(m_session->sourceFolder).filePath(subName);
        // currentFolder must be set so relative subfolder structure is preserved
        m_session->currentFolder = tempPath;
        const QStringList copied = m_projectController->copyFramesToSourceSubfolder(
            framePaths, subfolderPath, m_mergeReplaceAllDuplicates);
        m_session->activeFramePaths.append(copied);
        m_projectController->registerLoadedSource(sourcePath, action, subfolderPath);
        ensureFrameListInput();
        if (m_projectController) m_projectController->setShouldClearSpritesFolder(false);
    } else {
        // On Replace, delete all contents from sprites folder (including subdirectories)
        if (!m_session->sourceFolder.isEmpty()) {
            QDir dir(m_session->sourceFolder);
            dir.removeRecursively();
            QDir().mkpath(m_session->sourceFolder);
        }

        m_session->sourceFolder.clear();
        m_projectController->clearSourceFolderTempDir();
        ensureSourceFolder();
        m_session->activeAtlas().timelines.clear();
        m_session->selectedTimelineIndex = -1;
        // Clear selection state when loading new frames to avoid stale pointers
        m_session->selectedSprite.reset();
        m_session->selectedSprites.clear();
        m_session->selectedPointName.clear();
        // Update UI to clear sprite selection display
        onSpriteSelected(SpritePtr());
        refreshTimelineList();
        refreshAnimationTest();
        if (m_projectController && m_projectController->isSourceFolderTemp()) {
            m_projectController->setProjectFilePath(QString());
        }
        if (m_projectController) m_projectController->setSourceFolderIsTemp(false);
        m_session->layoutSourcePath = tempPath;
        m_session->layoutSourceIsList = false;
        // Set currentFolder to extraction root so relative subfolder structure is preserved
        m_session->currentFolder = tempPath;
        // Copy sprites into a dedicated subfolder on Replace
        const QString subName = m_projectController->computeSourceSubfolderName(sourcePath);
        const QString subfolderPath = QDir(m_session->sourceFolder).filePath(subName);
        m_session->activeFramePaths = m_projectController->copyFramesToSourceSubfolder(framePaths, subfolderPath);
        if (m_projectController) m_projectController->setShouldClearSpritesFolder(false);

        m_projectController->registerLoadedSource(sourcePath, action, subfolderPath);
    }

    if (!m_session->frameListPath.isEmpty()) {
        QFile::remove(m_session->frameListPath);
        m_session->frameListPath.clear();
    }

    // Ensure we generate a list file so spratlayout respects our sort order
    if (!ensureFrameListInput()) {
        qWarning() << "[FrameExtraction] ensureFrameListInput failed"
                   << "activeFramePaths.size()=" << m_session->activeFramePaths.size()
                   << "sourceFolder=" << m_session->sourceFolder;
        m_statusLabel->setText(tr("Error: Could not create frame list for layout"));
        setLoading(false);
        return false;
    }

    m_statusLabel->setText(QString(tr("Loaded %1 frames")).arg(framePaths.size()));
    if (m_layoutOrchestrator) m_layoutOrchestrator->markCenterPivotsOnNextLayout();
    return true;
}

void MainWindow::onTransparencyProcessingFinished() {
    // Transparency processing completed in background thread
    // Continue with the merge/replace logic
    setLoading(false);

    if (m_isCanceled) {
        m_statusLabel->setText(tr("Cancelled"));
        return;
    }

    // Use the stored parameters to continue processing
    const QString& tempPath = m_pendingTransparencyTempPath;
    const QString& sourcePath = m_pendingTransparencySourcePath;
    const QStringList& framePaths = m_pendingTransparencyFramePaths;
    const DropAction& action = m_pendingTransparencyAction;

    if (action == DropAction::Merge) {
        const QString subName = m_projectController->computeSourceSubfolderName(sourcePath);
        const QString subfolderPath = QDir(m_session->sourceFolder).filePath(subName);
        m_session->currentFolder = tempPath;
        const QStringList copied = m_projectController->copyFramesToSourceSubfolder(
            framePaths, subfolderPath, m_mergeReplaceAllDuplicates);
        m_session->activeFramePaths.append(copied);
        m_projectController->registerLoadedSource(sourcePath, action, subfolderPath);
        ensureFrameListInput();
        if (m_projectController) m_projectController->setShouldClearSpritesFolder(false);
        if (m_layoutOrchestrator) m_layoutOrchestrator->markCenterPivotsOnNextLayout();
        scheduleLayoutRebuild(true);
    } else {
        // On Replace, delete all contents from sprites folder (including subdirectories)
        if (!m_session->sourceFolder.isEmpty()) {
            QDir dir(m_session->sourceFolder);
            dir.removeRecursively();
            QDir().mkpath(m_session->sourceFolder);
        }

        m_session->sourceFolder.clear();
        m_projectController->clearSourceFolderTempDir();
        ensureSourceFolder();
        m_session->activeAtlas().timelines.clear();
        m_session->selectedTimelineIndex = -1;
        // Clear selection state when loading new frames to avoid stale pointers
        m_session->selectedSprite.reset();
        m_session->selectedSprites.clear();
        m_session->selectedPointName.clear();
        // Update UI to clear sprite selection display
        onSpriteSelected(SpritePtr());
        refreshTimelineList();
        refreshAnimationTest();
        if (m_projectController && m_projectController->isSourceFolderTemp()) {
            m_projectController->setProjectFilePath(QString());
        }
        if (m_projectController) m_projectController->setSourceFolderIsTemp(false);
        m_session->layoutSourcePath = tempPath;
        m_session->layoutSourceIsList = false;
        // Set currentFolder to extraction root so relative subfolder structure is preserved
        m_session->currentFolder = tempPath;
        // Copy sprites into a dedicated subfolder on Replace
        const QString subName = m_projectController->computeSourceSubfolderName(sourcePath);
        const QString subfolderPath = QDir(m_session->sourceFolder).filePath(subName);
        m_session->activeFramePaths = m_projectController->copyFramesToSourceSubfolder(framePaths, subfolderPath);
        if (m_projectController) m_projectController->setShouldClearSpritesFolder(false);
        m_projectController->registerLoadedSource(sourcePath, action, subfolderPath);
    }

    if (!m_session->frameListPath.isEmpty()) {
        QFile::remove(m_session->frameListPath);
        m_session->frameListPath.clear();
    }

    // Ensure we generate a list file so spratlayout respects our sort order
    if (!ensureFrameListInput()) {
        m_statusLabel->setText(tr("Error: Could not create frame list for layout"));
        return;
    }

    m_statusLabel->setText(QString(tr("Loaded %1 frames")).arg(framePaths.size()));
    if (m_layoutOrchestrator) m_layoutOrchestrator->markCenterPivotsOnNextLayout();
    scheduleLayoutRebuild(true);
}
