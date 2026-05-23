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
#include "FrameDetectionDialog.h"
#include "AnimatedImageImport.h"
#include "ArchiveExtractor.h"
#include "ImageDiscoveryService.h"
#include "SpriteNameUtils.h"
#include "AnimationPreviewService.h"

void MainWindow::loadImageWithFrameDetection(const QString& imagePath, DropAction action) {
    if (action == DropAction::Cancel) {
        return;
    }

    if (AnimatedImageImport::isAnimatedGif(imagePath)) {
        if (!m_cliReady || m_isLoading) {
            QMessageBox::warning(this, tr("Error"), tr("CLI tools not ready or already loading."));
            return;
        }

        m_loadingUiMessage = tr("Decoding animated GIF...");
        setLoading(true);

        if (action == DropAction::Replace) {
            m_session->clearTempDirs();
        }
        auto tempDir = std::make_unique<QTemporaryDir>();
        if (!tempDir->isValid()) {
            m_statusLabel->setText(tr("Error: Could not create temporary directory"));
            setLoading(false);
            return;
        }

        const QString tempPath = tempDir->path();
        m_session->addTempDir(std::move(tempDir));

        QString error;
        if (!AnimatedImageImport::extractAnimatedFrames(imagePath, tempPath, &error)) {
            setLoading(false);
            QMessageBox::warning(this, tr("Load Failed"), error.isEmpty() ? tr("Could not decode animated GIF.") : error);
            return;
        }

        if (processExtractedFrames(tempPath, imagePath, action)) {
            scheduleLayoutRebuild(true);
        }
        return;
    }

#ifdef Q_OS_WASM
    if (!m_cliReady || m_isLoading) {
        QMessageBox::warning(this, tr("Error"), tr("CLI tools not ready or already loading."));
        return;
    }
    
    if (m_spratFramesBin.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("spratframes binary not found."));
        return;
    }

    m_loadingUiMessage = tr("Detecting frames in image...");
    setLoading(true);

    QTimer::singleShot(0, this, [this, imagePath, action]() {
        FrameDetectionTaskResult result;
        result.imagePath = imagePath;
        result.action = action;
        result.detection = detectFramesInImage(imagePath);
        processFrameDetectionResult(result);
    });
#else
    if (!m_cliReady || m_isLoading) {
        QMessageBox::warning(this, tr("Error"), tr("CLI tools not ready or already loading."));
        return;
    }
    
    if (m_spratFramesBin.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("spratframes binary not found."));
        return;
    }

    if (m_frameDetectionWatcher.isRunning()) {
        return;
    }
    
    m_loadingUiMessage = tr("Detecting frames in image...");
    setLoading(true);

    auto task = [this, imagePath, action]() {
        FrameDetectionTaskResult result;
        result.imagePath = imagePath;
        result.action = action;
        result.detection = detectFramesInImage(imagePath);
        return result;
    };

    m_frameDetectionWatcher.setFuture(QtConcurrent::run(task));
#endif
}

void MainWindow::onFrameDetectionFinished() {
    processFrameDetectionResult(m_frameDetectionWatcher.result());
}

