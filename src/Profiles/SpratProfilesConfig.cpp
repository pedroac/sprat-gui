#include "SpratProfilesConfig.h"
#include "ResolutionUtils.h"
#include "CliToolsConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

namespace {
SpratProfile genericDefaultProfile(const QString& name = QString()) {
    SpratProfile p;
    p.name = name;
    p.preset = "quality";
    p.maxWidth = -1;
    p.maxHeight = -1;
    p.targetResolutionWidth = 1024;
    p.targetResolutionHeight = 1024;
    p.targetResolutionUseSource = false;
    p.resolutionReference = "largest";
    p.padding = 0;
    p.extrude = 0;
    p.threads = 0;
    p.trimTransparent = true;
    p.allowRotation = false;
    p.scale = 1.0;
    p.multipack = false;
    p.sort = "none";
    p.gpuCompress = "";
    p.dilate = 0;
    p.imageFormat = "png";
    p.imageQuality = 100;
    p.zopfli = false;
    p.frameLines = false;
    p.frameLineWidth = 1;
    p.frameLineColor = "255,0,0,255";
    p.atlasIndex = -1;
    p.autoAnimations = false;
    return p;
}

bool toBool(const QString& value, bool fallback) {
    const QString normalized = value.trimmed().toLower();
    if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "false" || normalized == "0" || normalized == "no" || normalized == "off") {
        return false;
    }
    return fallback;
}

QString migrateModOptimizeToPreset(const QString& mode, const QString& optimize) {
    const QString m = mode.trimmed().toLower();
    const QString o = optimize.trimmed().toLower();
    if (m == "pot") return "pot";
    if (m == "fast") return "fast";
    // compact mode
    if (o == "space") return "small";
    return "quality"; // compact + gpu (default)
}

QVector<SpratProfile> sanitizeProfiles(const QVector<SpratProfile>& profiles) {
    QVector<SpratProfile> cleaned;
    QStringList seen;
    for (SpratProfile p : profiles) {
        p.name = p.name.trimmed();
        p.preset = p.preset.trimmed().toLower();
        if (p.name.isEmpty() || seen.contains(p.name)) {
            continue;
        }
        if (p.preset != "fast" && p.preset != "quality" && p.preset != "small" && p.preset != "pot") {
            p.preset = "quality";
        }
        if (p.maxWidth <= 0) {
            p.maxWidth = -1;
        }
        if (p.maxHeight <= 0) {
            p.maxHeight = -1;
        }
        if (!p.targetResolutionUseSource &&
            (p.targetResolutionWidth <= 0 || p.targetResolutionHeight <= 0)) {
            p.targetResolutionWidth = 1024;
            p.targetResolutionHeight = 1024;
        }
        p.resolutionReference = p.resolutionReference.trimmed().toLower();
        if (p.resolutionReference != "largest" && p.resolutionReference != "smallest") {
            p.resolutionReference = "largest";
        }
        if (p.padding < 0) {
            p.padding = 0;
        }
        if (p.extrude < 0) {
            p.extrude = 0;
        }
        if (p.threads < 0) {
            p.threads = 0;
        }
        if (p.scale <= 0 || p.scale > 1.0) {
            p.scale = 1.0;
        }
        p.sort = p.sort.trimmed().toLower();
        if (p.sort != "name" && p.sort != "none" &&
            p.sort != "stable" && p.sort != "stable:area" &&
            p.sort != "stable:maxside" && p.sort != "stable:height" &&
            p.sort != "stable:width" && p.sort != "stable:perimeter") {
            p.sort = "none";
        }
        p.gpuCompress = p.gpuCompress.trimmed().toLower();
        if (p.gpuCompress != "" && p.gpuCompress != "dxt1" && p.gpuCompress != "dxt5") {
            p.gpuCompress = "";
        }
        if (p.dilate < 0) {
            p.dilate = 0;
        }
        p.imageFormat = p.imageFormat.trimmed().toLower();
        if (p.imageFormat != "png" && p.imageFormat != "webp" && p.imageFormat != "avif") {
            p.imageFormat = "png";
        }
        p.imageQuality = qBound(0, p.imageQuality, 100);
        if (p.frameLineWidth < 1) {
            p.frameLineWidth = 1;
        }
        if (p.atlasIndex < -1) {
            p.atlasIndex = -1;
        }
        seen.append(p.name);
        cleaned.append(p);
    }
    return cleaned;
}
}

