#include "ResolutionsConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

namespace {
constexpr int kMinResolutionAxis = 16;
constexpr int kMaxResolutionAxis = 16384;

QStringList defaultResolutions() {
    return {
        "320x240",
        "640x360",
        "640x480",
        "800x600",
        "1024x768",
        "1280x720",
        "1366x768",
        "1600x900",
        "1920x1080",
        "2048x2048",
        "3840x2160"
    };
}

bool isValidResolution(const QString& value) {
    static const QRegularExpression pattern("^\\d+x\\d+$");
    const QString normalized = value.trimmed().toLower();
    if (!pattern.match(normalized).hasMatch()) {
        return false;
    }
    const QStringList parts = normalized.split('x', Qt::SkipEmptyParts);
    if (parts.size() != 2) {
        return false;
    }
    bool okW = false;
    bool okH = false;
    const int width = parts[0].toInt(&okW);
    const int height = parts[1].toInt(&okH);
    if (!okW || !okH) {
        return false;
    }
    return width >= kMinResolutionAxis &&
           width <= kMaxResolutionAxis &&
           height >= kMinResolutionAxis &&
           height <= kMaxResolutionAxis;
}

QStringList sanitizeResolutions(const QStringList& values) {
    QStringList cleaned;
    for (const QString& raw : values) {
        const QString normalized = raw.trimmed().toLower();
        if (!isValidResolution(normalized) || cleaned.contains(normalized)) {
            continue;
        }
        cleaned.append(normalized);
    }
    if (cleaned.isEmpty()) {
        return defaultResolutions();
    }
    return cleaned;
}

bool writeResolutionsFile(const QString& path, const QStringList& values) {
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    QTextStream out(&file);
    out << "# Sprat GUI source/target resolution presets (WxH)\n";
    for (const QString& resolution : values) {
        out << resolution << "\n";
    }
    out.flush();
    return out.status() == QTextStream::Ok;
}
}

QString ResolutionsConfig::configPath() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(configDir).filePath("sprat/resolutions.cfg");
}

QStringList ResolutionsConfig::loadResolutionOptions() {
    const QString path = configPath();
    QFile file(path);
    if (!file.exists()) {
        const QStringList defaults = defaultResolutions();
        writeResolutionsFile(path, defaults);
        return defaults;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return defaultResolutions();
    }

    QStringList fromFile;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';')) {
            continue;
        }
        fromFile.append(line);
    }
    const QStringList cleaned = sanitizeResolutions(fromFile);
    if (cleaned != fromFile) {
        writeResolutionsFile(path, cleaned);
    }
    return cleaned;
}
