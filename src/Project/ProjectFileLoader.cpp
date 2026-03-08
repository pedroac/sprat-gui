#include "ProjectFileLoader.h"
#include "ArchiveExtractor.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>

namespace {
QString trProjectFileLoader(const char* text) {
    return QCoreApplication::translate("ProjectFileLoader", text);
}
}

bool ProjectFileLoader::load(const QString& path, QJsonObject& root, QString& error) {
    QByteArray jsonData;
    if (path.endsWith(".zip", Qt::CaseInsensitive)) {
        if (!ArchiveExtractor::readFileFromArchive(path, "project.spart.json", jsonData, error)) {
            return false;
        }
    } else {
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
