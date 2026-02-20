#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QHash>
#include <QStringList>
#include "models.h"
#include "SpriteItem.h"

class QFocusEvent;

/**
 * @class LayoutCanvas
 * @brief Custom QGraphicsView for displaying and managing sprite layouts.
 * 
 * This class provides a visual interface for managing sprite sheets and layouts.
 * It handles sprite display, selection, zooming, panning, and drag-and-drop operations.
 */
class LayoutCanvas : public QGraphicsView {
    Q_OBJECT
public:
    /**
     * @brief Constructor for LayoutCanvas.
     * 
     * @param parent Parent widget (optional)
     */
    explicit LayoutCanvas(QWidget* parent = nullptr);

    /**
     * @brief Sets the layout model to display.
     * 
     * This method updates the canvas with the provided layout model,
     * clearing any existing sprites and displaying the new ones.
     * 
     * @param model Layout model containing sprite information
     */
    void setModel(const LayoutModel& model);

    /**
     * @brief Clears all sprites from the canvas.
     * 
     * This method removes all sprites and resets the canvas to its initial state.
     */
    void clearCanvas();

    /**
     * @brief Sets the zoom level of the canvas.
     * 
     * @param zoom Zoom factor (1.0 = 100%, 2.0 = 200%, etc.)
     */
    void setZoom(double zoom);

    /**
     * @brief Selects a sprite by its file path.
     * 
     * @param path File path of the sprite to select
     */
    void selectSpriteByPath(const QString& path);

    /**
     * @brief Selects multiple sprites by their file paths.
     * 
     * @param paths List of file paths to select
     * @param primaryPath Primary path to focus on (optional)
     */
    void selectSpritesByPaths(const QStringList& paths, const QString& primaryPath = QString());

    /**
     * @brief Applies visual settings to the canvas.
     * 
     * @param settings Application settings to apply
     */
    void setSettings(const AppSettings& settings);

signals:
    /**
     * @brief Emitted when the sprite selection changes.
     * 
     * @param selection List of currently selected sprites
     */
    void selectionChanged(const QList<SpritePtr>& selection);

    /**
     * @brief Emitted when the zoom level changes.
     * 
     * @param zoom New zoom level
     */
    void zoomChanged(double zoom);

    /**
     * @brief Emitted when a sprite is selected.
     * 
     * @param sprite Selected sprite
     */
    void spriteSelected(SpritePtr sprite);

    /**
     * @brief Emitted when timeline generation is requested.
     */
    void requestTimelineGeneration();

    /**
     * @brief Emitted when an external path is dropped onto the canvas.
     * 
     * @param path Dropped path
     */
    void externalPathDropped(const QString& path);

    /**
     * @brief Emitted when frames should be added to the layout.
     */
    void addFramesRequested();

    /**
     * @brief Emitted when frames should be removed from the layout.
     * 
     * @param paths List of paths to remove
     */
    void removeFramesRequested(const QStringList& paths);

protected:
    // === Event Handling ===
    /**
     * @brief Handles drag enter events.
     * 
     * @param event Drag enter event
     */
    void dragEnterEvent(QDragEnterEvent* event) override;

    /**
     * @brief Handles drop events.
     * 
     * @param event Drop event
     */
    void dropEvent(QDropEvent* event) override;

    /**
     * @brief Handles wheel events for zooming.
     * 
     * @param event Wheel event
     */
    void wheelEvent(QWheelEvent* event) override;

    /**
     * @brief Handles mouse press events.
     * 
     * @param event Mouse press event
     */
    void mousePressEvent(QMouseEvent* event) override;

    /**
     * @brief Handles mouse move events.
     * 
     * @param event Mouse move event
     */
    void mouseMoveEvent(QMouseEvent* event) override;

    /**
     * @brief Handles mouse release events.
     * 
     * @param event Mouse release event
     */
    void mouseReleaseEvent(QMouseEvent* event) override;

    /**
     * @brief Handles key press events.
     * 
     * @param event Key press event
     */
    void keyPressEvent(QKeyEvent* event) override;

    /**
     * @brief Handles key release events.
     * 
     * @param event Key release event
     */
    void keyReleaseEvent(QKeyEvent* event) override;

    /**
     * @brief Handles focus out events.
     * 
     * @param event Focus out event
     */
    void focusOutEvent(QFocusEvent* event) override;

    /**
     * @brief Draws foreground elements on the canvas.
     * 
     * @param painter Painter to use for drawing
     * @param rect Rectangle to draw in
     */
    void drawForeground(QPainter* painter, const QRectF& rect) override;

    /**
     * @brief Handles context menu events.
     * 
     * @param event Context menu event
     */
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    // === Helper Methods ===
    /**
     * @brief Updates the search filter for sprites.
     */
    void updateSearch();

    /**
     * @brief Emits the selectionChanged signal.
     */
    void emitSelectionChanged();

    /**
     * @brief Updates border highlights for selected sprites.
     */
    void updateBorderHighlights();

    // === Member Variables ===
    QGraphicsScene* m_scene;                    ///< Graphics scene containing all items
    double m_zoomLevel = 1.0;                   ///< Current zoom level
    bool m_isPanning = false;                   ///< Whether the user is currently panning
    QPoint m_lastMousePos;                      ///< Last mouse position for panning
    bool m_spacePressed = false;                 ///< Whether space key is pressed for panning
    QString m_searchQuery;                       ///< Current search query for filtering sprites
    QRect m_searchCloseRect;                     ///< Rectangle for search close button

    LayoutModel m_model;                         ///< Current layout model
    QVector<SpriteItem*> m_items;                ///< List of sprite items in the scene
    int m_lastSelectedIndex = -1;                ///< Index of last selected sprite
    bool m_pendingDeselect = false;              ///< Whether to deselect on next click

    AppSettings m_settings;                      ///< Application visual settings
    QGraphicsRectItem* m_atlasBgItem = nullptr;  ///< Background item for the atlas
    QList<QAbstractGraphicsShapeItem*> m_borderItems; ///< List of border items for sprites
    QHash<QString, QPixmap> m_sourcePixmaps;     ///< Cache of loaded sprite pixmaps
    QString m_contextMenuTargetPath;             ///< Path of sprite under context menu
};
