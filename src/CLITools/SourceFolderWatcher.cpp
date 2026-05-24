#include "SourceFolderWatcher.h"
#include "AppConstants.h"
#include "ImageDiscoveryService.h"

#include <QDir>
#include <QDirIterator>
#include <QTimer>
#include <QFileInfo>
#include <QDebug>

SourceFolderWatcher::SourceFolderWatcher(QObject* parent)
    : QObject(parent),
      m_watcher(nullptr),
      m_debounceTimer(nullptr),
      m_debounceInterval(AppConstants::kFolderWatchDebounceMs) {

    try {
        m_watcher = new QFileSystemWatcher(this);
        m_debounceTimer = new QTimer(this);

        if (!m_watcher || !m_debounceTimer) {
            qWarning() << "SourceFolderWatcher: Failed to allocate watcher or timer";
            return;
        }

        connect(m_watcher, &QFileSystemWatcher::directoryChanged,
                this, &SourceFolderWatcher::onDirectoryChanged);
        connect(m_watcher, &QFileSystemWatcher::fileChanged,
                this, &SourceFolderWatcher::onFileChanged);

        m_debounceTimer->setSingleShot(true);
        connect(m_debounceTimer, &QTimer::timeout,
                this, &SourceFolderWatcher::onDebounceTimeout);
    } catch (...) {
        qWarning() << "SourceFolderWatcher: Exception during construction";
        m_watcher = nullptr;
        m_debounceTimer = nullptr;
    }
}

SourceFolderWatcher::~SourceFolderWatcher() {
    stopWatching();
}

void SourceFolderWatcher::watchFolder(const QString& folderPath) {
    stopWatching();

    QDir dir(folderPath);
    if (!dir.exists()) {
        qWarning() << "SourceFolderWatcher: Folder does not exist:" << folderPath;
        return;
    }

    m_watchedPath = dir.absolutePath();

    if (!QDir(m_watchedPath).exists()) {
        qWarning() << "SourceFolderWatcher: Folder path is invalid:" << m_watchedPath;
        m_watchedPath.clear();
        return;
    }

    if (!m_watcher) {
        try {
            m_watcher = new QFileSystemWatcher(this);
            if (!m_watcher) {
                qWarning() << "SourceFolderWatcher: Failed to create watcher";
                m_watchedPath.clear();
                return;
            }
            connect(m_watcher, &QFileSystemWatcher::directoryChanged,
                    this, &SourceFolderWatcher::onDirectoryChanged);
            connect(m_watcher, &QFileSystemWatcher::fileChanged,
                    this, &SourceFolderWatcher::onFileChanged);
        } catch (...) {
            qWarning() << "SourceFolderWatcher: Exception creating watcher";
            m_watcher = nullptr;
            m_watchedPath.clear();
            return;
        }
    }

    if (!m_debounceTimer) {
        try {
            m_debounceTimer = new QTimer(this);
            if (!m_debounceTimer) {
                qWarning() << "SourceFolderWatcher: Failed to create timer";
                m_watchedPath.clear();
                return;
            }
            m_debounceTimer->setSingleShot(true);
            connect(m_debounceTimer, &QTimer::timeout,
                    this, &SourceFolderWatcher::onDebounceTimeout);
        } catch (...) {
            qWarning() << "SourceFolderWatcher: Exception creating timer";
            m_debounceTimer = nullptr;
            m_watchedPath.clear();
            return;
        }
    }

    // Defer the addPath call to avoid synchronous issues during startup.
    try {
        QTimer::singleShot(100, this, [this]() {
            if (!m_watchedPath.isEmpty() && m_watcher) {
                try {
                    m_watcher->addPath(m_watchedPath);
                    // Also watch all existing subdirectories so changes anywhere
                    // in the tree are detected.
                    watchSubdirectories();

                    if (m_watcher->directories().contains(m_watchedPath)) {
                        updatePreviousFilesList();
                        emit watchingStarted();
                        qInfo() << "SourceFolderWatcher: Started watching" << m_watchedPath
                                << "(" << m_watcher->directories().size() << "dirs)";
                    } else {
                        qWarning() << "SourceFolderWatcher: Failed to add path to watcher:" << m_watchedPath;
                        m_watchedPath.clear();
                    }
                } catch (...) {
                    qWarning() << "SourceFolderWatcher: Exception in deferred addPath:" << m_watchedPath;
                    m_watchedPath.clear();
                }
            }
        });
    } catch (...) {
        qWarning() << "SourceFolderWatcher: Exception setting up watch:" << m_watchedPath;
        m_watchedPath.clear();
        return;
    }

    qInfo() << "SourceFolderWatcher: Watch scheduled for:" << m_watchedPath;
}

