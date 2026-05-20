#include "FolderSyncService.h"
#include "ImageDiscoveryService.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QDebug>
#include <algorithm>

FolderSyncService::SyncResult FolderSyncService::detectChanges(
    const QString& folderPath,
    const QVector<SpritePtr>& currentSprites) {

    SyncResult result;

    if (folderPath.isEmpty()) {
        result.error = "Source folder path is empty";
        return result;
    }

    QDir dir(folderPath);
    if (!dir.exists()) {
        result.error = "Source folder does not exist: " + folderPath;
        return result;
    }

    // Get current image files in folder
    const QStringList folderImagesList = getImageFilesInFolder(folderPath);
    const QSet<QString> folderImagesSet(folderImagesList.cbegin(), folderImagesList.cend());

    // Get current sprite paths
    const QStringList currentPaths = getSpritePaths(currentSprites);
    const QSet<QString> currentPathsSet(currentPaths.cbegin(), currentPaths.cend());

    // Detect added files (in folder but not in sprites) — O(N) with QSet lookup
    for (const QString& imagePath : folderImagesList) {
        if (!currentPathsSet.contains(imagePath)) {
            result.newImagePaths.append(imagePath);
        }
    }

    // Detect removed files (in sprites but not in folder) — O(M) with QSet lookup
    for (const QString& spritePath : currentPaths) {
        if (!folderImagesSet.contains(spritePath)) {
            result.deletedImagePaths.append(spritePath);
        }
    }

    qInfo() << "FolderSyncService: Detected"
            << result.newImagePaths.size() << "new files,"
            << result.deletedImagePaths.size() << "deleted files";

    return result;
}

bool FolderSyncService::mergeSyncResults(
    LayoutModel& layout,
    const SyncResult& changes,
    const QString& sourceFolder) {

    if (changes.error.isEmpty() == false) {
        qWarning() << "FolderSyncService: Cannot merge with error:" << changes.error;
        return false;
    }

    // Remove deleted sprites from layout
    for (const QString& deletedPath : changes.deletedImagePaths) {
        auto it = std::find_if(layout.sprites.begin(), layout.sprites.end(),
            [&deletedPath](const SpritePtr& sprite) { return sprite && sprite->path == deletedPath; });
        if (it != layout.sprites.end()) {
            qInfo() << "FolderSyncService: Removed sprite" << (*it)->name;
            layout.sprites.erase(it);
        }
    }

    // Organize new images by pattern before adding to layout
    const QStringList organizedPaths = organizeNewImagesByPattern(changes.newImagePaths);

    // Add new sprites to layout
    for (const QString& imagePath : organizedPaths) {
        auto newSprite = std::make_shared<Sprite>();
        newSprite->path = imagePath;

        // Derive name from relative path within sourceFolder when available
        if (!sourceFolder.isEmpty()) {
            QString rel = QDir(sourceFolder).relativeFilePath(imagePath);
            QFileInfo relInfo(rel);
            newSprite->name = (relInfo.path() == ".")
                ? relInfo.baseName()
                : relInfo.path() + "/" + relInfo.baseName();
        } else {
            QFileInfo fileInfo(imagePath);
            newSprite->name = fileInfo.baseName();
        }

        // Initialize with default values
        newSprite->rect = QRect(0, 0, 0, 0);  // Will be set by layout generation
        newSprite->trimmed = false;
        newSprite->rotated = false;
        newSprite->pivotX = 0;
        newSprite->pivotY = 0;

        layout.sprites.append(newSprite);
        qInfo() << "FolderSyncService: Added sprite" << newSprite->name;
    }

    qInfo() << "FolderSyncService: Merged" << changes.newImagePaths.size() << "new sprites,"
            << changes.deletedImagePaths.size() << "deleted sprites";
    return true;
}

QString FolderSyncService::describeSyncResult(const SyncResult& result) {
    QStringList parts;

    if (!result.newImagePaths.isEmpty()) {
        parts.append(QString::number(result.newImagePaths.size()) + " new sprite(s) added");
    }

    if (!result.deletedImagePaths.isEmpty()) {
        parts.append(QString::number(result.deletedImagePaths.size()) + " sprite(s) deleted from folder");
    }

    if (parts.isEmpty()) {
        return "No changes detected";
    }

    return parts.join("; ");
}

