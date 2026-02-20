#pragma once

#include <functional>
#include <QJsonObject>
#include <QStringList>
#include "models.h"
#include "SpratProfilesConfig.h"

class QWidget;

class ProjectSaveService {
public:
    static bool save(
        QWidget* parent,
        SaveConfig config,
        const QString& layoutInputPath,
        const QStringList& framePaths,
        const QVector<SpratProfile>& availableProfiles,
        const QString& selectedProfileName,
        const QString& spratLayoutBin,
        const QString& spratPackBin,
        const QString& spratConvertBin,
        const QJsonObject& projectPayload,
        QString& savedDestination,
        const std::function<void(bool)>& setLoading,
        const std::function<void(const QString&)>& setStatus
    );
};
