#include "FolderSyncService.h"

#include <QDir>
#include <QFileInfo>
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
    QStringList folderImages = getImageFilesInFolder(folderPath);

    // Get current sprite paths
    QStringList currentPaths = getSpritePaths(currentSprites);

    // Detect added files (in folder but not in sprites)
    for (const QString& imagePath : folderImages) {
        if (!currentPaths.contains(imagePath)) {
            result.newImagePaths.append(imagePath);
        }
    }

    // Detect removed files (in sprites but not in folder)
    for (const QString& spritePath : currentPaths) {
        if (!folderImages.contains(spritePath)) {
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
    const SyncResult& changes) {

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

    // Add new sprites to layout
    for (const QString& imagePath : changes.newImagePaths) {
        auto newSprite = std::make_shared<Sprite>();
        newSprite->path = imagePath;

        // Extract filename for display name
        QFileInfo fileInfo(imagePath);
        newSprite->name = fileInfo.baseName();

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
    QStringList result;

    QDir dir(folderPath);
    QStringList nameFilters;
    nameFilters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.gif" << "*.webp"
                << "*.PNG" << "*.JPG" << "*.JPEG" << "*.BMP" << "*.GIF" << "*.WEBP";

    dir.setNameFilters(nameFilters);
    dir.setFilter(QDir::Files | QDir::Readable);

    const QFileInfoList fileList = dir.entryInfoList();
    for (const QFileInfo& fileInfo : fileList) {
        result.append(fileInfo.absoluteFilePath());
    }

    result.sort();
    return result;
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
