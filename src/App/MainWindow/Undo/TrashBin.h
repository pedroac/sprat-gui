#pragma once
#include <QString>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDateTime>

// ---------------------------------------------------------------------------
// TrashBin — reversible file deletion via hidden trash folder
// ---------------------------------------------------------------------------
class TrashBin {
public:
    static QString trashRoot(const QString& sf) {
        return QDir(sf).filePath(".sprat-trash");
    }

    // Move filePath into trash, return the trash path (empty on failure)
    static QString send(const QString& filePath, const QString& sourceFolder) {
        QDir srcDir(sourceFolder);
        QString relPath = srcDir.relativeFilePath(filePath);
        if (relPath == ".." || relPath.startsWith("../")) {
            relPath = QFileInfo(filePath).fileName();
        }
        QString trashPath = QDir(trashRoot(sourceFolder)).filePath(relPath);
        QFileInfo trashInfo(trashPath);
        if (!QDir().mkpath(trashInfo.absolutePath())) {
            return {};
        }
        // If a stale trash copy exists, remove it
        if (QFile::exists(trashPath)) {
            QFile::remove(trashPath);
        }
        if (QFile::rename(filePath, trashPath)) {
            return trashPath;
        }

        // Some platforms/filesystems can refuse rename for files that are still
        // being touched by another process. Fall back to copy + remove so the
        // session state cannot drift from what remains on disk.
        if (QFile::copy(filePath, trashPath) && QFile::remove(filePath)) {
            return trashPath;
        }

        QFile::remove(trashPath);
        return {};
    }

    // Move trashPath back to originalPath
    static bool restore(const QString& trashPath, const QString& originalPath) {
        QFileInfo origInfo(originalPath);
        QDir().mkpath(origInfo.absolutePath());
        return QFile::rename(trashPath, originalPath);
    }

    // Delete the entire trash folder for a source folder
    static void purge(const QString& sourceFolder) {
        if (sourceFolder.isEmpty()) return;
        QDir(trashRoot(sourceFolder)).removeRecursively();
    }

    // Move an entire folder to a system temp location for undo support.
    // Returns the new trash path on success, or an empty string on failure.
    static QString sendFolder(const QString& folderPath) {
        if (folderPath.isEmpty() || !QDir(folderPath).exists())
            return {};
        const QString tmpBase = QDir(
            QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                .filePath(QStringLiteral("sprat-source-trash"));
        if (!QDir().mkpath(tmpBase))
            return {};
        const QString folderName = QFileInfo(folderPath).fileName();
        const QString trashPath = QDir(tmpBase).filePath(
            folderName + QLatin1Char('_') +
            QString::number(QDateTime::currentMSecsSinceEpoch()));
        if (QFile::rename(folderPath, trashPath))
            return trashPath;
        return {};
    }

    // Move a folder from its trash path back to its original location.
    static bool restoreFolder(const QString& trashPath, const QString& originalPath) {
        if (trashPath.isEmpty() || !QDir(trashPath).exists())
            return false;
        QFileInfo origInfo(originalPath);
        QDir().mkpath(origInfo.absolutePath());
        return QFile::rename(trashPath, originalPath);
    }
};
