#pragma once

#include <QVector>
#include <QStringList>
#include "SpriteModels.h"

struct TimelineSeed {
    QString name;
    QStringList frames;
};

class TimelineBuilder {
public:
    static QVector<TimelineSeed> buildFromSprites(const QVector<SpritePtr>& sprites);

    /**
     * @brief Returns the animation group label for @p name, or an empty string
     * if the name does not match any recognised pattern.
     *
     * Examples: "walk1" → "walk", "run_3" → "run", "idle" → ""
     */
    static QString groupLabelFor(const QString& name);
};
