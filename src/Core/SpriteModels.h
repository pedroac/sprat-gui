#pragma once
#include <QString>
#include <QStringList>
#include <QRect>
#include <QVector>
#include <memory>
#include <QMetaType>
#include "MarkerModels.h"

/**
 * @struct Sprite
 * @brief Represents a sprite image and its metadata.
 */
struct Sprite {
    QString path;
    QString name;
    QStringList aliases;
    QRect rect;
    bool trimmed = false;
    bool rotated = false;
    QRect trimRect;
    int pivotX = 0;
    int pivotY = 0;
    QVector<NamedPoint> points;
};

/**
 * @typedef SpritePtr
 * @brief Shared pointer to Sprite struct.
 */
using SpritePtr = std::shared_ptr<Sprite>;

Q_DECLARE_METATYPE(SpritePtr)
