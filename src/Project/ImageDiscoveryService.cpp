#include "ImageDiscoveryService.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSet>
#include <QDebug>

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
    QElapsedTimer timer;
    timer.start();
    QStringList directories;
    QDir base(root);
    if (!base.exists()) {
        return directories;
    }

    if (hasImageFiles(root)) {
        directories.append(base.absolutePath());
    }

    const QStringList subdirs = base.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : subdirs) {
        const QString candidate = QDir(base.filePath(entry)).absolutePath();
        if (hasImageFiles(candidate)) {
            directories.append(candidate);
        }
    }

    directories.removeDuplicates();
    std::sort(directories.begin(), directories.end());
    qInfo() << "[WASM] imageDirectoriesOneLevel root=" << root
            << "subdirs=" << subdirs.size()
            << "imageDirs=" << directories.size()
            << "ms=" << timer.elapsed();
    return directories;
}

QStringList ImageDiscoveryService::imageDirectoriesRecursive(const QString& root) {
    QSet<QString> imageDirs;
    QDir base(root);
    if (!base.exists()) {
        return QStringList();
    }

    QDirIterator it(base.absolutePath(), supportedImageFilters(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        imageDirs.insert(it.fileInfo().absolutePath());
    }

    QStringList result = imageDirs.values();
    std::sort(result.begin(), result.end());
    return result;
}

QStringList ImageDiscoveryService::imagesInDirectory(const QString& path) {
    QElapsedTimer timer;
    timer.start();
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
    qInfo() << "[WASM] imagesInDirectory path=" << path
            << "files=" << absolutePaths.size()
            << "ms=" << timer.elapsed();
    return absolutePaths;
}

QStringList ImageDiscoveryService::collectImagesRecursive(const QStringList& roots) {
    QSet<QString> absolutePaths;

    for (const QString& root : roots) {
        QDirIterator imageIt(root, supportedImageFilters(), QDir::Files, QDirIterator::Subdirectories);
        while (imageIt.hasNext()) {
            absolutePaths.insert(imageIt.next());
        }
    }

    QStringList result = absolutePaths.values();
    std::sort(result.begin(), result.end());
    return result;
}