QStringList FolderSyncService::getImageFilesInFolder(const QString& folderPath) {
    // Use the optimized recursive collector which skips .git, node_modules, etc.
    return ImageDiscoveryService::collectImagesRecursive({folderPath});
}

QStringList FolderSyncService::getSpritePaths(const QVector<SpritePtr>& sprites) {
    QStringList result;

    for (const auto& sprite : sprites) {
        if (sprite) {
            result.append(sprite->path);
        }
    }

    return result;
}

bool FolderSyncService::isImageFile(const QString& filePath) {
    static const QStringList extensions = {
        ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp"
    };

    QString ext = QFileInfo(filePath).suffix().toLower();
    return extensions.contains("." + ext);
}

QStringList FolderSyncService::organizeNewImagesByPattern(const QStringList& newImagePaths) {
    if (newImagePaths.isEmpty())
        return newImagePaths;

    // Group files by their name pattern (prefix before trailing digits)
    QMap<QString, QStringList> patterns; // prefix -> list of file paths
    QStringList noPattern; // files without trailing digits

    for (const QString& path : newImagePaths) {
        QFileInfo fi(path);
        QString baseName = fi.baseName();

        // Extract prefix by removing trailing digits
        int end = baseName.size();
        while (end > 0 && baseName[end - 1].isDigit()) {
            --end;
        }

        if (end > 0 && end < baseName.size()) {
            // Has trailing digits
            QString prefix = baseName.left(end);
            patterns[prefix].append(path);
        } else {
            // No trailing digits
            noPattern.append(path);
        }
    }

    // Check if we need to organize
    // We organize only if:
    // 1. There are multiple different patterns with numbered files, OR
    // 2. There are mixed numbered and non-numbered files
    int patternsWithMultiple = 0;
    for (auto it = patterns.constBegin(); it != patterns.constEnd(); ++it) {
        if (it.value().size() > 1) {
            ++patternsWithMultiple;
        }
    }

    // If all files follow the same pattern (or no pattern), don't organize
    if (patternsWithMultiple <= 1 && noPattern.isEmpty()) {
        return newImagePaths;
    }

    // Organize files into subfolders by pattern
    QStringList result;

    // Handle non-patterned files
    for (const QString& path : noPattern) {
        result.append(path); // Keep them at current location
    }

    // Handle patterned files - move to subfolders
    for (auto it = patterns.constBegin(); it != patterns.constEnd(); ++it) {
        const QString& prefix = it.key();
        const QStringList& paths = it.value();

        // Only organize if there are multiple files with this pattern
        if (paths.size() <= 1) {
            result.append(paths);
            continue;
        }

        QFileInfo firstFile(paths.first());
        QString parentDir = firstFile.absolutePath();

        // Create target subfolder name
        QString targetFolder = parentDir + '/' + prefix;
        QDir targetDir(targetFolder);

        // Handle existing folder collision
        int counter = 2;
        while (targetDir.exists()) {
            targetFolder = parentDir + '/' + prefix + '_' + QString::number(counter);
            targetDir.setPath(targetFolder);
            ++counter;
        }

        // Create the subfolder
        if (!QDir().mkpath(targetFolder)) {
            qWarning() << "FolderSyncService: Failed to create folder" << targetFolder;
            result.append(paths);
            continue;
        }

        // Move files to subfolder
        for (const QString& oldPath : paths) {
            QFileInfo fi(oldPath);
            QString newPath = targetFolder + '/' + fi.fileName();

            if (QFile::rename(oldPath, newPath)) {
                qInfo() << "FolderSyncService: Moved" << oldPath << "to" << newPath;
                result.append(newPath);
            } else {
                qWarning() << "FolderSyncService: Failed to move" << oldPath << "to" << newPath;
                result.append(oldPath);
            }
        }
    }

    return result;
}
