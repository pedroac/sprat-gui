#include "ProjectFileLoader.h"
#include "ArchiveExtractor.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace {
QString trProjectFileLoader(const char* text) {
    return QCoreApplication::translate("ProjectFileLoader", text);
}
}

bool ProjectFileLoader::load(const QString& path, QJsonObject& root, QString& error) {
    QByteArray jsonData;
    if (path.endsWith(".zip", Qt::CaseInsensitive)) {
        if (!ArchiveExtractor::readFileFromArchive(path, "project.spart.json", jsonData, error, true)) {
            return false;
        }
    } else {
        constexpr qint64 kMaxFileSize = 256 * 1024 * 1024; // 256 MB
        if (QFileInfo(path).size() > kMaxFileSize) {
            error = trProjectFileLoader("File is too large (> 256 MB).");
            return false;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            error = trProjectFileLoader("Could not open file.");
            return false;
        }
        jsonData = file.readAll();
    }

    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (doc.isNull() || !doc.isObject()) {
        error = trProjectFileLoader("Invalid JSON data.");
        return false;
    }
    root = doc.object();
    return true;
}
