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
#include <algorithm>

#include "MainWindow.h"
#include "FrameDetectionDialog.h"

void MainWindow::loadImageWithFrameDetection(const QString& imagePath, DropAction action) {
    if (action == DropAction::Replace && !confirmLayoutReplacement()) {
        return;
    }
    
    if (action == DropAction::Cancel) {
        return;
    }
    
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
    
    // Use spratframes to detect frames in the image
    FrameDetectionResult detection = detectFramesInImage(imagePath);
    setLoading(false);
    
    if (detection.frames.isEmpty()) {
        // No frames detected by spratframes, use the image as a single frame
        m_statusLabel->setText(tr("No frames detected, using image as single frame"));
        handleSingleImageLayout(imagePath, action, detection.backgroundColor);
        return;
    }
    
    // Show modal dialog with detected frames
    FrameDetectionDialog dialog(imagePath, detection.frames, m_settings, detection.backgroundColor, this);
    if (dialog.exec() == QDialog::Accepted) {
        if (dialog.userAccepted()) {
            m_loadingUiMessage = tr("Extracting frames...");
            setLoading(true);
            
            // ... (keep the same)
            QVector<QRect> selectedFrames = dialog.getSelectedFrames();
            
            // Use persistent temp dir in session to ensure files persist
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
            
            // Generate spratframes format
            QString framesData = generateSpratFramesFormat(selectedFrames, imagePath);
            
            // Use spratunpack with the correct command syntax
            // spratunpack <input_image> --frames - --output <output_directory>
            QProcess unpackProcess;
            QStringList args;
            args << imagePath << "--frames" << "-" << "--output" << tempPath;
            unpackProcess.start(m_spratUnpackBin, args);
            
            // Write frames data to stdin
            unpackProcess.write(framesData.toUtf8());
            unpackProcess.closeWriteChannel();
            
            unpackProcess.waitForFinished();
            
            if (unpackProcess.exitCode() == 0) {
                if (processExtractedFrames(tempPath, imagePath, action, detection.backgroundColor)) {
                    onRunLayout();
                }
            } else {
                QString error = QString::fromUtf8(unpackProcess.readAllStandardError());
                m_statusLabel->setText(tr("Error running spratunpack: ") + error);
                setLoading(false);
            }
        } else {
            // User rejected, use image as single frame
            m_statusLabel->setText(tr("Using image as single frame"));
            handleSingleImageLayout(imagePath, action, detection.backgroundColor);
        }
    } else {
        // User cancelled the dialog
        m_statusLabel->setText(tr("Frame detection cancelled"));
        setLoading(false);
    }
}

void MainWindow::loadTarFile(const QString& tarPath, DropAction action) {
    if (action == DropAction::Replace && !confirmLayoutReplacement()) {
        return;
    }
    
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
    
    m_loadingUiMessage = tr("Extracting frames from tar file...");
    setLoading(true);
    
    // Use persistent temp dir in session to ensure files persist
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

    // Use tar directly to extract the file
    // tar -xf <tarPath> -C <tempDir>
    QProcess tarProcess;
    tarProcess.setProgram("tar");
    tarProcess.setArguments(QStringList() << "-xf" << tarPath << "-C" << tempPath);

    tarProcess.start();
    bool finished = tarProcess.waitForFinished();
    
    if (!finished || tarProcess.exitCode() != 0) {
        QString error = QString::fromUtf8(tarProcess.readAllStandardError());
        m_statusLabel->setText(tr("Error extracting tar file: ") + error);
        setLoading(false);
        return;
    }
    
    if (processExtractedFrames(tempPath, tarPath, action)) {
        onRunLayout();
    }
}

MainWindow::FrameDetectionResult MainWindow::detectFramesInImage(const QString& imagePath) {
    FrameDetectionResult result;
    
    if (m_spratFramesBin.isEmpty()) {
        return result;
    }
    
    QProcess framesProcess;
    // Use spratframes to detect frames in the image
    framesProcess.start(m_spratFramesBin, QStringList() << imagePath);
    framesProcess.waitForFinished();
    
    if (framesProcess.exitCode() != 0) {
        QString error = QString::fromUtf8(framesProcess.readAllStandardError());
        qWarning() << "spratframes error:" << error;
        return result;
    }
    
    QString output = QString::fromUtf8(framesProcess.readAllStandardOutput());
    
    // Parse spratframes output to extract frame rectangles
    // Expected format:
    // path <filepath>
    // background r,g,b
    // sprite x,y w,h
    // (possibly several sprite lines)
    QTextStream stream(&output);
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
    
    stream << "path " << imagePath << "\n";
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
                }
            }
        }
    }

    if (action == DropAction::Merge) {
        m_session->activeFramePaths.append(finalPath);
        onRunLayout();
        return;
    }

    // For a single image, we need to create a simple layout model
    LayoutModel singleImageModel;
    singleImageModel.scale = 1.0;
    
    // Create a sprite for the single image
    SpritePtr sprite = std::make_shared<Sprite>();
    sprite->path = finalPath;
    sprite->name = QFileInfo(imagePath).baseName();
    
    // Get image dimensions
    QImage img(finalPath);
    if (!img.isNull()) {
        sprite->rect = QRect(0, 0, img.width(), img.height());
        sprite->pivotX = img.width() / 2;
        sprite->pivotY = img.height() / 2;
    } else {
        // Fallback dimensions if image can't be loaded
        sprite->rect = QRect(0, 0, 100, 100);
        sprite->pivotX = 50;
        sprite->pivotY = 50;
    }
    
    singleImageModel.sprites.append(sprite);

    // Apply the model to the canvas
    m_session->layoutModels = { singleImageModel };
    if (m_canvas) {
        m_canvas->setModels(m_session->layoutModels);
        QTimer::singleShot(0, m_canvas, &LayoutCanvas::initialFit);
    }
    m_statusLabel->setText(QString(tr("Loaded single image: %1")).arg(sprite->name));    
    // Update UI state
    populateActiveFrameListFromModel();
    updateMainContentView();
    updateUiState();
    
    setLoading(false);
}

bool MainWindow::processExtractedFrames(const QString& tempPath, const QString& sourcePath, DropAction action, const QColor& backgroundColor) {
    QDir extractDir(tempPath);
    QStringList imageFiles = extractDir.entryList(QStringList() << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.gif" << "*.webp" << "*.tga" << "*.dds", QDir::Files);
    
    if (imageFiles.isEmpty()) {
        m_statusLabel->setText(tr("No image files found after extraction"));
        setLoading(false);
        return false;
    }

    // If a background color is provided, make it transparent in all extracted frames
    if (backgroundColor.isValid()) {
        for (const QString& fileName : imageFiles) {
            QString filePath = extractDir.absoluteFilePath(fileName);
            QImage img(filePath);
            if (!img.isNull()) {
                applyTransparencyToImage(img, backgroundColor);
                img.save(filePath);
            }
        }
    }

    // Sort frames naturally (e.g. 1, 2, 10 instead of 1, 10, 2)
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(imageFiles.begin(), imageFiles.end(), collator);
    
    QStringList framePaths;
    framePaths.reserve(imageFiles.size());
    for (const QString& fileName : imageFiles) {
        framePaths.append(extractDir.absoluteFilePath(fileName));
    }
    
    if (action == DropAction::Merge) {
        m_session->activeFramePaths.append(framePaths);
    } else {
        m_session->activeFramePaths = framePaths;
        m_session->layoutSourcePath = tempPath;
        m_session->layoutSourceIsList = false;
        m_session->currentFolder = QFileInfo(sourcePath).absoluteDir().absolutePath();
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
