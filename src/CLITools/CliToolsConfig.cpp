#include "CliToolsConfig.h"

#include "SpratCliLocator.h"

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

AppSettings CliToolsConfig::loadAppSettings() {
    QSettings settings(configPath(), QSettings::IniFormat);
    AppSettings out;

    const QString canvasColor = settings.value("settings/canvas_color").toString();
    if (!canvasColor.isEmpty()) {
        out.canvasColor = QColor(canvasColor);
    }
    const QString frameColor = settings.value("settings/frame_color").toString();
    if (!frameColor.isEmpty()) {
        out.frameColor = QColor(frameColor);
    }
    out.showBorders = settings.value("settings/show_borders", out.showBorders).toBool();

    const QString borderColor = settings.value("settings/border_color").toString();
    if (!borderColor.isEmpty()) {
        out.borderColor = QColor(borderColor);
    }
    out.borderStyle = static_cast<Qt::PenStyle>(
        settings.value("settings/border_style", static_cast<int>(out.borderStyle)).toInt());
    return out;
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

void CliToolsConfig::saveAppSettings(const AppSettings& settings) {
    QString pathToConfig = configPath();
    QDir().mkpath(QFileInfo(pathToConfig).path());
    QSettings qsettings(pathToConfig, QSettings::IniFormat);
    qsettings.setValue("settings/canvas_color", settings.canvasColor.name(QColor::HexArgb));
    qsettings.setValue("settings/frame_color", settings.frameColor.name(QColor::HexArgb));
    qsettings.setValue("settings/show_borders", settings.showBorders);
    qsettings.setValue("settings/border_color", settings.borderColor.name(QColor::HexArgb));
    qsettings.setValue("settings/border_style", static_cast<int>(settings.borderStyle));
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

    QString siblingBin = findSiblingSpratCliBinary(name);
    if (!siblingBin.isEmpty()) {
        return siblingBin;
    }

    QString localBin = QDir::homePath() + "/.local/bin/" + name;
    if (QFile::exists(localBin) && QFileInfo(localBin).isExecutable()) {
        return localBin;
    }

    return QString();
}
