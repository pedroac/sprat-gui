#pragma once

#include <QJsonObject>
#include <QString>

class ProjectFileLoader {
public:
    static bool load(const QString& path, QJsonObject& root, QString& error);
};
