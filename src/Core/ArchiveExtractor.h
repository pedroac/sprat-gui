#pragma once

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QDir>
#include <atomic>

/**
 * @class ArchiveExtractor
 * @brief Handles archive (zip, tar, etc.) extraction using libarchive.
 */
class ArchiveExtractor {
public:
    /**
     * @brief Extracts an archive to a destination directory.
     * @param archivePath Path to the archive file.
     * @param destDir Path to the destination directory.
     * @param error Output error message if extraction fails.
     * @param canceled Optional atomic boolean to signal cancellation.
     * @return bool True if extraction was successful.
     */
    static bool extractToDirectory(const QString& archivePath, const QString& destDir, QString& error, std::atomic<bool>* canceled = nullptr);

    /**
     * @brief Reads a specific file from an archive into a QByteArray.
     * @param archivePath Path to the archive file.
     * @param fileName Name of the file inside the archive to read.
     * @param data Output byte array with file content.
     * @param error Output error message if reading fails.
     * @return bool True if reading was successful.
     */
    static bool readFileFromArchive(const QString& archivePath, const QString& fileName, QByteArray& data, QString& error);

private:
    static int copyData(struct archive* ar, struct archive* aw);
};
