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
    return QDir(QCoreApplication::applicationDirPath()).filePath("cli");
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
    out.deduplicateMode = settings.value("settings/deduplicate_mode", out.deduplicateMode).toString();
    out.syncMode = syncModeFromString(settings.value("settings/sync_mode", syncModeToString(out.syncMode)).toString());
    out.theme = settings.value("settings/theme", out.theme).toString();
    out.onionSkinOpacity        = settings.value("settings/onion_skin_opacity", out.onionSkinOpacity).toInt();
    out.propagateEditsToChecked = settings.value("settings/propagate_edits_to_checked", out.propagateEditsToChecked).toBool();
    out.flipbookMode        = flipbookModeFromString(settings.value("settings/flipbook_mode", "none").toString());
    out.frameZoomMode       = frameZoomModeFromString(settings.value("settings/frame_zoom_mode", "fit").toString());
    out.layoutZoomOnChange  = layoutZoomOnChangeFromString(settings.value("settings/layout_zoom_on_change", "no_change").toString());
    out.layoutLabelMode     = layoutLabelModeFromString(settings.value("settings/layout_label_mode", "name").toString());
    out.exportZoomOnChange        = exportZoomOnChangeFromString(settings.value("settings/export_zoom_on_change", "fit").toString());
    out.exportDefaultOutputFolder = settings.value("settings/export_default_output_folder",
        QDir::homePath() + "/Sprat").toString();
    out.exportDefaultFormat       = settings.value("settings/export_default_format",       "none").toString();
    out.exportDefaultScaleFilter  = settings.value("settings/export_default_scale_filter", "nearest").toString();
    out.coordUnit = settings.value("settings/coord_unit", 0).toInt() == 1
                  ? CoordUnit::Percent : CoordUnit::Pixels;
    out.showTrimRect = settings.value("settings/show_trim_rect", out.showTrimRect).toBool();
    if (settings.contains("settings/trim_rect_color")) {
        out.trimRectColor = QColor(settings.value("settings/trim_rect_color").toString());
    }
    out.trimRectStyle = static_cast<Qt::PenStyle>(
        settings.value("settings/trim_rect_style", static_cast<int>(out.trimRectStyle)).toInt());
    out.defaultProjectsFolder = settings.value("settings/default_projects_folder",
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("Sprat Projects")).toString();
    out.recentProjects = settings.value("recent/projects", out.recentProjects).toStringList();
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

#ifdef Q_OS_WASM
#include <emscripten.h>
extern "C" { void sync_idbfs(); }
#endif

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
    qsettings.setValue("settings/deduplicate_mode", settings.deduplicateMode);
    qsettings.setValue("settings/sync_mode", syncModeToString(settings.syncMode));
    qsettings.setValue("settings/theme", settings.theme);
    qsettings.setValue("settings/default_projects_folder", settings.defaultProjectsFolder);
    qsettings.setValue("settings/onion_skin_opacity",          settings.onionSkinOpacity);
    qsettings.setValue("settings/propagate_edits_to_checked", settings.propagateEditsToChecked);
    qsettings.setValue("settings/flipbook_mode",              flipbookModeToString(settings.flipbookMode));
    qsettings.setValue("settings/frame_zoom_mode",            frameZoomModeToString(settings.frameZoomMode));
    qsettings.setValue("settings/layout_zoom_on_change",      layoutZoomOnChangeToString(settings.layoutZoomOnChange));
    qsettings.setValue("settings/layout_label_mode",          layoutLabelModeToString(settings.layoutLabelMode));
    qsettings.setValue("settings/export_zoom_on_change",      exportZoomOnChangeToString(settings.exportZoomOnChange));
    qsettings.setValue("settings/export_default_output_folder", settings.exportDefaultOutputFolder);
    qsettings.setValue("settings/export_default_format",       settings.exportDefaultFormat);
    qsettings.setValue("settings/export_default_scale_filter", settings.exportDefaultScaleFilter);
    qsettings.setValue("settings/coord_unit",
        settings.coordUnit == CoordUnit::Percent ? 1 : 0);
    qsettings.setValue("settings/show_trim_rect", settings.showTrimRect);
    qsettings.setValue("settings/trim_rect_color", settings.trimRectColor.name(QColor::HexArgb));
    qsettings.setValue("settings/trim_rect_style", static_cast<int>(settings.trimRectStyle));
    qsettings.setValue("cli/base_dir", cliPaths.baseDir);
    qsettings.sync();

