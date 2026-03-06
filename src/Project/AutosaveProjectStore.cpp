#include "AutosaveProjectStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>

QString AutosaveProjectStore::defaultPath() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(configDir).filePath("sprat/gui_saved.json");
}

bool AutosaveProjectStore::load(const QString& path, QJsonObject& root, QString& error) {
    QFile file(path);
    if (!file.exists()) {
        error = "No autosave found.";
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        error = "Could not open autosave file.";
        return false;
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull() || !doc.isObject()) {
        error = "Invalid autosave file.";
        return false;
    }
    root = doc.object();
    return true;
}

bool AutosaveProjectStore::save(const QString& path, const QJsonObject& root, QString& error) {
    const QFileInfo fileInfo(path);
    const QString parentDir = fileInfo.path();
    if (!parentDir.isEmpty()) {
        QDir dir(parentDir);
        if (!dir.exists() && !dir.mkpath(".")) {
            error = "Could not create autosave directory.";
            return false;
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = "Autosave failed";
        return false;
    }
    if (file.write(QJsonDocument(root).toJson()) < 0) {
        error = "Autosave failed";
        return false;
    }
    file.close();
    return true;
}
