#include "CliToolsConfig.h"

#include "SpratCliLocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

QString CliToolsConfig::configPath() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(configDir).filePath("sprat/sprat.conf");
}

QString CliToolsConfig::defaultInstallDir() {
#ifdef Q_OS_WIN
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("bin");
#else
    return QDir::homePath() + "/.local/bin";
#endif
}

void CliToolsConfig::ensureConfigExists() {
    QString path = configPath();
    if (!QFile::exists(path)) {
        QDir().mkpath(QFileInfo(path).path());
        saveAppSettings(loadAppSettings(), loadCliPaths());
    }
}

AppSettings CliToolsConfig::loadAppSettings() {
    QSettings settings(configPath(), QSettings::IniFormat);
    AppSettings out;

    if (settings.contains("settings/workspace_color")) {
        out.workspaceColor = QColor(settings.value("settings/workspace_color").toString());
    }
    if (settings.contains("settings/sprite_frame_color")) {
        out.spriteFrameColor = QColor(settings.value("settings/sprite_frame_color").toString());
    }
    out.showCheckerboard = settings.value("settings/show_checkerboard", out.showCheckerboard).toBool();
    out.showBorders = settings.value("settings/show_borders", out.showBorders).toBool();

    if (settings.contains("settings/border_color")) {
        out.borderColor = QColor(settings.value("settings/border_color").toString());
    }
    if (settings.contains("settings/detection_selected_color")) {
        out.detectionSelectedColor = QColor(settings.value("settings/detection_selected_color").toString());
    }
    out.borderStyle = static_cast<Qt::PenStyle>(
        settings.value("settings/border_style", static_cast<int>(out.borderStyle)).toInt());
    return out;
}

CliPaths CliToolsConfig::loadCliPaths() {
    QSettings settings(configPath(), QSettings::IniFormat);
    CliPaths out;
    out.baseDir = settings.value("cli/base_dir", defaultInstallDir()).toString();
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

#include <QProcess>

QString CliToolsConfig::checkBinaryVersion(const QString& binaryPath) {
    if (binaryPath.isEmpty()) {
        return QString();
    }
    QProcess process;
    process.start(binaryPath, QStringList() << "--version");
    if (!process.waitForFinished(2000)) {
        return QString();
    }
    QString output = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    // Expected output format: "spratlayout version v0.1.0"
    QStringList parts = output.split(' ');
    if (parts.size() >= 3) {
        return parts.last();
    }
    return QString();
}

QString CliToolsConfig::resolveBinary(const QString& name, const QString& baseDir) {
    QString executableName = name;
#ifdef Q_OS_WIN
    if (!executableName.endsWith(".exe", Qt::CaseInsensitive)) {
        executableName += ".exe";
    }
#endif

    // 1. Check provided baseDir
    if (!baseDir.isEmpty()) {
        QFileInfo fi(QDir(baseDir).filePath(executableName));
        if (fi.exists() && fi.isExecutable()) {
            return fi.absoluteFilePath();
        }
    }

    // 2. Check application directory
    QFileInfo appFi(QDir(QCoreApplication::applicationDirPath()).filePath(executableName));
    if (appFi.exists() && appFi.isExecutable()) {
        return appFi.absoluteFilePath();
    }

    // 3. Check current directory
    QFileInfo localFi(QDir::current().filePath(executableName));
    if (localFi.exists() && localFi.isExecutable()) {
        return localFi.absoluteFilePath();
    }

    // 4. Check standard PATH
    QString inPath = QStandardPaths::findExecutable(name);
    if (!inPath.isEmpty()) {
        return inPath;
    }

    // 5. Check sibling directories (development/relative paths)
    QString siblingBin = findSiblingSpratCliBinary(name);
    if (!siblingBin.isEmpty()) {
        return siblingBin;
    }

    // 6. Check home config/bin locations
    QStringList homeLocations;
    homeLocations << QDir::homePath() + "/.local/bin/" + executableName;
    homeLocations << QDir::homePath() + "/.config/sprat/bin/" + executableName;
    
    for (const QString& loc : homeLocations) {
        if (QFile::exists(loc) && QFileInfo(loc).isExecutable()) {
            return loc;
        }
    }

    return QString();
}
