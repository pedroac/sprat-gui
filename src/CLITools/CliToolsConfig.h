#pragma once

#include <QString>
#include "models.h"

class CliToolsConfig {
public:
    static QString configPath();
    static QString loadBinDir();
    static CliPaths loadOverrides();
    static void saveBinDir(const QString& path);
    static void saveOverride(const QString& key, const QString& path);
    static QString resolveBinary(const QString& name, const QString& overridePath, const QString& binDir);
};
