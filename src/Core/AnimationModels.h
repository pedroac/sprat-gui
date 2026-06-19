#pragma once
#include <QString>
#include <QStringList>

/**
 * @struct AnimationTimeline
 * @brief Represents an animation sequence.
 */
struct AnimationTimeline {
    QString name;
    int fps = 8;
    QStringList frames;
    QString aliasOf;
    bool hFlip = false;
    bool vFlip = false;
};
