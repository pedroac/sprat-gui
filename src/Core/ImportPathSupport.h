#pragma once

#include <QString>
#include <QUrl>

namespace ImportPathSupport {

inline bool isSupportedLocalImportPath(const QString& path) {
    const QString lowerPath = path.toLower();
    return lowerPath.endsWith(".zip") || lowerPath.endsWith(".json") ||
           lowerPath.endsWith(".tar") || lowerPath.endsWith(".tar.gz") ||
           lowerPath.endsWith(".tar.bz2") || lowerPath.endsWith(".tar.xz") ||
           lowerPath.endsWith(".png") || lowerPath.endsWith(".jpg") ||
           lowerPath.endsWith(".jpeg") || lowerPath.endsWith(".bmp") ||
           lowerPath.endsWith(".gif") || lowerPath.endsWith(".webp") ||
           lowerPath.endsWith(".tga") || lowerPath.endsWith(".dds");
}

inline bool isSupportedRemoteImportUrl(const QUrl& url) {
    if (!url.isValid() || url.isLocalFile()) {
        return false;
    }

    const QString scheme = url.scheme().toLower();
    return scheme == "http" || scheme == "https";
}

}
