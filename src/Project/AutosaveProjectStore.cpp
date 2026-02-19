#include "AutosaveProjectStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>

QString AutosaveProjectStore::defaultPath() {
    return QDir::homePath() + "/.config/sprat/gui_saved.json";
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
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = "Autosave failed";
        return false;
    }
    file.write(QJsonDocument(root).toJson());
    file.close();
    return true;
}
