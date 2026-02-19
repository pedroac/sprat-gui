#include "CliToolsConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

QString CliToolsConfig::configPath() {
    return QDir::homePath() + "/.config/sprat/sprat.conf";
}

QString CliToolsConfig::loadBinDir() {
    QSettings settings(configPath(), QSettings::IniFormat);
    return settings.value("cli/bin_dir").toString();
}

CliPaths CliToolsConfig::loadOverrides() {
    QSettings settings(configPath(), QSettings::IniFormat);
    CliPaths paths;
    paths.layoutBinary = settings.value("cli/spratlayout").toString();
    paths.packBinary = settings.value("cli/spratpack").toString();
    paths.convertBinary = settings.value("cli/spratconvert").toString();
    return paths;
}

void CliToolsConfig::saveBinDir(const QString& path) {
    QString pathToConfig = configPath();
    QDir().mkpath(QFileInfo(pathToConfig).path());
    QSettings settings(pathToConfig, QSettings::IniFormat);
    settings.setValue("cli/bin_dir", path);
}

void CliToolsConfig::saveOverride(const QString& key, const QString& path) {
    QString pathToConfig = configPath();
    QDir().mkpath(QFileInfo(pathToConfig).path());
    QSettings settings(pathToConfig, QSettings::IniFormat);
    if (path.isEmpty()) {
        settings.remove(key);
    } else {
        settings.setValue(key, path);
    }
}

QString CliToolsConfig::resolveBinary(const QString& name, const QString& overridePath, const QString& binDir) {
    if (!overridePath.isEmpty()) {
        QFileInfo overrideInfo(overridePath);
        if (overrideInfo.exists() && overrideInfo.isExecutable()) {
            return overrideInfo.absoluteFilePath();
        }
    }

    QString inPath = QStandardPaths::findExecutable(name);
    if (!inPath.isEmpty()) {
        return inPath;
    }

    if (!binDir.isEmpty()) {
        QFileInfo fi(QDir(binDir).filePath(name));
        if (fi.exists() && fi.isExecutable()) {
            return fi.absoluteFilePath();
        }
    }

    QString localBin = QDir::homePath() + "/.local/bin/" + name;
    if (QFile::exists(localBin) && QFileInfo(localBin).isExecutable()) {
        return localBin;
    }

    return QString();
}