void MainWindow::processFrameDetectionResult(const FrameDetectionTaskResult& result) {
    setLoading(false);

    if (result.detection.frames.isEmpty()) {
        m_statusLabel->setText(tr("No frames detected, using image as single frame"));
        handleSingleImageLayout(result.imagePath, result.action, result.detection.backgroundColor);
        return;
    }

    FrameDetectionDialog dialog(result.imagePath, result.detection.frames, m_settings, result.detection.backgroundColor, this);
    if (dialog.exec() == QDialog::Accepted) {
        if (dialog.userAccepted()) {
            QVector<QRect> selectedFrames = dialog.getSelectedFrames();
            const DropAction action = result.action;
            const QString imagePath = result.imagePath;
            const QColor backgroundColor = result.detection.backgroundColor;

            m_loadingUiMessage = tr("Extracting frames...");
            setLoading(true);

            if (action == DropAction::Replace) {
                m_session->clearTempDirs();
            }
            auto tempDir = std::make_unique<QTemporaryDir>();
            if (!tempDir->isValid()) {
                m_statusLabel->setText(tr("Error: Could not create temporary directory"));
                setLoading(false);
                return;
            }
            QString tempPath = tempDir->path();
            m_session->addTempDir(std::move(tempDir));

            auto extractTask = [this, selectedFrames, imagePath, tempPath, action, backgroundColor]() {
                FrameExtractionResult res;
                res.tempPath = tempPath;
                res.sourcePath = imagePath;
                res.action = action;
                res.backgroundColor = backgroundColor;

                QString framesData = generateSpratFramesFormat(selectedFrames, imagePath);
                QStringList args;
                args << imagePath << "--frames" << "-" << "--output" << tempPath;
                QByteArray framesDataBytes = framesData.toUtf8();
                res.success = runTool(m_spratUnpackBin, args, &framesDataBytes);
                return res;
            };

#ifdef Q_OS_WASM
            QTimer::singleShot(0, this, [this, extractTask]() {
                processFrameExtractionResult(extractTask());
            });
#else
            m_frameExtractionWatcher.setFuture(QtConcurrent::run(extractTask));
#endif
        } else {
            m_statusLabel->setText(tr("Using image as single frame"));
            handleSingleImageLayout(result.imagePath, result.action, result.detection.backgroundColor);
        }
    } else {
        m_statusLabel->setText(tr("Frame detection cancelled"));
    }
}

void MainWindow::onFrameExtractionFinished() {
    processFrameExtractionResult(m_frameExtractionWatcher.result());
}

void MainWindow::processFrameExtractionResult(const FrameExtractionResult& result) {
    if (result.success) {
        if (processExtractedFrames(result.tempPath, result.sourcePath, result.action, result.backgroundColor)) {
            scheduleLayoutRebuild(true);
        }
    } else {
        m_statusLabel->setText(tr("Error running spratunpack"));
        setLoading(false);
    }
}

void MainWindow::loadTarFile(const QString& tarPath, DropAction action) {
    if (action == DropAction::Cancel) {
        return;
    }
    
    if (!m_cliReady || m_isLoading) {
        QMessageBox::warning(this, tr("Error"), tr("CLI tools not ready or already loading."));
        return;
    }
    
    if (m_spratUnpackBin.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("spratunpack binary not found."));
        return;
    }

    if (m_tarExtractionWatcher.isRunning()) {
        return;
    }
    
    m_loadingUiMessage = tr("Extracting frames from tar file...");
    m_isCanceled = false;
    setLoading(true);
    
    if (action == DropAction::Replace) {
        m_session->clearTempDirs();
    }
    auto tempDir = std::make_unique<QTemporaryDir>();
    if (!tempDir->isValid()) {
        m_statusLabel->setText(tr("Error: Could not create temporary directory"));
        setLoading(false);
        return;
    }
    QString tempPath = tempDir->path();
    m_session->addTempDir(std::move(tempDir));

    auto task = [this, tarPath, tempPath, action]() {
        TarExtractionResult result;
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

#ifdef Q_OS_WASM
    QTimer::singleShot(0, this, [this, task]() {
        processTarExtractionResult(task());
    });
#else
    m_tarExtractionWatcher.setFuture(QtConcurrent::run(task));
#endif
}

void MainWindow::onTarExtractionFinished() {
    processTarExtractionResult(m_tarExtractionWatcher.result());
}

void MainWindow::processTarExtractionResult(const TarExtractionResult& result) {
    if (m_isCanceled) {
        setLoading(false);
        m_statusLabel->setText(tr("Tar extraction canceled"));
        return;
    }
    if (result.success) {
        if (processExtractedFrames(result.tempPath, result.tarPath, result.action)) {
            scheduleLayoutRebuild(true);
        }
    } else {
        m_statusLabel->setText(tr("Error extracting tar file"));
        setLoading(false);
    }
}