#ifdef Q_OS_WASM
    sync_idbfs();
#endif
}

#include <QProcess>
#include <QRegularExpression>
#ifdef SPRAT_EMBEDDED_CLI
#include "EmbeddedCli.h"
#endif

QString CliToolsConfig::checkBinaryVersion(const QString& binaryPath) {
#ifdef SPRAT_EMBEDDED_CLI
    Q_UNUSED(binaryPath);
    return SPRAT_CLI_VERSION;
#else
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
    static const QRegularExpression versionRe("(v\\d+\\.\\d+\\.\\d+(?:[.\\-][a-zA-Z0-9]+)*)");
    QRegularExpressionMatch match = versionRe.match(output);
    return match.hasMatch() ? match.captured(1) : QString();
#endif
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
    QDir appDir(QCoreApplication::applicationDirPath());
    QFileInfo appFi(appDir.filePath(executableName));
    if (appFi.exists() && appFi.isExecutable()) {
        return appFi.absoluteFilePath();
    }

    // 2.1 Check "bin" or "cli" subdirectory in application directory
    QFileInfo binFi(appDir.filePath(QString("bin/%1").arg(executableName)));
    if (binFi.exists() && binFi.isExecutable()) {
        return binFi.absoluteFilePath();
    }
    QFileInfo cliFi(appDir.filePath(QString("cli/%1").arg(executableName)));
    if (cliFi.exists() && cliFi.isExecutable()) {
        return cliFi.absoluteFilePath();
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

QString CliToolsConfig::queryTransformsDir(const QString& convertBinaryPath) {
#ifdef SPRAT_EMBEDDED_CLI
    Q_UNUSED(convertBinaryPath);
    CliResult result = EmbeddedCli::run("spratconvert", {"--transforms-dir"});
    return QString::fromLocal8Bit(result.stdOut).trimmed();
#else
    if (convertBinaryPath.isEmpty()) return {};
    QProcess process;
    process.start(convertBinaryPath, {"--transforms-dir"});
    if (!process.waitForFinished(2000)) return {};
    return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
#endif
}

QString CliToolsConfig::queryDefaultProfilesConfig(const QString& layoutBinaryPath) {
#ifdef SPRAT_EMBEDDED_CLI
    Q_UNUSED(layoutBinaryPath);
    CliResult result = EmbeddedCli::run("spratlayout", {"--default-profiles-config"});
    return QString::fromLocal8Bit(result.stdOut).trimmed();
#else
    if (layoutBinaryPath.isEmpty()) return {};
    QProcess process;
    process.start(layoutBinaryPath, {"--default-profiles-config"});
    if (!process.waitForFinished(2000)) return {};
    return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
#endif
}

void CliToolsConfig::saveInstalledCliVersion(const QString& version) {
    QString pathToConfig = configPath();
    QDir().mkpath(QFileInfo(pathToConfig).path());
    QSettings qsettings(pathToConfig, QSettings::IniFormat);
    qsettings.setValue("cli/installed_version", version);
    qsettings.sync();
}

QString CliToolsConfig::loadInstalledCliVersion() {
    QSettings settings(configPath(), QSettings::IniFormat);
    return settings.value("cli/installed_version", QString()).toString();
}

QStringList CliToolsConfig::loadRecentProjects() {
    QSettings s(configPath(), QSettings::IniFormat);
    return s.value("recent/projects").toStringList();
}

void CliToolsConfig::saveRecentProjects(const QStringList& recent) {
    QSettings s(configPath(), QSettings::IniFormat);
    s.setValue("recent/projects", recent);
    s.sync();
}
