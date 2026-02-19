#pragma once

#include <QVector>
#include <QStringList>
#include "models.h"

struct TimelineSeed {
    QString name;
    QStringList frames;
};

class TimelineBuilder {
public:
    static QVector<TimelineSeed> buildFromSprites(const QVector<SpritePtr>& sprites);
};
