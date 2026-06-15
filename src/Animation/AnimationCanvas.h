#pragma once

#include "ZoomableGraphicsView.h"
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include "EditorOverlayItem.h"
#include "models.h"

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

    void setSettings(const AppSettings& settings);

    /**
     * @brief Sets the sprite shown in the overlay (pivot + markers).
     */
    void setOverlaySprite(SpritePtr sprite);

    /**
     * @brief Shows or hides the pivot/marker overlay.
     */
    void setOverlayVisible(bool visible);

    /**
     * @brief Enables or disables interactive editing on the overlay.
     * Should be called with false during animation playback.
     */
    void setOverlayEditable(bool editable);

    EditorOverlayItem* overlay() const { return m_overlay; }
    SpritePtr overlaySprite() const { return m_overlaySprite; }

private:
    QGraphicsScene*      m_scene;
    QGraphicsPixmapItem* m_pixmapItem;
    EditorOverlayItem*   m_overlay = nullptr;
    SpritePtr            m_overlaySprite;
    AppSettings          m_settings;
};
