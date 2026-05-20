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
    QElapsedTimer timer;
    timer.start();
    QSet<QString> imageDirs;
    QStringList stack = { root };

    while (!stack.isEmpty()) {
        QString currentPath = stack.takeLast();
        QDir dir(currentPath);
        if (!dir.exists()) continue;

        QString dirName = dir.dirName();
        if (dirName == QLatin1String(".git") || 
            dirName == QLatin1String(".svn") || 
            dirName == QLatin1String("node_modules") ||
            dirName == QLatin1String(".sprat-trash")) {
            continue;
        }

        if (hasImageFiles(currentPath)) {
            imageDirs.insert(dir.absolutePath());
        }

        QDirIterator dirIt(currentPath, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        while (dirIt.hasNext()) {
            stack.append(dirIt.next());
        }
    }

    QStringList result = imageDirs.values();
    std::sort(result.begin(), result.end());
    
    qInfo() << "[ImageDiscovery] imageDirectoriesRecursive total ms=" << timer.elapsed()
            << "dirs=" << result.size();
            
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
    QElapsedTimer timer;
    timer.start();

    QStringList result;
    QSet<QString> seen; // To avoid duplicates if roots overlap
    QStringList stack = roots;
    
    const QStringList& filters = supportedImageFilters();

    while (!stack.isEmpty()) {
        QString currentPath = stack.takeLast();
        QDir dir(currentPath);
        if (!dir.exists()) continue;

        QString dirName = dir.dirName();
        // Skip common large/irrelevant directories
        if (dirName == QLatin1String(".git") || 
            dirName == QLatin1String(".svn") || 
            dirName == QLatin1String("node_modules") ||
            dirName == QLatin1String(".sprat-trash")) {
            continue;
        }

        // 1. Collect image files in the current directory
        QDirIterator fileIt(currentPath, filters, QDir::Files | QDir::Readable | QDir::NoDotAndDotDot);
        while (fileIt.hasNext()) {
            QString path = fileIt.next();
            if (roots.size() > 1) {
                if (!seen.contains(path)) {
                    seen.insert(path);
                    result.append(path);
                }
            } else {
                result.append(path);
            }
        }

        // 2. Add subdirectories to the stack for further exploration
        QDirIterator dirIt(currentPath, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        while (dirIt.hasNext()) {
            stack.append(dirIt.next());
        }
    }

    std::sort(result.begin(), result.end());

    qInfo() << "[ImageDiscovery] collectImagesRecursive total ms=" << timer.elapsed()
            << "files=" << result.size()
            << "roots=" << roots.size();

    return result;
}
