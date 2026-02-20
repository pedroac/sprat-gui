#pragma once
#include <QGraphicsView>
#include "models.h"
#include "EditorOverlayItem.h"

class QFocusEvent;

/**
 * @brief Widget for previewing and editing a single sprite.
 * 
 * Displays the sprite, pivot point, and markers. Handles zooming and panning.
 */
class PreviewCanvas : public QGraphicsView {
    Q_OBJECT
public:
    /**
     * @brief Constructs the PreviewCanvas.
     * @param parent The parent widget.
     */
    explicit PreviewCanvas(QWidget* parent = nullptr);
    /**
     * @brief Sets the sprites to display (usually just one).
     * @param sprites List of sprites.
     */
    void setSprites(const QList<SpritePtr>& sprites);
    /**
     * @brief Sets the zoom level.
     * @param zoom The zoom factor.
     */
    void setZoom(double zoom);
    /**
     * @brief Centers the content in the view.
     */
    void centerContent();
    /**
     * @brief Updates visual settings.
     * @param settings The application settings.
     */
    void setSettings(const AppSettings& settings);
    EditorOverlayItem* overlay() const { return m_overlay; }
    
signals:
    /**
     * @brief Emitted when the pivot position changes.
     */
    void pivotChanged(int x, int y);
    /**
     * @brief Emitted when the zoom level changes via mouse wheel.
     */
    void zoomChanged(double zoom);
    void applyPivotToSelectedFramesRequested();
    void applyMarkerToSelectedFramesRequested(const QString& markerName);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    QGraphicsScene* m_scene;
    QList<QGraphicsPixmapItem*> m_imageItems;
    QList<QGraphicsRectItem*> m_borderItems;
    EditorOverlayItem* m_overlay;
    QList<SpritePtr> m_sprites;

    bool m_isPanning = false;
    QPoint m_lastMousePos;
    bool m_spacePressed = false;
    AppSettings m_settings;
};
