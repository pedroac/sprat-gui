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
     * Start watching a single folder (and all its subdirectories) for changes.
     * Replaces any currently watched folder(s).
     * @param folderPath Absolute path to folder to watch
     */
    void watchFolder(const QString& folderPath);

    /**
     * Start watching multiple folders (and their subdirectories) for changes.
     * Replaces any currently watched folder(s).
     * @param folderPaths List of absolute paths to watch
     */
    void watchFolders(const QStringList& folderPaths);

    /**
     * Stop watching all folders.
     */
    void stopWatching();

    /**
     * Check if currently watching at least one folder.
     */
    bool isWatching() const { return !m_watchedPath.isEmpty() || !m_watchedPaths.isEmpty(); }

    /**
     * Get the path of the primary watched folder (first in multi-folder mode, or the single path).
     */
    QString watchedPath() const { return m_watchedPaths.isEmpty() ? m_watchedPath : m_watchedPaths.first(); }

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
    QString m_watchedPath;     // Single-folder mode (legacy, used by watchFolder())
    QStringList m_watchedPaths; // Multi-folder mode (used by watchFolders())
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
    // Adds all subdirectories under m_watchedPath (and m_watchedPaths) to the watcher.
    void watchSubdirectories();
};