MainWindow::FrameDetectionResult MainWindow::detectFramesInImage(const QString& imagePath) {
    FrameDetectionResult result;
    
    if (m_spratFramesBin.isEmpty()) {
        return result;
    }
    
    QByteArray output;
    if (!runTool(m_spratFramesBin, QStringList() << imagePath, nullptr, &output)) {
        qWarning() << "spratframes error";
        return result;
    }
    
    QString outputStr = QString::fromUtf8(output);
    
    // Parse spratframes output to extract frame rectangles
    // Expected format:
    // path <filepath>
    // background r,g,b
    // sprite x,y w,h
    // (possibly several sprite lines)
    QTextStream stream(&outputStr);
    QString line;
    while (stream.readLineInto(&line)) {
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        
        // Skip path lines
        if (line.startsWith("path ")) {
            continue;
        }

        // Parse background lines: "background r,g,b", "background r g b", or "background hex"
        if (line.startsWith("background")) {
            QString bgColorStr = line.mid(10).trimmed();
            if (bgColorStr.startsWith(":")) {
                bgColorStr = bgColorStr.mid(1).trimmed();
            }
            
            // Support hex colors (e.g., "b9fcc9" or "#b9fcc9")
            if (bgColorStr.startsWith("#") || (bgColorStr.length() == 6 && QRegularExpression("^[0-9a-fA-F]{6}$").match(bgColorStr).hasMatch())) {
                QString hexStr = bgColorStr.startsWith("#") ? bgColorStr : "#" + bgColorStr;
                result.backgroundColor = QColor(hexStr);
                if (result.backgroundColor.isValid()) {
                    continue;
                }
            }

            // Support both comma-separated and space-separated RGB values
            QStringList parts = bgColorStr.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
            if (parts.size() >= 3) {
                bool rOk, gOk, bOk;
                int r = parts[0].toInt(&rOk);
                int g = parts[1].toInt(&gOk);
                int b = parts[2].toInt(&bOk);
                if (rOk && gOk && bOk) {
                    result.backgroundColor = QColor(r, g, b);
                }
            }
            continue;
        }
        
        // Parse sprite lines: "sprite x,y w,h"
        if (line.startsWith("sprite ")) {
            QString spriteData = line.mid(7).trimmed();
            QStringList parts = spriteData.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                // Parse "x,y" and "w,h"
                QStringList posParts = parts[0].split(',');
                QStringList sizeParts = parts[1].split(',');
                
                if (posParts.size() == 2 && sizeParts.size() == 2) {
                    bool ok = true;
                    int x = posParts[0].toInt(&ok);
                    if (!ok) continue;
                    int y = posParts[1].toInt(&ok);
                    if (!ok) continue;
                    int w = sizeParts[0].toInt(&ok);
                    if (!ok) continue;
                    int h = sizeParts[1].toInt(&ok);
                    if (!ok) continue;
                    
                    result.frames.append(QRect(x, y, w, h));
                }
            }
        }
    }
    
    return result;
}

QString MainWindow::generateSpratFramesFormat(const QVector<QRect>& frames, const QString& imagePath) {
    QString format;
    QTextStream stream(&format);
    
    stream << "path \"" << imagePath << "\"\n";
    for (const QRect& frame : frames) {
        stream << "sprite " << frame.x() << "," << frame.y() << " " 
               << frame.width() << "," << frame.height() << "\n";
    }
    
    return format;
}

