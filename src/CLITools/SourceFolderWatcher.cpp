#include "SourceFolderWatcher.h"

#include <QDir>
#include <QTimer>
#include <QFileInfo>
#include <QDebug>

SourceFolderWatcher::SourceFolderWatcher(QObject* parent)
    : QObject(parent),
      m_watcher(nullptr),
      m_debounceTimer(nullptr),
      m_debounceInterval(500) {

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

    // Verify the path still exists and is valid before adding to watcher
    if (!QDir(m_watchedPath).exists()) {
        qWarning() << "SourceFolderWatcher: Folder path is invalid:" << m_watchedPath;
        m_watchedPath.clear();
        return;
    }

    // Ensure watcher is properly initialized
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

    // Ensure timer is properly initialized
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

    // Add path to watcher with try-catch
    try {
        // Use QTimer to defer the addPath call, avoiding synchronous issues
        QTimer::singleShot(100, this, [this]() {
            if (!m_watchedPath.isEmpty() && m_watcher) {
                try {
                    m_watcher->addPath(m_watchedPath);
                    if (m_watcher->directories().contains(m_watchedPath)) {
                        updatePreviousFilesList();
                        emit watchingStarted();
                        qInfo() << "SourceFolderWatcher: Started watching" << m_watchedPath;
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

void SourceFolderWatcher::stopWatching() {
    if (!m_watchedPath.isEmpty()) {
        if (m_watcher) {
            m_watcher->removePath(m_watchedPath);
        }
        if (m_debounceTimer) {
            m_debounceTimer->stop();
        }
        m_pendingAdds.clear();
        m_pendingRemoves.clear();
        m_pendingModifies.clear();
        m_previousFiles.clear();
        m_watchedPath.clear();

        emit watchingStopped();
        qInfo() << "SourceFolderWatcher: Stopped watching";
    }
}

void SourceFolderWatcher::setDebounceInterval(int ms) {
    m_debounceInterval = qMax(100, ms);
}

void SourceFolderWatcher::onDirectoryChanged(const QString& path) {
    if (path != m_watchedPath) {
        return;
    }

    // Folder changed - detect which files were added/removed/modified
    QStringList currentFiles = getCurrentFiles();

    // Find added files
    for (const QString& file : currentFiles) {
        if (!m_previousFiles.contains(file)) {
            if (!m_pendingAdds.contains(file)) {
                m_pendingAdds.append(file);
            }
            // Remove from removals if it was marked for removal
            m_pendingRemoves.removeAll(file);
        }
    }

    // Find removed files
    for (const QString& file : m_previousFiles) {
        if (!currentFiles.contains(file)) {
            if (!m_pendingRemoves.contains(file)) {
                m_pendingRemoves.append(file);
            }
            // Remove from additions if it was marked for addition
            m_pendingAdds.removeAll(file);
        }
    }

    m_previousFiles = currentFiles;

    // Debounce to batch rapid changes
    m_debounceTimer->start(m_debounceInterval);
}

void SourceFolderWatcher::onFileChanged(const QString& path) {
    // File was modified
    if (!m_pendingModifies.contains(path)) {
        m_pendingModifies.append(path);
    }

    // Debounce
    m_debounceTimer->start(m_debounceInterval);
}

void SourceFolderWatcher::onDebounceTimeout() {
    // Emit batched changes
    if (!m_pendingAdds.isEmpty()) {
        m_pendingAdds.removeDuplicates();
        qInfo() << "SourceFolderWatcher: Files added" << m_pendingAdds;
        emit filesAdded(m_pendingAdds);
        m_pendingAdds.clear();
    }

    if (!m_pendingRemoves.isEmpty()) {
        m_pendingRemoves.removeDuplicates();
        qInfo() << "SourceFolderWatcher: Files removed" << m_pendingRemoves;
        emit filesRemoved(m_pendingRemoves);
        m_pendingRemoves.clear();
    }

    if (!m_pendingModifies.isEmpty()) {
        m_pendingModifies.removeDuplicates();
        qInfo() << "SourceFolderWatcher: Files modified" << m_pendingModifies;
        emit filesModified(m_pendingModifies);
        m_pendingModifies.clear();
    }
}

void SourceFolderWatcher::updatePreviousFilesList() {
    m_previousFiles = getCurrentFiles();
}

QStringList SourceFolderWatcher::getCurrentFiles() const {
    QStringList files;

    QDir dir(m_watchedPath);
    // Filter for image files
    QStringList nameFilters;
    nameFilters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.gif" << "*.webp"
                << "*.PNG" << "*.JPG" << "*.JPEG" << "*.BMP" << "*.GIF" << "*.WEBP";

    dir.setNameFilters(nameFilters);
    dir.setFilter(QDir::Files | QDir::Readable);

    const QFileInfoList fileList = dir.entryInfoList();
    for (const QFileInfo& fileInfo : fileList) {
        files.append(fileInfo.absoluteFilePath());
    }

    files.sort();
    return files;
}
