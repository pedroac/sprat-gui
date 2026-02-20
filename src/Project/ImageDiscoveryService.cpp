#include "ImageDiscoveryService.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace {
const QStringList kSupportedImageFilters = {
    "*.png", "*.jpg", "*.jpeg", "*.bmp", "*.gif", "*.webp", "*.tga", "*.dds"
};
}

const QStringList& ImageDiscoveryService::supportedImageFilters() {
    return kSupportedImageFilters;
}

bool ImageDiscoveryService::hasImageFiles(const QString& path) {
    QDir dir(path);
    if (!dir.exists()) {
        return false;
    }
    return !dir.entryList(supportedImageFilters(), QDir::Files).isEmpty();
}

QStringList ImageDiscoveryService::imageDirectoriesOneLevel(const QString& root) {
    QStringList directories;
    QDir base(root);
    if (!base.exists()) {
        return directories;
    }

    if (hasImageFiles(root)) {
        directories.append(base.absolutePath());
    }

    for (const QString& entry : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString candidate = QDir(base.filePath(entry)).absolutePath();
        if (hasImageFiles(candidate)) {
            directories.append(candidate);
        }
    }

    directories.removeDuplicates();
    std::sort(directories.begin(), directories.end());
    return directories;
}

QStringList ImageDiscoveryService::imageDirectoriesRecursive(const QString& root) {
    QStringList imageDirs;
    QDir base(root);
    if (!base.exists()) {
        return imageDirs;
    }

    if (hasImageFiles(base.absolutePath())) {
        imageDirs.append(base.absolutePath());
    }

    QDirIterator it(base.absolutePath(), QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString dirPath = QDir(it.next()).absolutePath();
        if (hasImageFiles(dirPath)) {
            imageDirs.append(dirPath);
        }
    }

    imageDirs.removeDuplicates();
    std::sort(imageDirs.begin(), imageDirs.end());
    return imageDirs;
}

QStringList ImageDiscoveryService::imagesInDirectory(const QString& path) {
    QStringList absolutePaths;
    QDir dir(path);
    if (!dir.exists()) {
        return absolutePaths;
    }

    const QStringList relativePaths = dir.entryList(supportedImageFilters(), QDir::Files);
    absolutePaths.reserve(relativePaths.size());
    for (const QString& relPath : relativePaths) {
        absolutePaths.append(dir.absoluteFilePath(relPath));
    }
    std::sort(absolutePaths.begin(), absolutePaths.end());
    return absolutePaths;
}

QStringList ImageDiscoveryService::collectImagesRecursive(const QStringList& roots) {
    QStringList absolutePaths;
    QSet<QString> seen;

    for (const QString& root : roots) {
        QDirIterator imageIt(root, supportedImageFilters(), QDir::Files, QDirIterator::Subdirectories);
        while (imageIt.hasNext()) {
            const QString absPath = QFileInfo(imageIt.next()).absoluteFilePath();
            if (seen.contains(absPath)) {
                continue;
            }
            seen.insert(absPath);
            absolutePaths.append(absPath);
        }
    }

    std::sort(absolutePaths.begin(), absolutePaths.end());
    return absolutePaths;
}
