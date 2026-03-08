#include "ArchiveExtractor.h"
#include <archive.h>
#include <archive_entry.h>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#define PATH_TO_UTF8(p) (p).toUtf8().constData()
#else
#define PATH_TO_UTF8(p) (p).toLocal8Bit().constData()
#endif

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
    archive_write_disk_set_standard_lookup(ext);

    if ((r = archive_read_open_filename(a, PATH_TO_UTF8(archivePath), 10240))) {
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

        const QString currentFile = QString::fromUtf8(archive_entry_pathname(entry));
        const QString fullPath = destination.absoluteFilePath(currentFile);
        
        archive_entry_set_pathname(entry, PATH_TO_UTF8(fullPath));

        r = archive_write_header(ext, entry);
        if (r < ARCHIVE_OK) {
            error = QString("Error writing archive header: %1").arg(archive_error_string(ext));
            break;
        } else if (archive_entry_size(entry) > 0) {
            r = copyData(a, ext);
            if (r < ARCHIVE_OK) {
                error = QString("Error copying data from archive: %1").arg(archive_error_string(ext));
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

    if (archive_read_open_filename(a, PATH_TO_UTF8(archivePath), 10240)) {
        error = QString("Could not open archive: %1").arg(archive_error_string(a));
        archive_read_free(a);
        return false;
    }

    bool found = false;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        QString entryPath = QString::fromUtf8(archive_entry_pathname(entry));
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
