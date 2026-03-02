#pragma once
#include "ZoomableGraphicsView.h"
#include "models.h"
#include "EditorOverlayItem.h"

/**
 * @brief Widget for previewing and editing a single sprite.
 * 
 * Displays the sprite, pivot point, and markers. Handles zooming and panning.
 */
class PreviewCanvas : public ZoomableGraphicsView {
    Q_OBJECT
public:
    explicit PreviewCanvas(QWidget* parent = nullptr);

    /**
     * @brief Sets the sprites to display (usually just one).
     */
    void setSprites(const QList<SpritePtr>& sprites);

    /**
     * @brief Centers the content in the view.
     */
    void centerContent();

    void setZoom(double zoom) override;
    void initialFit() override;

    /**
     * @brief Gets the center of the viewport in scene coordinates.
     */
    QPointF viewportCenterInScene() const;

    /**
     * @brief Updates visual settings.
     */
    void setSettings(const AppSettings& settings);

    EditorOverlayItem* overlay() const { return m_overlay; }
    
signals:
    /**
     * @brief Emitted when the pivot position changes.
     */
    void pivotChanged(int x, int y);
    void applyPivotToSelectedFramesRequested();
    void applyMarkerToSelectedFramesRequested(const QString& markerName);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    QGraphicsScene* m_scene;
    QList<QGraphicsPixmapItem*> m_imageItems;
    QList<QGraphicsRectItem*> m_borderItems;
    EditorOverlayItem* m_overlay;
    QList<SpritePtr> m_sprites;
    AppSettings m_settings;
};
