#include "LayoutParser.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>

LayoutModel LayoutParser::parse(const QString& output, const QString& folderPath) {
    LayoutModel model;
    QDir dir(folderPath);
    QRegularExpression spriteRe("sprite\\s+\"([^\"]+)\"\\s+(\\d+),(\\d+)\\s+(\\d+),(\\d+)(?:\\s+(\\d+),(\\d+)\\s+(\\d+),(\\d+))?");

    QStringList lines = output.split('\n');
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith("atlas ")) {
            QString dims = trimmed.mid(6);
            QStringList parts = dims.split(',');
            if (parts.size() == 2) {
                model.atlasWidth = parts[0].toInt();
                model.atlasHeight = parts[1].toInt();
            }
        } else if (trimmed.startsWith("scale ")) {
            model.scale = trimmed.mid(6).toDouble();
        } else {
            QRegularExpressionMatch match = spriteRe.match(trimmed);
            if (!match.hasMatch()) {
                continue;
            }
            auto s = std::make_shared<Sprite>();
            s->path = dir.absoluteFilePath(match.captured(1));
            s->name = QFileInfo(s->path).baseName();
            s->rect = QRect(match.captured(2).toInt(), match.captured(3).toInt(), match.captured(4).toInt(), match.captured(5).toInt());
            if (!match.captured(6).isEmpty()) {
                s->trimmed = true;
                s->trimRect = QRect(match.captured(6).toInt(), match.captured(7).toInt(), match.captured(8).toInt(), match.captured(9).toInt());
            }
            // Use the original image dimensions so the pivot aligns with the visual frame center.
            const QSize sourceSize = QImageReader(s->path).size();
            if (sourceSize.isValid() && sourceSize.width() > 0 && sourceSize.height() > 0) {
                s->pivotX = sourceSize.width() / 2;
                s->pivotY = sourceSize.height() / 2;
            } else if (s->trimmed) {
                const int sourceWidth = s->trimRect.x() + s->rect.width() + s->trimRect.width();
                const int sourceHeight = s->trimRect.y() + s->rect.height() + s->trimRect.height();
                s->pivotX = sourceWidth / 2;
                s->pivotY = sourceHeight / 2;
            } else {
                s->pivotX = s->rect.width() / 2;
                s->pivotY = s->rect.height() / 2;
            }
            model.sprites.append(s);
        }
    }
    return model;
}
