#pragma once

#include <QString>
#include "AppSettings.h"
#include "CliPaths.h"

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

    // Clear all in-process caches for checkBinaryVersion, queryTransformsDir and
    // queryDefaultProfilesConfig.  Call after a CLI install so the fresh binaries
    // are re-queried on the next check().
    static void invalidateCaches();
};
