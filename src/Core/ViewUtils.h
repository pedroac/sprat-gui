#pragma once

#include <QPixmap>
#include <QPainter>

/**
 * @brief Creates a checkerboard pixmap for visualizing transparency.
 * @param baseColor The base color to tint the checkerboard with.
 * @param size The size of each square in the checkerboard.
 * @return A QPixmap containing the checkerboard pattern.
 */
inline QPixmap createCheckerboardPixmap(const QColor& baseColor = Qt::white, int size = 8) {
    QPixmap pixmap(size * 2, size * 2);
    pixmap.fill(baseColor);
    QPainter painter(&pixmap);
    
    // Create an alternate color for the checkers by slightly darkening or lightening the base color
    QColor alternateColor = baseColor;
    if (baseColor.lightness() > 128) {
        alternateColor = baseColor.darker(115);
    } else {
        alternateColor = baseColor.lighter(115);
    }
    
    painter.fillRect(0, 0, size, size, alternateColor);
    painter.fillRect(size, size, size, size, alternateColor);
    return pixmap;
}
