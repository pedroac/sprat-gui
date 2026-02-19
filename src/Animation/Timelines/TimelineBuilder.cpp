#include "TimelineBuilder.h"

#include <QMap>
#include <QPair>
#include <QRegularExpression>
#include <algorithm>

QVector<TimelineSeed> TimelineBuilder::buildFromSprites(const QVector<SpritePtr>& sprites) {
    QRegularExpression pattern(R"(^(.*)\s*\((\d+)\)$)");
    QMap<QString, QVector<QPair<int, QString>>> groups;

    for (const auto& sprite : sprites) {
        QRegularExpressionMatch match = pattern.match(sprite->name);
        if (!match.hasMatch()) {
            continue;
        }

        QString label = match.captured(1).trimmed();
        if (label.isEmpty()) {
            continue;
        }

        bool ok = false;
        int index = match.captured(2).toInt(&ok);
        if (!ok) {
            continue;
        }

        groups[label].append({index, sprite->path});
    }

    QVector<TimelineSeed> result;
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        auto entries = it.value();
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

        TimelineSeed seed;
        seed.name = it.key();
        for (const auto& entry : entries) {
            seed.frames.append(entry.second);
        }
        result.append(seed);
    }

    return result;
}
