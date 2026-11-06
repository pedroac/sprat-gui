#include "ArchiveExtractor.h"
#include <archive.h>
#include <archive_entry.h>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QSaveFile>
#include <QDebug>

namespace {

constexpr int kReadBlockSize = 10240;

/// Opens an archive for reading with every format and filter enabled.
/// @return The archive, or nullptr with error set
struct archive* openForReading(const QString& archivePath, QString& error) {
    struct archive* a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
#ifdef Q_OS_WIN
    const int r = archive_read_open_filename_w(a, reinterpret_cast<const wchar_t*>(archivePath.utf16()), kReadBlockSize);
#else
    const int r = archive_read_open_filename(a, archivePath.toUtf8().constData(), kReadBlockSize);
#endif
    if (r != ARCHIVE_OK) {
        error = QString("Could not open archive: %1").arg(archive_error_string(a));
        archive_read_free(a);
        return nullptr;
    }
    return a;
}

QString entryPathName(struct archive_entry* entry) {
#ifdef Q_OS_WIN
    return QString::fromWCharArray(archive_entry_pathname_w(entry));
#else
    return QString::fromUtf8(archive_entry_pathname(entry));
#endif
}

/// Links can point outside the destination (a hard link with data overwrites its target),
/// and devices or FIFOs have no place in a sprite archive, so only plain files and folders
/// are extracted.
bool isPlainFileOrFolder(struct archive_entry* entry) {
    if (archive_entry_hardlink(entry) || archive_entry_symlink(entry))
        return false;
    const auto type = archive_entry_filetype(entry);
    return type == AE_IFREG || type == AE_IFDIR;
}

la_ssize_t writeToSaveFile(struct archive*, void* clientData, const void* buffer, size_t length) {
    auto* file = static_cast<QSaveFile*>(clientData);
    const qint64 written = file->write(static_cast<const char*>(buffer), static_cast<qint64>(length));
    return written == static_cast<qint64>(length) ? static_cast<la_ssize_t>(written) : -1;
}

} // namespace

bool ArchiveExtractor::extractToDirectory(const QString& archivePath, const QString& destDir, QString& error, std::atomic<bool>* canceled) {
    // Permissions, ACLs and file flags from the archive are not restored: sprites only need
    // their content, and an archive could otherwise mark files setuid or unreadable.
    const int flags = ARCHIVE_EXTRACT_TIME
                    | ARCHIVE_EXTRACT_SECURE_NODOTDOT
                    | ARCHIVE_EXTRACT_SECURE_SYMLINKS;

    QDir destination(destDir);
    if (!destination.exists())
        destination.mkpath(".");
    // Entry paths are resolved against the canonical folder so the containment check
    // compares like with like (e.g. macOS /var is a symlink to /private/var).
    const QString canonicalDest = QDir::cleanPath(destination.canonicalPath());
    if (canonicalDest.isEmpty()) {
        error = QString("Could not create extraction folder: %1").arg(destDir);
        return false;
    }

    struct archive* a = openForReading(archivePath, error);
    if (!a)
        return false;
    struct archive* ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, flags);

    constexpr qint64 kMaxTotalExtractSize = 4LL * 1024 * 1024 * 1024; // 4 GB total
    constexpr la_int64_t kMaxExtractEntrySize = 256 * 1024 * 1024;    // 256 MB per entry
    constexpr int    kMaxExtractFileCount  = 100'000;
    qint64 totalExtractedBytes = 0;
    int    fileCount           = 0;
    int    r                   = ARCHIVE_OK;
    struct archive_entry* entry = nullptr;

    for (;;) {
        if (canceled && *canceled) {
            error = "Canceled";
            r = ARCHIVE_FATAL;
            break;
        }

        r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF)
            break;
        if (r < ARCHIVE_WARN) {
            error = QString("Error reading archive header: %1").arg(archive_error_string(a));
            break;
        }

        const QString currentFile = entryPathName(entry);
        if (!isPlainFileOrFolder(entry)) {
            qWarning() << "ArchiveExtractor: skipping link or special file" << currentFile;
            archive_read_data_skip(a);
            continue;
        }

        const QString fullPath = QDir::cleanPath(QDir(canonicalDest).absoluteFilePath(currentFile));
        if (!fullPath.startsWith(canonicalDest + '/') && fullPath != canonicalDest) {
            error = QString("Path traversal attempt detected in archive entry: %1").arg(currentFile);
            r = ARCHIVE_FATAL;
            break;
        }

