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

AppSettings CliToolsConfig::loadAppSettings() {
    QSettings settings(configPath(), QSettings::IniFormat);
    AppSettings out;

    const QString workspaceColor = settings.value("settings/workspace_color", settings.value("settings/canvas_color")).toString();
    if (!workspaceColor.isEmpty()) {
        out.workspaceColor = QColor(workspaceColor);
    }
    const QString spriteFrameColor = settings.value("settings/sprite_frame_color", settings.value("settings/frame_color")).toString();
    if (!spriteFrameColor.isEmpty()) {
        out.spriteFrameColor = QColor(spriteFrameColor);
    }
    out.showCheckerboard = settings.value("settings/show_checkerboard", out.showCheckerboard).toBool();
    out.showBorders = settings.value("settings/show_borders", out.showBorders).toBool();

    const QString borderColor = settings.value("settings/border_color").toString();
    if (!borderColor.isEmpty()) {
        out.borderColor = QColor(borderColor);
    }
    const QString detectionSelectedColor = settings.value("settings/detection_selected_color").toString();
    if (!detectionSelectedColor.isEmpty()) {
        out.detectionSelectedColor = QColor(detectionSelectedColor);
    }
    out.borderStyle = static_cast<Qt::PenStyle>(
        settings.value("settings/border_style", static_cast<int>(out.borderStyle)).toInt());
    return out;
}

CliPaths CliToolsConfig::loadCliPaths() {
    QSettings settings(configPath(), QSettings::IniFormat);
    CliPaths out;
    out.baseDir = settings.value("cli/base_dir").toString();
    out.layoutBinary = resolveBinary("spratlayout", out.baseDir);
    out.packBinary = resolveBinary("spratpack", out.baseDir);
    out.convertBinary = resolveBinary("spratconvert", out.baseDir);
    out.framesBinary = resolveBinary("spratframes", out.baseDir);
    out.unpackBinary = resolveBinary("spratunpack", out.baseDir);
    return out;
}

void CliToolsConfig::saveAppSettings(const AppSettings& settings, const CliPaths& cliPaths) {
    QString pathToConfig = configPath();
    QDir().mkpath(QFileInfo(pathToConfig).path());
    QSettings qsettings(pathToConfig, QSettings::IniFormat);
    qsettings.setValue("settings/workspace_color", settings.workspaceColor.name(QColor::HexArgb));
    qsettings.setValue("settings/sprite_frame_color", settings.spriteFrameColor.name(QColor::HexArgb));
    qsettings.setValue("settings/show_checkerboard", settings.showCheckerboard);
    qsettings.setValue("settings/show_borders", settings.showBorders);
    qsettings.setValue("settings/border_color", settings.borderColor.name(QColor::HexArgb));
    qsettings.setValue("settings/detection_selected_color", settings.detectionSelectedColor.name(QColor::HexArgb));
    qsettings.setValue("settings/border_style", static_cast<int>(settings.borderStyle));
    qsettings.setValue("cli/base_dir", cliPaths.baseDir);
}

QString CliToolsConfig::resolveBinary(const QString& name, const QString& baseDir) {
    // 1. Check provided baseDir
    if (!baseDir.isEmpty()) {
        QFileInfo fi(QDir(baseDir).filePath(name));
        if (fi.exists() && fi.isExecutable()) {
            return fi.absoluteFilePath();
        }
    }

    // 2. Check current directory
    QFileInfo localFi(QDir::current().filePath(name));
    if (localFi.exists() && localFi.isExecutable()) {
        return localFi.absoluteFilePath();
    }

    // 3. Check standard PATH
    QString inPath = QStandardPaths::findExecutable(name);
    if (!inPath.isEmpty()) {
        return inPath;
    }

    // 4. Check sibling directories (development/relative paths)
    QString siblingBin = findSiblingSpratCliBinary(name);
    if (!siblingBin.isEmpty()) {
        return siblingBin;
    }

    // 5. Check home config/bin locations
    QStringList homeLocations;
    homeLocations << QDir::homePath() + "/.local/bin/" + name;
    homeLocations << QDir::homePath() + "/.config/sprat/bin/" + name;
    
    for (const QString& loc : homeLocations) {
        if (QFile::exists(loc) && QFileInfo(loc).isExecutable()) {
            return loc;
        }
    }

    return QString();
}

