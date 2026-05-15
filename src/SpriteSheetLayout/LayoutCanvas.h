#pragma once
#include "ZoomableGraphicsView.h"
#include "models.h"
#include "SpriteItem.h"
#include <atomic>

class QFocusEvent;

/**
 * @class LayoutCanvas
 * @brief Custom QGraphicsView for displaying and managing sprite layouts.
 * 
 * This class provides a visual interface for managing sprite sheets and layouts.
 * It handles sprite display, selection, zooming, panning, and drag-and-drop operations.
 */
class LayoutCanvas : public ZoomableGraphicsView {
    Q_OBJECT
public:
    explicit LayoutCanvas(QWidget* parent = nullptr);

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
     * @brief Emitted when a sprite should be split into two sub-images.
     */
    void splitSpriteRequested(SpritePtr sprite, Qt::Orientation orientation, int localPos);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

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
    QRect m_searchCloseRect;

    QVector<LayoutModel> m_models;
    QVector<QPoint> m_modelOffsets;
    QVector<SpriteItem*> m_items;
    QSet<QString> m_baseSelectionPaths;
    int m_lastSelectedIndex = -1;
    bool m_pendingDeselect = false;

    AppSettings m_settings;
    QGraphicsRectItem* m_atlasBgItem = nullptr;
    QList<QAbstractGraphicsShapeItem*> m_borderItems;
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
};
