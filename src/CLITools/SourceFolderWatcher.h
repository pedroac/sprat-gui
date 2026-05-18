#pragma once

#include <QObject>
#include <QString>
#include <QSet>
#include <QStringList>
#include <QFileSystemWatcher>

/**
 * @class SourceFolderWatcher
 * @brief Monitors a source folder (and its subdirectories) for file system changes.
 *
 * Uses QFileSystemWatcher to detect when image files are added, removed, or modified
 * in the source folder tree. Debounces rapid changes to batch processing.
 *
 * Internal state uses QSet<QString> for O(1) change detection instead of the
 * O(N²) QStringList::contains approach.
 */
class SourceFolderWatcher : public QObject {
    Q_OBJECT

public:
    explicit SourceFolderWatcher(QObject* parent = nullptr);
    ~SourceFolderWatcher() override;

    /**
     * Start watching a folder (and all its subdirectories) for changes.
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
    void filesAdded(const QStringList& paths);
    void filesRemoved(const QStringList& paths);
    void filesModified(const QStringList& paths);
    void watchingStarted();
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

    // QSet gives O(1) contains/insert/remove; replacing QStringList avoids O(N²) in
    // onDirectoryChanged when many files are present.
    QSet<QString> m_pendingAdds;
    QSet<QString> m_pendingRemoves;
    QSet<QString> m_pendingModifies;
    QSet<QString> m_previousFiles;

    void updatePreviousFilesList();
    // Returns the set of all image files under m_watchedPath (recursive).
    QSet<QString> getCurrentFiles() const;
    // Adds all subdirectories under m_watchedPath to the watcher.
    void watchSubdirectories();
};
