#pragma once

#include <functional>
#include <QJsonObject>
#include "models.h"

class QWidget;

class ProjectSaveService {
public:
    static bool save(
        QWidget* parent,
        SaveConfig config,
        const QString& currentFolder,
        const QString& profile,
        int padding,
        bool trimTransparent,
        const QString& spratLayoutBin,
        const QString& spratPackBin,
        const QString& spratConvertBin,
        const QJsonObject& projectPayload,
        QString& savedDestination,
        const std::function<void(bool)>& setLoading,
        const std::function<void(const QString&)>& setStatus);
};
