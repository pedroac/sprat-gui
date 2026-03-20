#pragma once

#include <QPixmap>
#include <QPainter>

#ifdef Q_OS_WASM
extern "C" bool jsIsAsyncBusy();
extern "C" bool jsHaveAsyncify();
extern "C" bool jsHaveJspi();
#endif

/**
 * @brief Creates a checkerboard pixmap for visualizing transparency.
 */
inline QPixmap createCheckerboardPixmap(const QColor& baseColor = Qt::white, int size = 8) {
    QPixmap pixmap(size * 2, size * 2);
    pixmap.fill(baseColor);
    QPainter painter(&pixmap);
    QColor alternateColor = baseColor.lightness() > 128 ? baseColor.darker(115) : baseColor.lighter(115);
    painter.fillRect(0, 0, size, size, alternateColor);
    painter.fillRect(size, size, size, size, alternateColor);
    return pixmap;
}
