#pragma once

#include "ZoomableGraphicsView.h"
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>

/**
 * @class AnimationCanvas
 * @brief Widget for previewing animations.
 */
class AnimationCanvas : public ZoomableGraphicsView {
    Q_OBJECT
public:
    explicit AnimationCanvas(QWidget* parent = nullptr);

    /**
     * @brief Sets the pixmap to display.
     */
    void setPixmap(const QPixmap& pixmap);

    /**
     * @brief Centers the content in the view.
     */
    void centerContent();

private:
    QGraphicsScene* m_scene;
    QGraphicsPixmapItem* m_pixmapItem;
};