void SourceFolderWatcher::watchFolders(const QStringList& folderPaths) {
    stopWatching();
    if (folderPaths.isEmpty()) return;

    QStringList validPaths;
    for (const QString& folderPath : folderPaths) {
        QDir dir(folderPath);
        if (!dir.exists()) {
            qWarning() << "SourceFolderWatcher: Folder does not exist:" << folderPath;
            continue;
        }
        validPaths.append(dir.absolutePath());
    }
    if (validPaths.isEmpty()) return;

    m_watchedPaths = validPaths;

    // Defer the actual addPath calls to avoid synchronous issues during startup.
    QTimer::singleShot(100, this, [this]() {
        if (m_watchedPaths.isEmpty() || !m_watcher) return;
        for (const QString& path : m_watchedPaths) {
            m_watcher->addPath(path);
        }
        watchSubdirectories();
        updatePreviousFilesList();
        emit watchingStarted();
        qInfo() << "SourceFolderWatcher: Started watching" << m_watchedPaths.size() << "folder(s)";
    });

    qInfo() << "SourceFolderWatcher: Watch scheduled for" << m_watchedPaths.size() << "folder(s)";
}

void SourceFolderWatcher::stopWatching() {
    const bool wasWatching = !m_watchedPath.isEmpty() || !m_watchedPaths.isEmpty();
    if (!wasWatching) return;

    if (m_watcher) {
        // Remove the root and every subdirectory that was added.
        const QStringList watchedDirs = m_watcher->directories();
        for (const QString& dir : watchedDirs) {
            m_watcher->removePath(dir);
        }
    }
    if (m_debounceTimer) {
        m_debounceTimer->stop();
    }
    m_pendingAdds.clear();
    m_pendingRemoves.clear();
    m_pendingModifies.clear();
    m_previousFiles.clear();
    m_watchedPath.clear();
    m_watchedPaths.clear();

    emit watchingStopped();
    qInfo() << "SourceFolderWatcher: Stopped watching";
}

void SourceFolderWatcher::setDebounceInterval(int ms) {
    m_debounceInterval = qMax(100, ms);
}

void SourceFolderWatcher::watchSubdirectories() {
    if (!m_watcher) return;

    // Single-folder mode
    if (!m_watchedPath.isEmpty()) {
        const QStringList dirs = ImageDiscoveryService::imageDirectoriesRecursive(m_watchedPath);
        for (const QString& dir : dirs) {
            m_watcher->addPath(dir);
        }
    }

    // Multi-folder mode
    for (const QString& rootPath : m_watchedPaths) {
        const QStringList dirs = ImageDiscoveryService::imageDirectoriesRecursive(rootPath);
        for (const QString& dir : dirs) {
            m_watcher->addPath(dir);
        }
    }
}

void SourceFolderWatcher::onDirectoryChanged(const QString& path) {
    // Accept the root directory or any watched subdirectory (single or multi-folder mode).
    bool accepted = !m_watchedPath.isEmpty() && path.startsWith(m_watchedPath);
    if (!accepted) {
        for (const QString& root : m_watchedPaths) {
            if (path.startsWith(root)) {
                accepted = true;
                break;
            }
        }
    }
    if (!accepted) return;

    // If a new subdirectory was created, register it with the watcher so future
    // changes inside it are also detected.
    watchSubdirectories();

    const QSet<QString> currentFiles = getCurrentFiles();

    // O(1) set operations replace the old O(N²) QStringList::contains loops.
    for (const QString& file : currentFiles) {
        if (!m_previousFiles.contains(file)) {
            m_pendingAdds.insert(file);
            m_pendingRemoves.remove(file);
        }
    }
    for (const QString& file : m_previousFiles) {
        if (!currentFiles.contains(file)) {
            m_pendingRemoves.insert(file);
            m_pendingAdds.remove(file);
        }
    }

    m_previousFiles = currentFiles;
    m_debounceTimer->start(m_debounceInterval);
}

void SourceFolderWatcher::onFileChanged(const QString& path) {
    m_pendingModifies.insert(path);
    m_debounceTimer->start(m_debounceInterval);
}

void SourceFolderWatcher::onDebounceTimeout() {
    if (!m_pendingAdds.isEmpty()) {
        const QStringList adds = m_pendingAdds.values();
        qInfo() << "SourceFolderWatcher: Files added" << adds;
        emit filesAdded(adds);
        m_pendingAdds.clear();
    }

    if (!m_pendingRemoves.isEmpty()) {
        const QStringList removes = m_pendingRemoves.values();
        qInfo() << "SourceFolderWatcher: Files removed" << removes;
        emit filesRemoved(removes);
        m_pendingRemoves.clear();
    }

    if (!m_pendingModifies.isEmpty()) {
        const QStringList modifies = m_pendingModifies.values();
        qInfo() << "SourceFolderWatcher: Files modified" << modifies;
        emit filesModified(modifies);
        m_pendingModifies.clear();
    }
}

void SourceFolderWatcher::updatePreviousFilesList() {
    m_previousFiles = getCurrentFiles();
}

QSet<QString> SourceFolderWatcher::getCurrentFiles() const {
    QStringList roots;
    if (!m_watchedPath.isEmpty()) roots.append(m_watchedPath);
    roots.append(m_watchedPaths);
    if (roots.isEmpty()) return QSet<QString>();
    // Use the optimized recursive collector which skips .git, node_modules, etc.
    const QStringList files = ImageDiscoveryService::collectImagesRecursive(roots);
    return QSet<QString>(files.begin(), files.end());
}