QString SpratProfilesConfig::configPath() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(configDir).filePath("sprat/spratprofiles.cfg");
}

QString SpratProfilesConfig::findProfilesConfigPath() {
    QStringList candidates;
    candidates << configPath();

    const CliPaths cliPaths = CliToolsConfig::loadCliPaths();
    const QString defaultConfig = CliToolsConfig::queryDefaultProfilesConfig(cliPaths.layoutBinary);
    if (!defaultConfig.isEmpty()) {
        candidates << defaultConfig;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    candidates << QDir(appDir).filePath("spratprofiles.cfg");
    candidates << QDir(appDir).filePath("bin/spratprofiles.cfg");
    candidates << QDir(appDir).filePath("cli/spratprofiles.cfg");

    for (const QString& candidate : candidates) {
        if (QFile::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

QVector<SpratProfile> SpratProfilesConfig::loadProfileDefinitions(QString* errorMessage) {
    if (errorMessage) {
        errorMessage->clear();
    }

    QStringList candidates;
    candidates << configPath();

    const CliPaths cliPaths = CliToolsConfig::loadCliPaths();
    const QString defaultConfig = CliToolsConfig::queryDefaultProfilesConfig(cliPaths.layoutBinary);
    if (!defaultConfig.isEmpty()) {
        candidates << defaultConfig;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    candidates << QDir(appDir).filePath("spratprofiles.cfg");
    candidates << QDir(appDir).filePath("bin/spratprofiles.cfg");
    candidates << QDir(appDir).filePath("cli/spratprofiles.cfg");

    QString path;
    for (const QString& candidate : candidates) {
        if (QFile::exists(candidate)) {
            path = candidate;
            break;
        }
    }

    if (path.isEmpty()) {
        return {};
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not open %1: %2").arg(path, file.errorString());
        }
        return {};
    }

    QVector<SpratProfile> profiles;
    SpratProfile current;
    bool inProfile = false;
    bool inLegacyProfilesSection = false;
    bool hasConfigContent = false;

    // Temporary storage for legacy mode/optimize migration
    QString legacyMode;
    QString legacyOptimize;

    QTextStream in(&file);
    const QRegularExpression headerPattern("^\\[profile\\s+(.+)\\]$", QRegularExpression::CaseInsensitiveOption);
    while (!in.atEnd()) {
        const QString raw = in.readLine();
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';')) {
            continue;
        }
        hasConfigContent = true;

        if (line.compare("[profiles]", Qt::CaseInsensitive) == 0) {
            inLegacyProfilesSection = true;
            continue;
        }

        const QRegularExpressionMatch headerMatch = headerPattern.match(line);
        if (headerMatch.hasMatch()) {
            if (inProfile) {
                // Migrate legacy mode/optimize if no preset was set explicitly
                if (!legacyMode.isEmpty() && current.preset == "quality") {
                    current.preset = migrateModOptimizeToPreset(legacyMode, legacyOptimize);
                }
                profiles.append(current);
            }
            QString profileName = headerMatch.captured(1).trimmed();
            if (profileName.size() >= 2 && profileName.startsWith('"') && profileName.endsWith('"')) {
                profileName = profileName.mid(1, profileName.size() - 2);
            }
            current = genericDefaultProfile(profileName);
            legacyMode.clear();
            legacyOptimize.clear();
            inProfile = true;
            inLegacyProfilesSection = false;
            continue;
        }

        if (line.startsWith('[') && line.endsWith(']')) {
            inLegacyProfilesSection = false;
        }

        const int eq = line.indexOf('=');
        if (eq <= 0) {
            continue;
        }
        QString key = line.left(eq).trimmed().toLower();
        key.replace('-', '_');
        const QString value = line.mid(eq + 1).trimmed();

        if (inLegacyProfilesSection && key == "names") {
            const QStringList names = value.split(',', Qt::SkipEmptyParts);
            for (const QString& rawName : names) {
                const QString name = rawName.trimmed();
                if (!name.isEmpty()) {
                    profiles.append(genericDefaultProfile(name));
                }
            }
            continue;
        }

        if (!inProfile) {
            continue;
        }

        if (key == "label") {
            current.label = value;
        } else if (key == "preset") {
            current.preset = value;
        } else if (key == "mode") {
            legacyMode = value;
        } else if (key == "optimize") {
            legacyOptimize = value;
        } else if (key == "max_width") {
            bool ok = false;
            int n = value.toInt(&ok);
            if (ok) {
                current.maxWidth = n;
            }
        } else if (key == "max_height") {
            bool ok = false;
            int n = value.toInt(&ok);
            if (ok) {
                current.maxHeight = n;
            }
        } else if (key == "padding") {
            bool ok = false;
            int n = value.toInt(&ok);
            if (ok) {
                current.padding = n;
            }
        } else if (key == "extrude") {
            bool ok = false;
            int n = value.toInt(&ok);
            if (ok) {
                current.extrude = n;
            }
        } else if (key == "target_resolution") {
            int width = 0;
            int height = 0;
            const QString normalized = value.trimmed().toLower();
            if (normalized == "source" ||
                normalized == "same-as-source" ||
                normalized == "same_as_source") {
                current.targetResolutionUseSource = true;
            } else if (parseResolutionText(value, width, height)) {
                current.targetResolutionWidth = width;
                current.targetResolutionHeight = height;
                current.targetResolutionUseSource = false;
            }
        } else if (key == "target_width") {
            bool ok = false;
            int n = value.toInt(&ok);
            if (ok) {
                current.targetResolutionWidth = n;
                current.targetResolutionUseSource = false;
            }
        } else if (key == "target_height") {
            bool ok = false;
            int n = value.toInt(&ok);
            if (ok) {
                current.targetResolutionHeight = n;
                current.targetResolutionUseSource = false;
            }
        } else if (key == "resolution_reference") {
            current.resolutionReference = value;
        } else if (key == "max_combinations") {
            // Ignored (removed in v0.4.0+), kept for backward compat parsing
        } else if (key == "threads") {
            bool ok = false;
            int n = value.toInt(&ok);
            if (ok) {
                current.threads = n;
            }
        } else if (key == "trim_transparent") {
            current.trimTransparent = toBool(value, current.trimTransparent);
        } else if (key == "rotate") {
            current.allowRotation = toBool(value, current.allowRotation);
        } else if (key == "scale") {
            bool ok = false;
            double d = value.toDouble(&ok);
            if (ok) {
                current.scale = d;
            }
        } else if (key == "multipack") {
            current.multipack = toBool(value, current.multipack);
        } else if (key == "sort") {
            current.sort = value;
        } else if (key == "gpu_compress") {
            current.gpuCompress = value;
        } else if (key == "dilate") {
            bool ok = false;
            int n = value.toInt(&ok);
            if (ok) {
                current.dilate = n;
            }
        } else if (key == "image_format") {
            current.imageFormat = value;
        } else if (key == "image_quality") {
            bool ok = false;
            int n = value.toInt(&ok);
            if (ok) {
                current.imageQuality = n;
            }
        } else if (key == "zopfli") {
            current.zopfli = toBool(value, current.zopfli);
        } else if (key == "frame_lines") {
            current.frameLines = toBool(value, current.frameLines);
        } else if (key == "frame_line_width") {
            bool ok = false;
            int n = value.toInt(&ok);
            if (ok) {
                current.frameLineWidth = n;
            }
        } else if (key == "frame_line_color") {
            current.frameLineColor = value;
        } else if (key == "atlas_index") {
            bool ok = false;
            int n = value.toInt(&ok);
            if (ok) {
                current.atlasIndex = n;
            }
        } else if (key == "auto_animations") {
            current.autoAnimations = toBool(value, current.autoAnimations);
        }
    }

    if (inProfile) {
        if (!legacyMode.isEmpty() && current.preset == "quality") {
            current.preset = migrateModOptimizeToPreset(legacyMode, legacyOptimize);
        }
        profiles.append(current);
    }

    const QVector<SpratProfile> cleaned = sanitizeProfiles(profiles);
    if (!cleaned.isEmpty()) {
        if (path != configPath()) {
            saveProfileDefinitions(cleaned);
        }
        return cleaned;
    }
    if (hasConfigContent && errorMessage) {
        *errorMessage = QStringLiteral("Invalid profiles configuration in %1.").arg(path);
    }
    return {};
}

#ifdef Q_OS_WASM
#include <emscripten.h>
extern "C" { void sync_idbfs(); }
#endif

bool SpratProfilesConfig::saveProfileDefinitions(const QVector<SpratProfile>& profiles) {
    const QVector<SpratProfile> cleaned = sanitizeProfiles(profiles);

    const QString path = configPath();
    QDir().mkpath(QFileInfo(path).path());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    QTextStream out(&file);
    out << "# Default spratlayout profiles.\n";
    for (int i = 0; i < cleaned.size(); ++i) {
        const SpratProfile& p = cleaned[i];
        if (i > 0) {
            out << "\n";
        }
        QString profileName = p.name;
        if (profileName.contains(' ') || profileName.contains('[') || profileName.contains(']')) {
            profileName = "\"" + profileName + "\"";
        }
        out << "[profile " << profileName << "]\n";
        if (!p.label.trimmed().isEmpty()) {
            out << "label=" << p.label.trimmed() << "\n";
        }
        out << "preset=" << p.preset << "\n";
        if (p.maxWidth > 0) {
            out << "max_width=" << p.maxWidth << "\n";
        }
        if (p.maxHeight > 0) {
            out << "max_height=" << p.maxHeight << "\n";
        }
        if (p.targetResolutionUseSource) {
            out << "target_resolution=source\n";
        } else {
            out << "target_resolution=" << p.targetResolutionWidth << "x" << p.targetResolutionHeight << "\n";
        }
        out << "resolution_reference=" << p.resolutionReference << "\n";
        out << "padding=" << p.padding << "\n";
        out << "extrude=" << p.extrude << "\n";
        if (p.threads > 0) {
            out << "threads=" << p.threads << "\n";
        }
        out << "trim_transparent=" << (p.trimTransparent ? "true" : "false") << "\n";
        out << "rotate=" << (p.allowRotation ? "true" : "false") << "\n";
        out << "scale=" << p.scale << "\n";
        out << "multipack=" << (p.multipack ? "true" : "false") << "\n";
        out << "sort=" << p.sort << "\n";
        if (!p.gpuCompress.isEmpty()) {
            out << "gpu_compress=" << p.gpuCompress << "\n";
        }
        if (p.dilate > 0) {
            out << "dilate=" << p.dilate << "\n";
        }
        if (p.imageFormat != "png") {
            out << "image_format=" << p.imageFormat << "\n";
        }
        if (p.imageQuality != 100) {
            out << "image_quality=" << p.imageQuality << "\n";
        }
        if (p.zopfli) {
            out << "zopfli=true\n";
        }
        if (p.frameLines) {
            out << "frame_lines=true\n";
            if (p.frameLineWidth > 1) {
                out << "frame_line_width=" << p.frameLineWidth << "\n";
            }
            if (!p.frameLineColor.isEmpty() && p.frameLineColor != "255,0,0,255") {
                out << "frame_line_color=" << p.frameLineColor << "\n";
            }
        }
        if (p.atlasIndex >= 0) {
            out << "atlas_index=" << p.atlasIndex << "\n";
        }
        if (p.autoAnimations) {
            out << "auto_animations=true\n";
        }
    }

    out.flush();
    file.close();

#ifdef Q_OS_WASM
    sync_idbfs();
#endif
    return out.status() == QTextStream::Ok;
}

QStringList SpratProfilesConfig::loadProfiles() {
    QStringList names;
    const QVector<SpratProfile> profiles = loadProfileDefinitions();
    names.reserve(profiles.size());
    for (const SpratProfile& profile : profiles) {
        names.append(profile.name);
    }
    return names;
}
