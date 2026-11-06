#include "LayoutParser.h"
#include "SpriteNameUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QHash>

namespace {
// Reads "KEY:A,B,C,D" (the token after "KEY:") into four integers.
bool parseFourInts(const QString& value, int out[4]) {
    const QStringList parts = value.split(',');
    if (parts.size() != 4) return false;
    for (int i = 0; i < 4; ++i) {
        bool ok = false;
        out[i] = parts[i].toInt(&ok);
        if (!ok) return false;
    }
    return true;
}
} // namespace

QVector<LayoutModel> LayoutParser::parse(const QString& output, const QString& folderPath,
                                          const QString& sourceFolder) {
    QVector<LayoutModel> models;
    double commonScale = 1.0;
    QString rootPath;
    QDir dir(folderPath);
    static QHash<QString, QSize> sourceSizeCache;
    if (sourceSizeCache.size() > 16384) {
        sourceSizeCache.clear();
    }
    // sprite "PATH" rect:X,Y,W,H [trim:L,T,R,B] [rotated] ... (options in any order)
    QRegularExpression spriteRe(R"raw(^sprite\s+"((?:[^"\\]|\\.)*)"(.*)$)raw");
    QRegularExpression rootRe(R"raw(root\s+"((?:[^"\\]|\\.)*)")raw");

    QStringList lines = output.split('\n');
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith("root ")) {
            QRegularExpressionMatch rootMatch = rootRe.match(trimmed);
            if (rootMatch.hasMatch()) {
                rootPath = rootMatch.captured(1);
                rootPath.replace("\\\"", "\"");
                rootPath.replace("\\\\", "\\");
                // Resolve relative root path against folderPath
                if (QDir::isRelativePath(rootPath)) {
                    rootPath = QDir(folderPath).absoluteFilePath(rootPath);
                }
                // A relative root leaves "a/../../b" segments, and paths are compared as strings.
                rootPath = QDir::cleanPath(rootPath);
                dir = QDir(rootPath);
            }
            continue;
        }
        if (trimmed.startsWith("atlas ")) {
            LayoutModel model;
            model.scale = commonScale;
            QString dims = trimmed.mid(6);
            QStringList parts = dims.split(',');
            if (parts.size() == 2) {
                model.atlasWidth = parts[0].toInt();
                model.atlasHeight = parts[1].toInt();
            }
            models.append(model);
        } else if (trimmed.startsWith("scale ")) {
            commonScale = trimmed.mid(6).toDouble();
            for (auto& model : models) {
                model.scale = commonScale;
            }
        } else {
            if (models.isEmpty()) {
                continue;
            }
            LayoutModel& model = models.last();
            QRegularExpressionMatch match = spriteRe.match(trimmed);
            if (!match.hasMatch()) {
                continue;
            }
            int rect[4] = {0, 0, 0, 0};
            int trim[4] = {0, 0, 0, 0};
            bool hasRect = false;
            bool hasTrim = false;
            bool rotated = false;
            for (const QString& token : match.captured(2).split(' ', Qt::SkipEmptyParts)) {
                if (token.startsWith(QLatin1String("rect:"))) {
                    hasRect = parseFourInts(token.mid(5), rect);
                } else if (token.startsWith(QLatin1String("trim:"))) {
                    hasTrim = parseFourInts(token.mid(5), trim);
                } else if (token == QLatin1String("rotated")) {
                    rotated = true;
                }
            }
            if (!hasRect) {
                continue;
            }
            auto s = std::make_shared<Sprite>();
            QString capturedPath = match.captured(1);
            capturedPath.replace("\\\"", "\"");
            capturedPath.replace("\\\\", "\\");
            s->path = QDir::cleanPath(dir.absoluteFilePath(capturedPath));
            // Derive name from relative path within sourceFolder when available
            if (!sourceFolder.isEmpty()) {
                QString rel = QDir(sourceFolder).relativeFilePath(s->path);
                QFileInfo relInfo(rel);
                if (!rel.startsWith("..")) {
                    s->name = (relInfo.path() == ".")
                        ? relInfo.baseName()
                        : relInfo.path() + "/" + relInfo.baseName();
                } else {
                    s->name = relInfo.baseName();
                }
            } else {
                s->name = QFileInfo(s->path).baseName();
            }
            s->rect = QRect(rect[0], rect[1], rect[2], rect[3]);
            if (hasTrim) {
                // trimRect holds the left, top, right and bottom margins.
                s->trimmed = true;
                s->trimRect = QRect(trim[0], trim[1], trim[2], trim[3]);
            }
            s->rotated = rotated;
            // Use the original image dimensions so the pivot aligns with the visual frame center.
            // For rotated sprites the atlas rect has width and height swapped relative to the
            // source image, so derive content dimensions in source-image space before centering.
#ifdef Q_OS_WASM
            // Avoid QImageReader size calls in WASM (slow/async). Use trim/rect fallback.
            {
                const int contentW = s->rotated ? s->rect.height() : s->rect.width();
                const int contentH = s->rotated ? s->rect.width()  : s->rect.height();
                if (s->trimmed) {
                    s->pivotX = (s->trimRect.x() + contentW + s->trimRect.width())  / 2;
                    s->pivotY = (s->trimRect.y() + contentH + s->trimRect.height()) / 2;
                } else {
                    s->pivotX = contentW / 2;
                    s->pivotY = contentH / 2;
                }
            }
#else
            QSize sourceSize = sourceSizeCache.value(s->path);
            if (!sourceSize.isValid()) {
                sourceSize = QImageReader(s->path).size();
                if (sourceSize.isValid()) {
                    sourceSizeCache.insert(s->path, sourceSize);
                }
            }
            if (sourceSize.isValid() && sourceSize.width() > 0 && sourceSize.height() > 0) {
                s->pivotX = sourceSize.width() / 2;
                s->pivotY = sourceSize.height() / 2;
            } else {
                const int contentW = s->rotated ? s->rect.height() : s->rect.width();
                const int contentH = s->rotated ? s->rect.width()  : s->rect.height();
                if (s->trimmed) {
                    s->pivotX = (s->trimRect.x() + contentW + s->trimRect.width())  / 2;
                    s->pivotY = (s->trimRect.y() + contentH + s->trimRect.height()) / 2;
                } else {
                    s->pivotX = contentW / 2;
                    s->pivotY = contentH / 2;
                }
            }
#endif
            model.sprites.append(s);
        }
    }
    ensureUniqueSpriteNames(models, folderPath);
    return models;
}
