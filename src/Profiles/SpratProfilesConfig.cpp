#include "SpratProfilesConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

namespace {
SpratProfile makeProfile(const QString& name,
                         const QString& mode,
                         const QString& optimize,
                         int maxWidth,
                         int maxHeight,
                         int targetResolutionWidth,
                         int targetResolutionHeight,
                         bool targetResolutionUseSource,
                         const QString& resolutionReference,
                         int padding,
                         int maxCombinations,
                         int threads,
                         bool trimTransparent) {
    SpratProfile p;
    p.name = name;
    p.mode = mode;
    p.optimize = optimize;
    p.maxWidth = maxWidth;
    p.maxHeight = maxHeight;
    p.targetResolutionWidth = targetResolutionWidth;
    p.targetResolutionHeight = targetResolutionHeight;
    p.targetResolutionUseSource = targetResolutionUseSource;
    p.resolutionReference = resolutionReference;
    p.padding = padding;
    p.maxCombinations = maxCombinations;
    p.threads = threads;
    p.trimTransparent = trimTransparent;
    return p;
}

SpratProfile genericDefaultProfile(const QString& name = QString()) {
    return makeProfile(name, "compact", "gpu", -1, -1, 1024, 1024, false, "largest", 0, 0, 0, true);
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

bool parseResolution(const QString& value, int& width, int& height) {
    const QString normalized = value.trimmed().toLower();
    const QStringList parts = normalized.split('x', Qt::SkipEmptyParts);
    if (parts.size() != 2) {
        return false;
    }
    bool okW = false;
    bool okH = false;
    const int parsedWidth = parts[0].trimmed().toInt(&okW);
    const int parsedHeight = parts[1].trimmed().toInt(&okH);
    if (!okW || !okH || parsedWidth <= 0 || parsedHeight <= 0) {
        return false;
    }
    width = parsedWidth;
    height = parsedHeight;
    return true;
}

QVector<SpratProfile> sanitizeProfiles(const QVector<SpratProfile>& profiles) {
    QVector<SpratProfile> cleaned;
    QStringList seen;
    for (SpratProfile p : profiles) {
        p.name = p.name.trimmed();
        p.mode = p.mode.trimmed();
        p.optimize = p.optimize.trimmed();
        if (p.name.isEmpty() || seen.contains(p.name)) {
            continue;
        }
        if (p.mode.isEmpty()) {
            p.mode = "compact";
        }
        if (p.optimize.isEmpty()) {
            p.optimize = "gpu";
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
        if (p.maxCombinations < 0) {
            p.maxCombinations = 0;
        }
        if (p.threads < 1) {
            p.threads = 0;
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

QVector<SpratProfile> SpratProfilesConfig::loadProfileDefinitions(QString* errorMessage) {
    if (errorMessage) {
        errorMessage->clear();
    }
    const QString path = configPath();
    QFile file(path);
    if (!file.exists()) {
        return {};
    }
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
                profiles.append(current);
            }
            current = genericDefaultProfile(headerMatch.captured(1).trimmed());
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
        const QString key = line.left(eq).trimmed().toLower();
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

        if (key == "mode") {
            current.mode = value;
        } else if (key == "optimize") {
            current.optimize = value;
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
        } else if (key == "target_resolution") {
            int width = 0;
            int height = 0;
            const QString normalized = value.trimmed().toLower();
            if (normalized == "source" ||
                normalized == "same-as-source" ||
                normalized == "same_as_source") {
                current.targetResolutionUseSource = true;
            } else if (parseResolution(value, width, height)) {
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
            bool ok = false;
            int n = value.toInt(&ok);
            if (ok) {
                current.maxCombinations = n;
            }
        } else if (key == "threads") {
            bool ok = false;
            int n = value.toInt(&ok);
            if (ok) {
                current.threads = n;
            }
        } else if (key == "trim_transparent") {
            current.trimTransparent = toBool(value, current.trimTransparent);
        }
    }

    if (inProfile) {
        profiles.append(current);
    }

    const QVector<SpratProfile> cleaned = sanitizeProfiles(profiles);
    if (!cleaned.isEmpty()) {
        saveProfileDefinitions(cleaned);
        return cleaned;
    }
    if (hasConfigContent && errorMessage) {
        *errorMessage = QStringLiteral("Invalid profiles configuration in %1.").arg(path);
    }
    return {};
}

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
        out << "[profile " << p.name << "]\n";
        out << "mode=" << p.mode << "\n";
        out << "optimize=" << p.optimize << "\n";
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
        out << "max_combinations=" << p.maxCombinations << "\n";
        if (p.threads > 0) {
            out << "threads=" << p.threads << "\n";
        }
        out << "trim_transparent=" << (p.trimTransparent ? "true" : "false") << "\n";
    }

    out.flush();
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
