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
#include <algorithm>

#include "MainWindow.h"
#include "FrameDetectionDialog.h"

void MainWindow::loadImageWithFrameDetection(const QString& imagePath, bool confirmReplace) {
    if (confirmReplace && !confirmLayoutReplacement()) {
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
    QVector<QRect> detectedFrames = detectFramesInImage(imagePath);
    
    if (detectedFrames.isEmpty()) {
        // No frames detected by spratframes, use the image as a single frame
        m_statusLabel->setText(tr("No frames detected, using image as single frame"));
        handleSingleImageLayout(imagePath);
        return;
    }
    
    // Show modal dialog with detected frames
    FrameDetectionDialog dialog(imagePath, detectedFrames, this);
    if (dialog.exec() == QDialog::Accepted) {
        if (dialog.userAccepted()) {
            // User accepted the detected frames, generate spratframes format and use spratunpack
            QVector<QRect> selectedFrames = dialog.getSelectedFrames();
            
            // Use persistent temp dir (m_zipTempDir) instead of local one to ensure files persist
            clearZipTempDir();
            m_zipTempDir = new QTemporaryDir();
            if (!m_zipTempDir->isValid()) {
                m_statusLabel->setText(tr("Error: Could not create temporary directory"));
                setLoading(false);
                delete m_zipTempDir;
                m_zipTempDir = nullptr;
                return;
            }
            
            // Generate spratframes format
            QString framesData = generateSpratFramesFormat(selectedFrames, imagePath);
            
            // Use spratunpack with the correct command syntax
            // spratunpack <input_image> --frames - --output <output_directory>
            QProcess unpackProcess;
            QStringList args;
            args << imagePath << "--frames" << "-" << "--output" << m_zipTempDir->path();
            unpackProcess.start(m_spratUnpackBin, args);
            
            // Write frames data to stdin
            unpackProcess.write(framesData.toUtf8());
            unpackProcess.closeWriteChannel();
            
            unpackProcess.waitForFinished();
            
            if (unpackProcess.exitCode() == 0) {
                if (processExtractedFrames(m_zipTempDir->path(), imagePath)) {
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
            handleSingleImageLayout(imagePath);
        }
    } else {
        // User cancelled the dialog
        m_statusLabel->setText(tr("Frame detection cancelled"));
        setLoading(false);
    }
}

void MainWindow::loadTarFile(const QString& tarPath, bool confirmReplace) {
    if (confirmReplace && !confirmLayoutReplacement()) {
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
    
    // Use persistent temp dir (m_zipTempDir) to ensure files persist for spratlayout
    clearZipTempDir();
    m_zipTempDir = new QTemporaryDir();
    if (!m_zipTempDir->isValid()) {
        m_statusLabel->setText(tr("Error: Could not create temporary directory"));
        setLoading(false);
        delete m_zipTempDir;
        m_zipTempDir = nullptr;
        return;
    }

    // Use tar directly to extract the file
    // tar -xf <tarPath> -C <tempDir>
    QProcess tarProcess;
    tarProcess.setProgram("tar");
    tarProcess.setArguments(QStringList() << "-xf" << tarPath << "-C" << m_zipTempDir->path());

    tarProcess.start();
    bool finished = tarProcess.waitForFinished();
    
    if (!finished || tarProcess.exitCode() != 0) {
        QString error = QString::fromUtf8(tarProcess.readAllStandardError());
        m_statusLabel->setText(tr("Error extracting tar file: ") + error);
        setLoading(false);
        return;
    }
    
    if (processExtractedFrames(m_zipTempDir->path(), tarPath)) {
        onRunLayout();
    }
}

QVector<QRect> MainWindow::detectFramesInImage(const QString& imagePath) {
    QVector<QRect> frames;
    
    if (m_spratFramesBin.isEmpty()) {
        return frames;
    }
    
    QProcess framesProcess;
    // Use spratframes to detect frames in the image
    framesProcess.start(m_spratFramesBin, QStringList() << imagePath);
    framesProcess.waitForFinished();
    
    if (framesProcess.exitCode() != 0) {
        QString error = QString::fromUtf8(framesProcess.readAllStandardError());
        qWarning() << "spratframes error:" << error;
        return frames;
    }
    
    QString output = QString::fromUtf8(framesProcess.readAllStandardOutput());
    
    // Parse spratframes output to extract frame rectangles
    // Expected format:
    // path <filepath>
    // sprite x,y w,h
    // (possibly several sprite lines)
    QTextStream stream(&output);
    QString line;
    while (stream.readLineInto(&line)) {
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        
        // Skip path and background lines
        if (line.startsWith("path ") || line.startsWith("background ")) {
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
                    
                    frames.append(QRect(x, y, w, h));
                }
            }
        }
    }
    
    return frames;
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

void MainWindow::handleSingleImageLayout(const QString& imagePath) {
    // For a single image, we need to create a simple layout model
    // This simulates what spratlayout would do for a single image
    
    LayoutModel singleImageModel;
    singleImageModel.scale = 1.0;
    
    // Create a sprite for the single image
    SpritePtr sprite = std::make_shared<Sprite>();
    sprite->path = imagePath;
    sprite->name = QFileInfo(imagePath).baseName();
    
    // Get image dimensions
    QPixmap pixmap(imagePath);
    if (!pixmap.isNull()) {
        sprite->rect = QRect(0, 0, pixmap.width(), pixmap.height());
        sprite->pivotX = pixmap.width() / 2;
        sprite->pivotY = pixmap.height() / 2;
    } else {
        // Fallback dimensions if image can't be loaded
        sprite->rect = QRect(0, 0, 100, 100);
        sprite->pivotX = 50;
        sprite->pivotY = 50;
    }
    
    singleImageModel.sprites.append(sprite);
    
    // Apply the model to the canvas
    m_layoutModel = singleImageModel;
    m_canvas->setModel(m_layoutModel);
    QTimer::singleShot(0, m_canvas, &LayoutCanvas::initialFit);
    m_statusLabel->setText(QString(tr("Loaded single image: %1")).arg(sprite->name));
    
    // Update UI state
    populateActiveFrameListFromModel();
    updateMainContentView();
    updateUiState();
    
    setLoading(false);
}

bool MainWindow::processExtractedFrames(const QString& tempPath, const QString& sourcePath) {
    QDir extractDir(tempPath);
    QStringList imageFiles = extractDir.entryList(QStringList() << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.gif" << "*.webp" << "*.tga" << "*.dds", QDir::Files);
    
    if (imageFiles.isEmpty()) {
        m_statusLabel->setText(tr("No image files found after extraction"));
        setLoading(false);
        return false;
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
    
    m_activeFramePaths = framePaths;
    m_layoutSourcePath = tempPath;
    m_layoutSourceIsList = false;
    m_currentFolder = QFileInfo(sourcePath).absoluteDir().absolutePath();
    
    if (!m_frameListPath.isEmpty()) {
        QFile::remove(m_frameListPath);
        m_frameListPath.clear();
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
