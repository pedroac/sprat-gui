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
    bool isNineSliced = false;
    int  nsLeft   = 0;
    int  nsTop    = 0;
    int  nsRight  = 0;
    int  nsBottom = 0;
    QString nsHMode = "stretch"; // stretch | repeat | mirror
    QString nsVMode = "stretch";
    int nsTargetWidth  = 0;  // 0 = use natural image size
    int nsTargetHeight = 0;
};

/**
 * @typedef SpritePtr
 * @brief Shared pointer to Sprite struct.
 */
using SpritePtr = std::shared_ptr<Sprite>;

Q_DECLARE_METATYPE(SpritePtr)
