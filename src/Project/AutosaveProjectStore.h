#pragma once

#include <QJsonObject>
#include <QString>

class AutosaveProjectStore {
public:
    static QString defaultPath();
    static bool save(const QString& path, const QJsonObject& root, QString& error);
    static bool load(const QString& path, QJsonObject& root, QString& error);
};
