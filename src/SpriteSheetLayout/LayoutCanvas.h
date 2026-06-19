#pragma once
#include "ZoomableGraphicsView.h"
#include "IAtlasViewport.h"
#include "LayoutModels.h"
#include "AppSettings.h"
#include "SpriteItem.h"
#include <atomic>
#include <optional>

class QFocusEvent;
class QGraphicsRectItem;
class QLabel;

/**
 * @class LayoutCanvas
 * @brief Custom QGraphicsView for displaying and managing sprite layouts.
 *
 * This class provides a visual interface for managing sprite sheets and layouts.
 * It handles sprite display, selection, zooming, panning, and drag-and-drop operations.
 */
class LayoutCanvas : public ZoomableGraphicsView, public IAtlasViewport {
    Q_OBJECT
public:
    explicit LayoutCanvas(QWidget* parent = nullptr);

    QWidget* widget() override { return this; }
    double zoom() const override { return ZoomableGraphicsView::zoom(); }

    /**
     * @brief Sets the layout models to display.
     */
    void setModels(const QVector<LayoutModel>& models, std::atomic<bool>* canceled = nullptr);

    /**
     * @brief Asynchronously prepares and sets models.
     * 
     * Loads pixmaps in a background thread and then updates the UI.
     * 
     * @param models The layout models to display
     * @param canceled Optional atomic cancellation flag
     * @param onFinished Optional callback for completion
     */
    void setModelsAsync(const QVector<LayoutModel>& models, std::atomic<bool>* canceled = nullptr, std::function<void()> onFinished = nullptr);

    /**
     * @brief Clears all sprites from the canvas.
     */
    void clearCanvas();

    /**
     * @brief Selects a sprite by its file path.
     */
    void selectSpriteByPath(const QString& path);

    /**
     * @brief Selects multiple sprites by their file paths.
     */
    void selectSpritesByPaths(const QStringList& paths, const QString& primaryPath = QString());

    /**
     * @brief Applies visual settings to the canvas.
     */
    void setSettings(const AppSettings& settings);

    /**
     * @brief Shows or hides the "Loading preview…" banner overlay.
     */
    void setLoadingHint(bool loading);

    /**
     * @brief Puts the canvas in display-only mode: no borders, no labels,
     *        no selection, no context menu, no key interaction.
     */
    void setDisplayOnly(bool displayOnly);

    /**
     * @brief Returns the current layout models.
     */
    const QVector<LayoutModel>& models() const { return m_models; }

    /**
     * @brief Enables or disables split mode.
     */
    void setSplitMode(bool enabled);

    /**
     * @brief Checks if split mode is currently active.
     */
    bool isSplitMode() const { return m_splitMode; }

    /**
     * @brief Removes sprites smaller than the given dimensions.
     */
    void removeFramesSmallerThan(int minW, int minH);

    /**
     * @brief Removes sprites by file path from the canvas without emitting removeFramesRequested.
     *
     * Provides immediate visual feedback when sprites are deleted — leaves a gap
     * in the layout until the next full rebuild.
     */
    void removeSprites(const QStringList& paths);

    /**
     * @brief Returns the current scene position of a sprite item.
     *
     * Returns the item's actual rendered position (including any per-model Y offset).
     * Returns a null optional if the sprite is not found.
     */
    std::optional<QPointF> spriteItemScenePos(const QString& spritePath) const;

    /**
     * @brief Sets a sprite item's scene position directly (no offset applied).
     * Also moves the corresponding border outline by the same delta.
     */
    void setSpriteItemScenePos(const QString& spritePath, const QPointF& pos);

    /**
     * @brief Sets the scene rotation angle of a sprite item (in degrees, CW positive).
     * Call setSpriteItemTransformOrigin first to set the pivot point.
     */
    void setSpriteItemRotation(const QString& spritePath, qreal angle);

    /**
     * @brief Sets the transform origin point of a sprite item (in item coordinates).
     * Must be called before setSpriteItemRotation for correct pivot behaviour.
     */
    void setSpriteItemTransformOrigin(const QString& spritePath, const QPointF& origin);

    /**
     * @brief Sets the label visibility of a sprite item.
     * Used to hide labels during animations to avoid visual artifacts.
     */
    void setSpriteItemLabelHidden(const QString& spritePath, bool hidden);

    /**
     * @brief Sets the active search query and updates the canvas highlighting.
     *
     * Sprites whose name contains the query (case-insensitive) are highlighted
     * and selected.  Pass an empty string to clear the search.
     */
    void setSearchQuery(const QString& query);

