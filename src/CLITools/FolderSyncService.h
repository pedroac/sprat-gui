#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include "models.h"

/**
 * @class FolderSyncService
 * @brief Service for synchronizing project sprites with a source folder.
 *
 * Detects changes in a folder (new/removed/modified images) and intelligently
 * merges them with the current layout while preserving metadata.
 */
class FolderSyncService {
public:
    /**
     * @struct SyncResult
     * @brief Result of a folder sync operation.
     */
    struct SyncResult {
        QStringList newImagePaths;      ///< Absolute paths to newly detected images
        QStringList deletedImagePaths;  ///< Absolute paths to images removed from folder
        QStringList modifiedImagePaths; ///< Absolute paths to modified images
        QString error;                  ///< Error message if sync failed

        bool hasChanges() const {
            return !newImagePaths.isEmpty() || !deletedImagePaths.isEmpty();
        }
    };

    /**
     * Scan a folder and detect changes compared to current layout.
     *
     * @param folderPath Absolute path to the source folder
     * @param currentSprites Current sprites in the layout
     * @return SyncResult containing detected changes
     */
    static SyncResult detectChanges(
        const QString& folderPath,
        const QVector<SpritePtr>& currentSprites);

    /**
     * Scan multiple smart folders and detect changes compared to current layout.
     * Files listed in each SmartFolder's excludedFiles are treated as already-excluded
     * (not reported as new even if they exist on disk).
     *
     * @param smartFolders Smart folders to scan (with per-folder exclusion lists)
     * @param currentSprites Current sprites in the layout
     * @return SyncResult containing detected changes
     */
    static SyncResult detectChangesFromSmartFolders(
        const QVector<SmartFolder>& smartFolders,
        const QVector<SpritePtr>& currentSprites);

    /**
     * Scan multiple named sources and detect changes compared to current layout.
     *
     * For Folder-type sources the scan folder is originalPath.
     * For non-Folder types the scan folder is cachedFolderPath (if set).
     * Paths added or removed relative to the current sprites are returned in the result.
     * Sprites that are in currentSprites but not in any source are returned as orphaned.
     *
     * @param sources Named sources to scan
     * @param currentSprites Current sprites in the layout
     * @return SyncResult containing detected changes; orphaned paths are in deletedImagePaths
     */
    static SyncResult detectChangesFromSources(
        const QVector<ProjectSource>& sources,
        const QVector<SpritePtr>& currentSprites);

    /**
     * Merge sync results into the layout, adding new sprites.
     *
     * New sprites are appended with default metadata (no markers, default pivot).
     *
     * @param layout The layout to merge results into (modified in-place)
     * @param changes The sync results to merge
     * @return true if merge was successful, false if error occurred
     */
    static bool mergeSyncResults(
        LayoutModel& layout,
        const SyncResult& changes,
        const QString& sourceFolder = QString());

    /**
     * Get displayable name for a sync result count.
     * Example: "5 new sprites added"
     */
    static QString describeSyncResult(const SyncResult& result);

    /**
     * Get all image files in a folder.
     * @param folderPath Absolute path to folder
     * @return Sorted list of absolute paths to image files
     */
    static QStringList getImageFilesInFolder(const QString& folderPath);

    /**
     * Organize new image files by naming pattern.
     *
     * Analyzes file names and creates subfolders when files have mixed patterns.
     * For example, if files include both "walk1.png" and "run1.png", creates
     * "walk" and "run" subfolders and moves files accordingly.
     *
     * @param newImagePaths List of new image file paths to organize
     * @return List of paths after organization (may differ from input if files were moved)
     */
    static QStringList organizeNewImagesByPattern(const QStringList& newImagePaths);

private:

    /**
     * Extract absolute paths from sprites.
     */
    static QStringList getSpritePaths(const QVector<SpritePtr>& sprites);

    /**
     * Check if a file path corresponds to an image file type.
     */
    static bool isImageFile(const QString& filePath);
};