#ifdef Q_OS_WIN
        archive_entry_copy_pathname_w(entry, reinterpret_cast<const wchar_t*>(fullPath.utf16()));
#else
        archive_entry_set_pathname(entry, fullPath.toUtf8().constData());
#endif

        if (++fileCount > kMaxExtractFileCount) {
            error = QString("Archive contains too many entries (> %1)").arg(kMaxExtractFileCount);
            r = ARCHIVE_FATAL;
            break;
        }
        if (archive_entry_size(entry) > kMaxExtractEntrySize) {
            error = QString("Archive entry too large (> 256 MB): %1").arg(currentFile);
            r = ARCHIVE_FATAL;
            break;
        }

        r = archive_write_header(ext, entry);
        if (r < ARCHIVE_WARN) {
            error = QString("Error writing archive header: %1").arg(archive_error_string(ext));
            break;
        }
        if (archive_entry_size(entry) > 0) {
            r = copyData(a, ext, totalExtractedBytes, kMaxTotalExtractSize);
            if (r < ARCHIVE_WARN) {
                if (totalExtractedBytes > kMaxTotalExtractSize) {
                    error = QString("Archive total uncompressed size exceeds limit (> 4 GB)");
                } else {
                    const QString readErr  = archive_error_string(a);
                    const QString writeErr = archive_error_string(ext);
                    if (!readErr.isEmpty()) {
                        error = QString("Error reading archive data: %1").arg(readErr);
                    } else if (!writeErr.isEmpty()) {
                        error = QString("Error writing archive data: %1").arg(writeErr);
                    } else {
                        error = "Unknown error during data copy";
                    }
                }
                break;
            }
        }
        r = archive_write_finish_entry(ext);
        if (r < ARCHIVE_WARN) {
            error = QString("Error finishing entry: %1").arg(archive_error_string(ext));
            break;
        }
    }

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);

    // Every early exit is an error; only reaching the end of the archive is a success.
    return r == ARCHIVE_EOF;
}

bool ArchiveExtractor::readFileFromArchive(const QString& archivePath, const QString& fileName, QByteArray& data, QString& error, bool exactMatch) {
    struct archive* a = openForReading(archivePath, error);
    if (!a)
        return false;

    bool found = false;
    bool failed = false;
    constexpr la_int64_t kMaxInMemorySize = 256 * 1024 * 1024; // 256 MB
    struct archive_entry* entry = nullptr;
    while (!found && !failed && archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const QString entryPath = entryPathName(entry);
        const bool matches = exactMatch
            ? (entryPath == fileName)
            : (entryPath == fileName || entryPath.endsWith("/" + fileName));
        if (!matches)
            continue;

        const la_int64_t size = archive_entry_size(entry);
        if (size <= 0 || size > kMaxInMemorySize) {
            error = QString("Entry size out of bounds (0 or > 256 MB): %1").arg(fileName);
            failed = true;
            break;
        }
        data.resize(static_cast<qsizetype>(size));
        qsizetype total = 0;
        while (total < data.size()) {
            const la_ssize_t n = archive_read_data(a, data.data() + total,
                                                   static_cast<size_t>(data.size() - total));
            if (n <= 0)
                break;
            total += n;
        }
        if (total != data.size()) {
            error = QString("Could not read %1 from the archive: %2")
                        .arg(fileName, QString::fromUtf8(archive_error_string(a)));
            data.clear();
            failed = true;
            break;
        }
        found = true;
    }

    if (!found && !failed)
        error = QString("File not found in archive: %1").arg(fileName);

    archive_read_close(a);
    archive_read_free(a);
    return found;
}