    /**
     * @brief Dims sprites whose name does not match the query.
     *
     * Non-matching sprites are rendered at reduced opacity.  Pass an empty
     * string to restore all sprites to full opacity.  The filter persists
     * across layout rebuilds.
     */
    void setDimFilter(const QString& query);

    /**
     * @brief Locks the scene rect to its current bounding rect, preventing auto-resize.
     * Call before animating items to old positions so the canvas doesn't jump in size.
     */
    void freezeSceneRect();

    /**
     * @brief Releases the locked scene rect, restoring normal auto-resize behaviour.
     */
    void thawSceneRect();

    /**
     * @brief Computes scene positions for all sprites in the given models,
     * using the same layout formula as setModels (without modifying the scene).
     */
    static QMap<QString, QPointF> computeItemScenePositions(const QVector<LayoutModel>& models);

    /**
     * @brief Computes the atlas background rects for the given models,
     * using the same layout formula as setModels (without modifying the scene).
     */
    static QVector<QRectF> computeAtlasRects(const QVector<LayoutModel>& models);

    /**
     * @brief Returns the current bounding rects of the atlas background items.
     */
    QVector<QRectF> currentAtlasRects() const;

    /**
     * @brief Sets the atlas background rect at the given index (for animation).
     */
    void setAtlasRect(int index, const QRectF& rect);

    /// Scrolls the view to centre on the atlas at the given index.
    void scrollToAtlas(int index);

signals:
    /**
     * @brief Emitted when the sprite selection changes.
     */
    void selectionChanged(const QList<SpritePtr>& selection);

    /**
     * @brief Emitted when a sprite is selected.
     */
    void spriteSelected(SpritePtr sprite);

    /**
     * @brief Emitted when timeline generation is requested.
     */
    void requestTimelineGeneration();

    /**
     * @brief Emitted when an external path is dropped onto the canvas.
     */
    void externalPathDropped(const QString& path);

    /**
     * @brief Emitted when frames should be added to the layout.
     */
    void addFramesRequested();

    /**
     * @brief Emitted when frames should be removed from the layout.
     */
    void removeFramesRequested(const QStringList& paths);

    /**
     * @brief Emitted when the user has chosen to remove frames smaller than the given dimensions.
     */
    void removeSmallFramesRequested(int minW, int minH);

    /**
     * @brief Emitted when a sprite should be split into two sub-images.
     */
    void splitSpriteRequested(SpritePtr sprite, Qt::Orientation orientation, int localPos);

    /**
     * @brief Emitted when split mode is toggled on or off.
     */
    void splitModeChanged(bool enabled);

    /**
     * @brief Emitted when the user starts interacting with the canvas (mouse enters).
     */
    void userInteractionStarted();

    /**
     * @brief Emitted when the user stops interacting with the canvas (mouse leaves).
     */
    void userInteractionEnded();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void onRemoveSmallTriggered();

private:
    void updateSearch();
    void finalizeSearchSelection();
    void emitSelectionChanged();
    void updateBorderHighlights();
    void ensureSplitLineItem();

    QGraphicsScene* m_scene;
    QString m_searchQuery;
    QString m_dimFilter;

    QVector<LayoutModel> m_models;
    QVector<QPoint> m_modelOffsets;
    QVector<SpriteItem*> m_items;
    QSet<QString> m_baseSelectionPaths;
    int m_lastSelectedIndex = -1;
    bool m_pendingDeselect = false;

    AppSettings m_settings;
    QGraphicsRectItem* m_atlasBgItem = nullptr;
    QList<QAbstractGraphicsShapeItem*> m_borderItems;
    QVector<QGraphicsRectItem*> m_atlasBackgroundItems;
    QHash<QString, QPixmap> m_sourcePixmaps;
    QHash<QString, QPixmap> m_transformedPixmapCache;
    QHash<QString, int> m_pathToIndex;
    QString m_contextMenuTargetPath;
    QPixmap m_cachedCheckerboard;
    QColor m_cachedCheckerboardColor;

    bool                m_splitMode     = false;
    QGraphicsLineItem*  m_splitLineItem = nullptr;
    int                 m_splitItemIndex = -1;
    Qt::Orientation     m_splitOrientation = Qt::Horizontal;

    bool    m_displayOnly   = false;
    QLabel* m_loadingBanner = nullptr;
};
