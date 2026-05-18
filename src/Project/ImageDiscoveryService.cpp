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
    // Use QDirIterator so we stop at the first match instead of collecting all files.
    QDirIterator it(path, supportedImageFilters(), QDir::Files);
    return it.hasNext();
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
    qInfo() << "[ImageDiscovery] imageDirectoriesOneLevel root=" << root
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

    // Pre-compute trash path fragments to avoid per-iteration string allocation.
    const QString trashInfix  = QStringLiteral("/.sprat-trash/");
    const QString trashSuffix = QStringLiteral("/.sprat-trash");

    QDirIterator it(base.absolutePath(), supportedImageFilters(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString absPath = it.fileInfo().absolutePath();
        if (absPath.contains(trashInfix) || absPath.endsWith(trashSuffix)) {
            continue;
        }
        imageDirs.insert(absPath);
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
    qInfo() << "[ImageDiscovery] imagesInDirectory path=" << path
            << "files=" << absolutePaths.size()
            << "ms=" << timer.elapsed();
    return absolutePaths;
}

QStringList ImageDiscoveryService::collectImagesRecursive(const QStringList& roots) {
    QSet<QString> absolutePaths;

    // Pre-compute trash path fragments to avoid per-iteration string allocation.
    const QString trashInfix  = QStringLiteral("/.sprat-trash/");
    const QString trashInfixW = QStringLiteral("/.sprat-trash\\");

    for (const QString& root : roots) {
        QDirIterator imageIt(root, supportedImageFilters(), QDir::Files, QDirIterator::Subdirectories);
        while (imageIt.hasNext()) {
            const QString path = imageIt.next();
            if (path.contains(trashInfix) || path.contains(trashInfixW)) {
                continue;
            }
            absolutePaths.insert(path);
        }
    }

    QStringList result = absolutePaths.values();
    std::sort(result.begin(), result.end());
    return result;
}
