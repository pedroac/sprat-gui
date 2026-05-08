#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QFileSystemWatcher>

/**
 * @class SourceFolderWatcher
 * @brief Monitors a source folder for file system changes.
 *
 * Uses QFileSystemWatcher to detect when files are added, removed, or modified
 * in the source folder. Debounces rapid changes to batch processing.
 */
class SourceFolderWatcher : public QObject {
    Q_OBJECT

public:
    explicit SourceFolderWatcher(QObject* parent = nullptr);
    ~SourceFolderWatcher() override;

    /**
     * Start watching a folder for changes.
     * @param folderPath Absolute path to folder to watch
     */
    void watchFolder(const QString& folderPath);

    /**
     * Stop watching the current folder.
     */
    void stopWatching();

    /**
     * Check if currently watching a folder.
     */
    bool isWatching() const { return !m_watchedPath.isEmpty(); }

    /**
     * Get the path of the currently watched folder.
     */
    QString watchedPath() const { return m_watchedPath; }

    /**
     * Set debounce interval in milliseconds (default 500ms).
     * Rapid changes within this interval are batched together.
     */
    void setDebounceInterval(int ms);

signals:
    /**
     * Emitted when files are detected as added to the watched folder.
     */
    void filesAdded(const QStringList& paths);

    /**
     * Emitted when files are detected as removed from the watched folder.
     */
    void filesRemoved(const QStringList& paths);

    /**
     * Emitted when files are detected as modified in the watched folder.
     */
    void filesModified(const QStringList& paths);

    /**
     * Emitted when watching has started.
     */
    void watchingStarted();

    /**
     * Emitted when watching has stopped.
     */
    void watchingStopped();

private slots:
    void onDirectoryChanged(const QString& path);
    void onFileChanged(const QString& path);
    void onDebounceTimeout();

private:
    QFileSystemWatcher* m_watcher;
    QString m_watchedPath;
    class QTimer* m_debounceTimer;
    int m_debounceInterval;
    QStringList m_pendingAdds;
    QStringList m_pendingRemoves;
    QStringList m_pendingModifies;
    QStringList m_previousFiles;

    void updatePreviousFilesList();
    QStringList getCurrentFiles() const;
};
