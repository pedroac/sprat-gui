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
};