bool ArchiveExtractor::createZip(const QString& sourceDir, const QString& destZipPath, QString& error) {
    // QSaveFile only replaces destZipPath once the whole archive is written, so a failure
    // never leaves a truncated ZIP in place of the user's original.
    QSaveFile output(destZipPath);
    if (!output.open(QIODevice::WriteOnly)) {
        error = QString("Could not open output archive %1: %2").arg(destZipPath, output.errorString());
        return false;
    }

    struct archive* a = archive_write_new();
    archive_write_set_format_zip(a);
    archive_write_set_bytes_in_last_block(a, 1); // no zero padding after the ZIP data
    if (archive_write_open(a, &output, nullptr, writeToSaveFile, nullptr) != ARCHIVE_OK) {
        error = QString("Could not open output archive: %1").arg(archive_error_string(a));
        archive_write_free(a);
        return false;
    }

    const QDir source(QFileInfo(sourceDir).absoluteFilePath());
    bool ok = true;
    QDirIterator it(source.absolutePath(), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (ok && it.hasNext()) {
        const QString filePath = it.next();
        const QString relPath = source.relativeFilePath(filePath);

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            error = QString("Could not read %1: %2").arg(filePath, file.errorString());
            ok = false;
            break;
        }

        struct archive_entry* entry = archive_entry_new();
#ifdef Q_OS_WIN
        archive_entry_copy_pathname_w(entry, reinterpret_cast<const wchar_t*>(relPath.utf16()));
#else
        archive_entry_set_pathname(entry, relPath.toUtf8().constData());
#endif
        archive_entry_set_size(entry, file.size());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_entry_set_mtime(entry, QFileInfo(file).lastModified().toSecsSinceEpoch(), 0);

        if (archive_write_header(a, entry) != ARCHIVE_OK) {
            error = QString("Header write error: %1").arg(archive_error_string(a));
            ok = false;
        }
        while (ok && !file.atEnd()) {
            const QByteArray chunk = file.read(64 * 1024);
            if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
                error = QString("Could not read %1: %2").arg(filePath, file.errorString());
                ok = false;
            } else if (archive_write_data(a, chunk.constData(), static_cast<size_t>(chunk.size())) < 0) {
                error = QString("Could not write %1 to the archive: %2").arg(relPath, archive_error_string(a));
                ok = false;
            }
        }
        archive_entry_free(entry);
    }

    if (archive_write_close(a) != ARCHIVE_OK && ok) {
        error = QString("Could not finish the archive: %1").arg(archive_error_string(a));
        ok = false;
    }
    archive_write_free(a);

    if (!ok) {
        output.cancelWriting();
        return false;
    }
    if (!output.commit()) {
        error = QString("Could not save %1: %2").arg(destZipPath, output.errorString());
        return false;
    }
    return true;
}

QStringList ArchiveExtractor::listEntries(const QString& archivePath, QString& error) {
    struct archive* a = openForReading(archivePath, error);
    if (!a)
        return {};

    constexpr int kMaxListEntryCount = 100'000;
    QStringList entries;
    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (isPlainFileOrFolder(entry) && archive_entry_filetype(entry) == AE_IFREG) {
            if (entries.size() >= kMaxListEntryCount) {
                error = QString("Archive contains too many entries (> %1)").arg(kMaxListEntryCount);
                archive_read_close(a);
                archive_read_free(a);
                return {};
            }
            entries.append(entryPathName(entry));
        }
        archive_read_data_skip(a);
    }

    archive_read_close(a);
    archive_read_free(a);
    return entries;
}

int ArchiveExtractor::copyData(struct archive* ar, struct archive* aw,
                               qint64& bytesWritten, qint64 maxBytes) {
    const void* buff;
    size_t size;
    la_int64_t offset;

    for (;;) {
        int r = archive_read_data_block(ar, &buff, &size, &offset);
        if (r == ARCHIVE_EOF)
            return ARCHIVE_OK;
        if (r < ARCHIVE_WARN)
            return r;
        bytesWritten += static_cast<qint64>(size);
        if (bytesWritten > maxBytes)
            return ARCHIVE_FATAL;
        r = archive_write_data_block(aw, buff, size, offset);
        if (r < ARCHIVE_WARN) {
            qWarning() << "archive_write_data_block error:" << archive_error_string(aw);
            return r;
        }
    }
}
