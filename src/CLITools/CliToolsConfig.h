#pragma once

#include <QString>
#include "models.h"

class CliToolsConfig {
public:
    static QString configPath();
    static QString loadBinDir();
    static CliPaths loadOverrides();
    static AppSettings loadAppSettings();
    static void saveBinDir(const QString& path);
    static void saveOverride(const QString& key, const QString& path);
    static void saveAppSettings(const AppSettings& settings);
    static QString resolveBinary(const QString& name, const QString& overridePath, const QString& binDir);
};
