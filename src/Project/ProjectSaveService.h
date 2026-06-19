#pragma once

#include <functional>
#include <QJsonObject>
#include <QStringList>
#include "ExportModels.h"
#include "SpratProfilesConfig.h"

class ProjectSaveService {
public:
    struct SaveCallbacks {
        std::function<void(bool)>            setLoading;
        std::function<void(const QString&)>  setStatus;
        std::function<bool()>                shouldCancel;
        std::function<bool(const QString&, const QStringList&, const QString&, const QByteArray*, QByteArray*)> runProcess;
        std::function<void(const ExportLogEntry&)> logEntry;  // optional
    };

    static bool save(
        SaveConfig config,
        const QString& layoutInputPath,
        const QStringList& framePaths,
        const QString& sourceFolder,
        const QVector<SpratProfile>& availableProfiles,
        const QString& selectedProfileName,
        const QString& spratLayoutBin,
        const QString& spratPackBin,
        const QString& spratConvertBin,
        const QJsonObject& projectPayload,
        QString& savedDestination,
        QString& error,
        const QString& deduplicateMode = "none",
        SaveCallbacks callbacks = {}
    );

    static bool writeProjectJson(
        const QString& projectFolder,
        const QJsonObject& payload,
        QString& error
    );

    static bool create(
        const QString& name,
        const QString& parentDir,
        bool createSubfolder,
        QString& outProjectPath,
        QString& error
    );
};
