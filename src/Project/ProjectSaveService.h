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
        QString& error,
        const std::function<void(bool)>& setLoading = nullptr,
        const std::function<void(const QString&)>& setStatus = nullptr,
        const std::function<bool(const QString&, const QStringList&, const QString&, const QByteArray*, QByteArray*)>& runProcessFunc = nullptr
    );
};
