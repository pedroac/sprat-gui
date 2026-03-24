#include "ArchiveExtractor.h"
#include <archive.h>
#include <archive_entry.h>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#define PATH_TO_UTF8(p) (p).toUtf8().constData()

bool ArchiveExtractor::extractToDirectory(const QString& archivePath, const QString& destDir, QString& error, std::atomic<bool>* canceled) {
    struct archive* a;
    struct archive* ext;
    struct archive_entry* entry;
    int flags;
    int r;

    // Attributes we want to restore
    flags = ARCHIVE_EXTRACT_TIME;
    flags |= ARCHIVE_EXTRACT_PERM;
    flags |= ARCHIVE_EXTRACT_ACL;
    flags |= ARCHIVE_EXTRACT_FFLAGS;

    a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, flags);

#ifdef Q_OS_WIN
    if ((r = archive_read_open_filename_w(a, reinterpret_cast<const wchar_t*>(archivePath.utf16()), 10240))) {
#else
    if ((r = archive_read_open_filename(a, PATH_TO_UTF8(archivePath), 10240))) {
#endif
        error = QString("Could not open archive: %1").arg(archive_error_string(a));
        archive_read_free(a);
        archive_write_free(ext);
        return false;
    }

    QDir destination(destDir);
    if (!destination.exists()) {
        destination.mkpath(".");
    }

    for (;;) {
        if (canceled && *canceled) {
            error = "Canceled";
            r = ARCHIVE_FATAL;
            break;
        }

        r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF)
            break;
        if (r < ARCHIVE_OK) {
            error = QString("Error reading archive header: %1").arg(archive_error_string(a));
            break;
        }
        if (r < ARCHIVE_WARN) {
            r = ARCHIVE_FATAL;
            break;
        }

#ifdef Q_OS_WIN
        const QString currentFile = QString::fromWCharArray(archive_entry_pathname_w(entry));
#else
        const QString currentFile = QString::fromUtf8(archive_entry_pathname(entry));
#endif
        const QString fullPath = destination.absoluteFilePath(currentFile);
        
#ifdef Q_OS_WIN
        archive_entry_copy_pathname_w(entry, reinterpret_cast<const wchar_t*>(fullPath.utf16()));
#else
        archive_entry_set_pathname(entry, PATH_TO_UTF8(fullPath));
#endif

        r = archive_write_header(ext, entry);
        if (r < ARCHIVE_OK) {
            error = QString("Error writing archive header: %1").arg(archive_error_string(ext));
            break;
        } else if (archive_entry_size(entry) > 0) {
            r = copyData(a, ext);
            if (r < ARCHIVE_OK) {
                // If r < 0, it could be from archive_read_data_block or archive_write_data_block
                // We try to get the most relevant error message
                QString readErr = archive_error_string(a);
                QString writeErr = archive_error_string(ext);
                if (!readErr.isEmpty()) {
                    error = QString("Error reading archive data: %1").arg(readErr);
                } else if (!writeErr.isEmpty()) {
                    error = QString("Error writing archive data: %1").arg(writeErr);
                } else {
                    error = "Unknown error during data copy";
                }
                break;
            }
            if (r < ARCHIVE_WARN) {
                r = ARCHIVE_FATAL;
                break;
            }
        }
        r = archive_write_finish_entry(ext);
        if (r < ARCHIVE_OK) {
            error = QString("Error finishing entry: %1").arg(archive_error_string(ext));
            break;
        }
        if (r < ARCHIVE_WARN) {
            r = ARCHIVE_FATAL;
            break;
        }
    }

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);

    return r >= ARCHIVE_WARN;
}

bool ArchiveExtractor::readFileFromArchive(const QString& archivePath, const QString& fileName, QByteArray& data, QString& error) {
    struct archive* a;
    struct archive_entry* entry;
    int r;

    a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

#ifdef Q_OS_WIN
    if (archive_read_open_filename_w(a, reinterpret_cast<const wchar_t*>(archivePath.utf16()), 10240)) {
#else
    if (archive_read_open_filename(a, PATH_TO_UTF8(archivePath), 10240)) {
#endif
        error = QString("Could not open archive: %1").arg(archive_error_string(a));
        archive_read_free(a);
        return false;
    }

    bool found = false;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
#ifdef Q_OS_WIN
        QString entryPath = QString::fromWCharArray(archive_entry_pathname_w(entry));
#else
        QString entryPath = QString::fromUtf8(archive_entry_pathname(entry));
#endif
        // Normalize paths for comparison (handles subdirectories inside zip)
        if (entryPath == fileName || entryPath.endsWith("/" + fileName)) {
            found = true;
            la_int64_t size = archive_entry_size(entry);
            if (size > 0) {
                data.resize(static_cast<int>(size));
                archive_read_data(a, data.data(), static_cast<size_t>(size));
            }
            break;
        }
    }

    if (!found) {
        error = QString("File not found in archive: %1").arg(fileName);
    }

    archive_read_close(a);
    archive_read_free(a);
    return found;
}

#include <QDirIterator>

bool ArchiveExtractor::createZip(const QString& sourceDir, const QString& destZipPath, QString& error) {
    struct archive* a = archive_write_new();
    archive_write_set_format_zip(a);
    
#ifdef Q_OS_WIN
    int r = archive_write_open_filename_w(a, reinterpret_cast<const wchar_t*>(destZipPath.utf16()));
#else
    int r = archive_write_open_filename(a, PATH_TO_UTF8(destZipPath));
#endif

    if (r != ARCHIVE_OK) {
        error = QString("Could not open output archive: %1").arg(archive_error_string(a));
        archive_write_free(a);
        return false;
    }

    QDir source(sourceDir);
    QDirIterator it(sourceDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString filePath = it.next();
        QString relPath = source.relativeFilePath(filePath);
        
        struct archive_entry* entry = archive_entry_new();
#ifdef Q_OS_WIN
        archive_entry_copy_pathname_w(entry, reinterpret_cast<const wchar_t*>(relPath.utf16()));
#else
        archive_entry_set_pathname(entry, PATH_TO_UTF8(relPath));
#endif
        
        QFileInfo info(filePath);
        archive_entry_set_size(entry, info.size());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        
        if (archive_write_header(a, entry) != ARCHIVE_OK) {
            error = QString("Header write error: %1").arg(archive_error_string(a));
            archive_entry_free(entry);
            archive_write_close(a);
            archive_write_free(a);
            return false;
        }

        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray content = file.readAll();
            archive_write_data(a, content.data(), static_cast<size_t>(content.size()));
            file.close();
        }
        
        archive_entry_free(entry);
    }

    archive_write_close(a);
    archive_write_free(a);
    return true;
}

int ArchiveExtractor::copyData(struct archive* ar, struct archive* aw) {
    int r;
    const void* buff;
    size_t size;
    la_int64_t offset;

    for (;;) {
        r = archive_read_data_block(ar, &buff, &size, &offset);
        if (r == ARCHIVE_EOF)
            return (ARCHIVE_OK);
        if (r < ARCHIVE_OK)
            return (r);
        r = archive_write_data_block(aw, buff, size, offset);
        if (r < ARCHIVE_OK) {
            qWarning() << "archive_write_data_block error:" << archive_error_string(aw);
            return (r);
        }
    }
}