void MainWindow::applyTransparencyToImage(QImage& img, const QColor& backgroundColor) {
    if (!backgroundColor.isValid()) return;

    QRgb target = backgroundColor.rgb();
    int tr = qRed(target);
    int tg = qGreen(target);
    int tb = qBlue(target);
    const int tolerance = 15; // Handle JPEG artifacts

    if (img.format() != QImage::Format_ARGB32) {
        img = img.convertToFormat(QImage::Format_ARGB32);
    }

    for (int y = 0; y < img.height(); ++y) {
        QRgb* scanLine = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            QRgb pixel = scanLine[x];
            if (qAbs(qRed(pixel) - tr) <= tolerance &&
                qAbs(qGreen(pixel) - tg) <= tolerance &&
                qAbs(qBlue(pixel) - tb) <= tolerance) {
                scanLine[x] = 0;
            }
        }
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
                applyTransparencyToImage(img, backgroundColor);
                if (img.save(outputPath)) {
                    finalPath = outputPath;
                    m_session->addTempDir(std::move(tempDir));
                    // Retain image size to avoid redundant reload later
                    m_singleImageDimensions = img.size();
                }
            }
        }
    }

    if (action == DropAction::Merge) {
        // Ask if files with same names should be replaced
        QMessageBox msg(this);
        msg.setWindowTitle(tr("Merge with duplicates"));
        msg.setText(tr("When merging, what should happen to files with the same name?"));
        QAbstractButton* replaceBtn = msg.addButton(tr("Replace"), QMessageBox::AcceptRole);
        msg.addButton(tr("Rename"), QMessageBox::AcceptRole);
        msg.exec();
        m_mergeReplaceAllDuplicates = (msg.clickedButton() == replaceBtn);

        m_session->activeFramePaths.append(finalPath);
        // Copy the image to source folder so it persists after temp dir cleanup
        copyActiveFramesToSourceFolder(m_mergeReplaceAllDuplicates);
        ensureFrameListInput();
        m_shouldClearSpritesFolder = false;
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
    m_session->clearSourceFolderTempDir();
    ensureSourceFolder();
    m_session->timelines.clear();
    m_session->selectedTimelineIndex = -1;
    // Clear selection state when loading new image to avoid stale pointers
    m_session->selectedSprite.reset();
    m_session->selectedSprites.clear();
    m_session->selectedPointName.clear();
    // Update UI to clear sprite selection display
    onSpriteSelected(SpritePtr());
    refreshTimelineList();
    refreshAnimationTest();

    // Copy the single image to source folder on Replace
    m_session->activeFramePaths = { finalPath };
    copyActiveFramesToSourceFolder();
    m_shouldClearSpritesFolder = false;
    // After copying, update to the copied path
    QString copiedPath = m_session->activeFramePaths.first();

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
    m_session->layoutModels = { singleImageModel };
    AnimationPreviewService::invalidateSpriteMap();
    ensureUniqueSpriteNames(m_session->layoutModels, m_session->sourceFolder);
    if (m_canvas) {
        m_loadingUiMessage = tr("Loading image...");
        setLoading(true);
        m_canvas->setModelsAsync(m_session->layoutModels, &m_isCanceled, [this, sprite]() {
            if (m_isCanceled) {
                setLoading(false);
                return;
            }
            m_canvas->setZoomManual(false);
            QTimer::singleShot(0, m_canvas, &LayoutCanvas::initialFit);
            
            m_statusLabel->setText(QString(tr("Loaded single image: %1")).arg(sprite->name));
            // Update UI state
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
    // Collect images recursively to preserve subfolder structure from archives
    QStringList framePaths = ImageDiscoveryService::collectImagesRecursive({tempPath});

    if (framePaths.isEmpty()) {
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
                    applyTransparencyToImage(img, backgroundColor);
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
        // Ask if files with same names should be replaced
        QMessageBox msg(this);
        msg.setWindowTitle(tr("Merge with duplicates"));
        msg.setText(tr("When merging, what should happen to files with the same name?"));
        QAbstractButton* replaceBtn = msg.addButton(tr("Replace"), QMessageBox::AcceptRole);
        msg.addButton(tr("Rename"), QMessageBox::AcceptRole);
        msg.exec();
        m_mergeReplaceAllDuplicates = (msg.clickedButton() == replaceBtn);

        m_session->activeFramePaths.append(framePaths);
        // Copy extracted frames to source folder so they persist after temp dir cleanup
        copyActiveFramesToSourceFolder(m_mergeReplaceAllDuplicates);
        ensureFrameListInput();
        m_shouldClearSpritesFolder = false;
    } else {
        // On Replace, delete all contents from sprites folder (including subdirectories)
        if (!m_session->sourceFolder.isEmpty()) {
            QDir dir(m_session->sourceFolder);
            dir.removeRecursively();
            QDir().mkpath(m_session->sourceFolder);
        }

        m_session->sourceFolder.clear();
        m_session->clearSourceFolderTempDir();
        ensureSourceFolder();
        m_session->timelines.clear();
        m_session->selectedTimelineIndex = -1;
        // Clear selection state when loading new frames to avoid stale pointers
        m_session->selectedSprite.reset();
        m_session->selectedSprites.clear();
        m_session->selectedPointName.clear();
        // Update UI to clear sprite selection display
        onSpriteSelected(SpritePtr());
        refreshTimelineList();
        refreshAnimationTest();
        if (m_sourceFolderIsTemp) {
            m_projectFilePath.clear();
        }
        m_sourceFolderIsTemp = false;
        m_session->activeFramePaths = framePaths;
        m_session->layoutSourcePath = tempPath;
        m_session->layoutSourceIsList = false;
        // Set currentFolder to extraction root so copyActiveFramesToSourceFolder
        // preserves the subfolder hierarchy from the archive
        m_session->currentFolder = tempPath;
        // Copy sprites to source folder on Replace
        copyActiveFramesToSourceFolder();
        m_shouldClearSpritesFolder = false;
    }
    
    if (!m_session->frameListPath.isEmpty()) {
        QFile::remove(m_session->frameListPath);
        m_session->frameListPath.clear();
    }
    
    // Ensure we generate a list file so spratlayout respects our sort order
    if (!ensureFrameListInput()) {
        m_statusLabel->setText(tr("Error: Could not create frame list for layout"));
        setLoading(false);
        return false;
    }
    
    m_statusLabel->setText(QString(tr("Loaded %1 frames")).arg(framePaths.size()));
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
        // Ask if files with same names should be replaced
        QMessageBox msg(this);
        msg.setWindowTitle(tr("Merge with duplicates"));
        msg.setText(tr("When merging, what should happen to files with the same name?"));
        QAbstractButton* replaceBtn = msg.addButton(tr("Replace"), QMessageBox::AcceptRole);
        msg.addButton(tr("Rename"), QMessageBox::AcceptRole);
        msg.exec();
        m_mergeReplaceAllDuplicates = (msg.clickedButton() == replaceBtn);

        m_session->activeFramePaths.append(framePaths);
        // Copy extracted frames to source folder so they persist after temp dir cleanup
        copyActiveFramesToSourceFolder(m_mergeReplaceAllDuplicates);
        ensureFrameListInput();
        m_shouldClearSpritesFolder = false;
        scheduleLayoutRebuild(true);
    } else {
        // On Replace, delete all contents from sprites folder (including subdirectories)
        if (!m_session->sourceFolder.isEmpty()) {
            QDir dir(m_session->sourceFolder);
            dir.removeRecursively();
            QDir().mkpath(m_session->sourceFolder);
        }

        m_session->sourceFolder.clear();
        m_session->clearSourceFolderTempDir();
        ensureSourceFolder();
        m_session->timelines.clear();
        m_session->selectedTimelineIndex = -1;
        // Clear selection state when loading new frames to avoid stale pointers
        m_session->selectedSprite.reset();
        m_session->selectedSprites.clear();
        m_session->selectedPointName.clear();
        // Update UI to clear sprite selection display
        onSpriteSelected(SpritePtr());
        refreshTimelineList();
        refreshAnimationTest();
        if (m_sourceFolderIsTemp) {
            m_projectFilePath.clear();
        }
        m_sourceFolderIsTemp = false;
        m_session->activeFramePaths = framePaths;
        m_session->layoutSourcePath = tempPath;
        m_session->layoutSourceIsList = false;
        // Set currentFolder to extraction root so copyActiveFramesToSourceFolder
        // preserves the subfolder hierarchy from the archive
        m_session->currentFolder = tempPath;
        // Copy sprites to source folder on Replace
        copyActiveFramesToSourceFolder();
        m_shouldClearSpritesFolder = false;
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
    scheduleLayoutRebuild(true);
}
