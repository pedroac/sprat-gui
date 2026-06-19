#pragma once
#include <QVector>
#include "SpriteModels.h"

/**
 * @struct LayoutModel
 * @brief Represents the layout of sprites in an atlas.
 */
struct LayoutModel {
    int atlasWidth = 0;
    int atlasHeight = 0;
    double scale = 1.0;
    QVector<SpritePtr> sprites;
};
