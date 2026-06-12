#pragma once
#include "ZoomableGraphicsView.h"
#include "models.h"
#include "EditorOverlayItem.h"
#include <QDateTime>
#include <QRect>

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

    /**
     * @brief Scrolls the view so @p pivot (in scene/image coordinates) appears
     * at @p screenPos (in viewport coordinates).
     *
     * Used for flipbook mode: keeps the pivot visually stationary when
     * navigating between frames.
     */
    void alignPivotToScreenPos(QPoint pivot, QPoint screenPos);

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

    /**
     * @brief Sets ghost (onion skin) sprites rendered as semi-transparent overlays.
     *
     * Each ghost is positioned so that its own pivot aligns with @p activePivot
     * in scene coordinates (i.e. the active sprite's pivot position).
     */
    void setGhostSprites(const QList<SpritePtr>& ghosts, QPoint activePivot = QPoint());

    EditorOverlayItem* overlay() const { return m_overlay; }

    /**
     * @brief Returns the cached trimmed-content bounding rect for the current sprite.
     *
     * The rect is in image (scene) coordinates. Returns an invalid QRect if no
     * sprite is loaded or the image is fully transparent.
     * The cache is refreshed automatically when the sprite path or file timestamp changes.
     */
    QRect cachedTrimRect();
    
signals:
    /**
     * @brief Emitted when the pivot position changes.
     */
    void pivotChanged(int x, int y);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    static QRect computeTrimRect(const QImage& img);
    void updateTrimRectItem();

    QGraphicsScene* m_scene;
    QList<QGraphicsPixmapItem*> m_imageItems;
    QList<QGraphicsRectItem*> m_borderItems;
    QList<QGraphicsPixmapItem*> m_ghostItems;
    QGraphicsRectItem* m_trimRectItem = nullptr;
    EditorOverlayItem* m_overlay;
    QList<SpritePtr> m_sprites;
    AppSettings m_settings;

    struct TrimCache {
        QString path;
        QDateTime timestamp;
        QRect rect;
    };
    TrimCache m_trimCache;
};
