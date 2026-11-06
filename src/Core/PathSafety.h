#pragma once

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>

/// Checks for names and paths that come from project files, where they could point
/// anywhere on disk (absolute paths, "..") before the app writes to or deletes them.
namespace PathSafety {

/// True for a relative path that stays inside the folder it is resolved against,
/// e.g. "ui" or "ui/icons", but not "", "/home", "C:/x", "..", or "a/../../b".
inline bool isContainedRelativePath(const QString& path) {
    if (path.trimmed().isEmpty() || QDir::isAbsolutePath(path)
            || path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1Char('\\'))
            || path.contains(QLatin1Char(':'))) {
        return false;
    }
    static const QRegularExpression separators(QStringLiteral("[/\\\\]"));
    const QStringList parts = path.split(separators, Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        if (part == QLatin1String(".."))
            return false;
    }
    return !parts.isEmpty();
}

/// True for a single file or folder name: no separators, not "." or "..".
inline bool isPlainName(const QString& name) {
    const QString trimmed = name.trimmed();
    return !trimmed.isEmpty() && trimmed != QLatin1String(".") && trimmed != QLatin1String("..")
        && !trimmed.contains(QLatin1Char('/')) && !trimmed.contains(QLatin1Char('\\'))
        && !trimmed.contains(QLatin1Char(':'));
}

/// Absolute, cleaned path with symlinks resolved in the part of it that exists
/// (e.g. macOS /var/folders/x/new -> /private/var/folders/x/new).
inline QString resolvedPath(const QString& path) {
    const QString absolute = QDir::cleanPath(QDir(path).absolutePath());
    QString existing = absolute;
    QStringList missingParts;
    while (!QFileInfo::exists(existing)) {
        const QFileInfo info(existing);
        if (info.path() == existing) // reached a root that doesn't exist
            return absolute;
        missingParts.prepend(info.fileName());
        existing = info.path();
    }
    QString resolved = QFileInfo(existing).canonicalFilePath();
    for (const QString& part : missingParts)
        resolved = QDir(resolved).filePath(part);
    return QDir::cleanPath(resolved);
}

/// True when path is strictly inside folder (not folder itself). Both are made absolute,
/// cleaned and, when they exist, resolved through symlinks first, so neither
/// "folder/a/../.." nor a symlink in folder pointing elsewhere counts as inside.
inline bool isInsideFolder(const QString& path, const QString& folder) {
    if (path.isEmpty() || folder.isEmpty())
        return false;
    const QString cleanPath = resolvedPath(path);
    QString cleanFolder = resolvedPath(folder);
    if (!cleanFolder.endsWith(QLatin1Char('/')))
        cleanFolder += QLatin1Char('/');
    return cleanPath.startsWith(cleanFolder, Qt::CaseSensitive) && cleanPath.size() > cleanFolder.size();
}

/// Turns a display name (an atlas name) into a folder name: lower case, spaces as "_",
/// and characters that would make it a path replaced, e.g. "UI / Menus" -> "ui___menus".
inline QString folderNameFromLabel(const QString& label, const QString& fallback = QStringLiteral("atlas")) {
    QString name = label.trimmed().toLower();
    for (QChar& c : name) {
        if (c == QLatin1Char(' ') || c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':'))
            c = QLatin1Char('_');
    }
    return isPlainName(name) ? name : fallback;
}

} // namespace PathSafety
