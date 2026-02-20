#include "TimelineBuilder.h"

#include <QMap>
#include <QPair>
#include <QRegularExpression>
#include <algorithm>

namespace {
const QRegularExpression kBracketedSuffixPattern(R"(^(.*\D)\s*[\(\[]\s*(\d+)\s*[\)\]]$)");
const QRegularExpression kSeparatedSuffixPattern(R"(^(.*\D)[ _-]+(\d+)$)");
const QRegularExpression kCompactSuffixPattern(R"(^(.*\D)(\d+)$)");
const QRegularExpression kTrailingSeparatorPattern(R"([ _-]+$)");

bool parseTimelineSeedName(const QString& spriteName, QString& label, int& index) {
    const QRegularExpression patterns[] = {
        kBracketedSuffixPattern,
        kSeparatedSuffixPattern,
        kCompactSuffixPattern
    };

    for (const QRegularExpression& pattern : patterns) {
        const QRegularExpressionMatch match = pattern.match(spriteName);
        if (!match.hasMatch()) {
            continue;
        }

        QString candidateLabel = match.captured(1).trimmed();
        candidateLabel.remove(kTrailingSeparatorPattern);
        if (candidateLabel.isEmpty()) {
            continue;
        }

        bool ok = false;
        const int candidateIndex = match.captured(2).toInt(&ok);
        if (!ok) {
            continue;
        }

        label = candidateLabel;
        index = candidateIndex;
        return true;
    }

    return false;
}
}

QVector<TimelineSeed> TimelineBuilder::buildFromSprites(const QVector<SpritePtr>& sprites) {
    QMap<QString, QVector<QPair<int, QString>>> groups;

    for (const auto& sprite : sprites) {
        QString label;
        int index = 0;
        if (!parseTimelineSeedName(sprite->name, label, index)) {
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
