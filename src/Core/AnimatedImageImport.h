#pragma once

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QString>

namespace AnimatedImageImport {

inline bool isAnimatedGif(const QString& imagePath) {
    if (!imagePath.toLower().endsWith(".gif")) {
        return false;
    }

    QImageReader reader(imagePath);
    return reader.supportsAnimation() && reader.imageCount() != 1;
}

inline bool extractAnimatedFrames(const QString& imagePath, const QString& outputDir, QString* error = nullptr) {
    QImageReader reader(imagePath);
    if (!reader.supportsAnimation()) {
        if (error) {
            *error = QStringLiteral("Image format does not support animation.");
        }
        return false;
    }

    if (!QDir(outputDir).exists() && !QDir().mkpath(outputDir)) {
        if (error) {
            *error = QStringLiteral("Could not create output directory.");
        }
        return false;
    }

    int frameIndex = 0;
    while (reader.canRead()) {
        const QImage frame = reader.read();
        if (frame.isNull()) {
            if (frameIndex == 0 && error) {
                *error = reader.errorString();
            }
            return frameIndex > 0;
        }

        const QString framePath = QDir(outputDir).filePath(QStringLiteral("frame_%1.png").arg(frameIndex, 4, 10, QChar('0')));
        if (!frame.save(framePath, "PNG")) {
            if (error) {
                *error = QStringLiteral("Could not save decoded animation frame.");
            }
            return false;
        }
        ++frameIndex;
    }

    if (frameIndex <= 1) {
        if (error) {
            *error = QStringLiteral("Animation did not contain multiple frames.");
        }
        return false;
    }

    return true;
}

}
