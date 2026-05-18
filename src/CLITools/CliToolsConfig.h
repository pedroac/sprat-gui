#pragma once

#include <QString>
#include "models.h"

class CliToolsConfig {
public:
    static QString configPath();
    static AppSettings loadAppSettings();
    static CliPaths loadCliPaths();
    static void saveAppSettings(const AppSettings& settings, const CliPaths& cliPaths);
    static QString resolveBinary(const QString& name, const QString& baseDir = QString());
    static QString checkBinaryVersion(const QString& binaryPath);
    static QString queryTransformsDir(const QString& convertBinaryPath);
    static QString queryDefaultProfilesConfig(const QString& layoutBinaryPath);
    static QString defaultInstallDir();
    static void ensureConfigExists();
    static void saveInstalledCliVersion(const QString& version);
    static QString loadInstalledCliVersion();
    static QStringList loadRecentProjects();
    static void saveRecentProjects(const QStringList& recent);
};
