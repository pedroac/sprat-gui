#pragma once
#include <QGraphicsObject>
#include "models.h"

class QGraphicsSceneHoverEvent;

/**
 * @brief Graphics item for drawing and interacting with sprite markers and pivots.
 */
class EditorOverlayItem : public QGraphicsObject {
    Q_OBJECT
public:
    enum ResizeHandle {
        NoHandle,
        TopLeft, Top, TopRight,
        Left, Right,
        BottomLeft, Bottom, BottomRight
    };

    /**
     * @brief Constructs the EditorOverlayItem.
     * @param parent The parent graphics item.
     */
    EditorOverlayItem(QGraphicsItem* parent = nullptr);
    
    /**
     * @brief Sets the sprites being edited.
     * @param sprites List of sprites.
     */
    void setSprites(const QList<SpritePtr>& sprites);
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    /**
     * @brief Sets the size of the scene for hit testing and drawing.
     * @param size The size of the scene.
     */
    void setSceneSize(const QSize& size);

signals:
    /**
     * @brief Emitted when the pivot is moved.
     */
    void pivotChanged(int x, int y);
    /**
     * @brief Emitted when a marker is modified.
     */
    void markerChanged();
    /**
     * @brief Emitted when a marker is selected.
     */
    void markerSelected(const QString& name);
    void applyPivotToSelectedFramesRequested();
    void applyMarkerToSelectedFramesRequested(const QString& markerName);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    QList<SpritePtr> m_sprites;
    QSize m_sceneSize;
    QString m_selectedMarkerName;
    int m_selectedVertexIndex = -1;
    
    bool m_draggingPivot = false;
    
    // Marker Drag State
    enum DragMode { None, Point, CircleRadius, RectMove, RectResize, PolyVertex, PolyMove };
    DragMode m_dragMode = None;
    QString m_dragTargetName;
    int m_dragVertexIndex = -1;
    ResizeHandle m_resizeHandle = NoHandle;
    QPointF m_dragStartPos;
    QPoint m_dragOriginalPos; // x,y or w,h depending on mode
    QRect m_dragOriginalRect;
    int m_dragOriginalRadius = 0;
    QVector<QPoint> m_dragOriginalPoly;
    bool m_suppressNextViewContextMenu = false;
    
    // Helpers
    void drawPivot(QPainter* painter, int x, int y);
    void drawMarkers(QPainter* painter, bool drawSelected);
    double getScale() const;
    NamedPoint* getNamedPoint(const QString& name);
    double distancePointToSegment(const QPointF& p, const QPointF& a, const QPointF& b) const;
    int getPolygonHitEdge(const NamedPoint* p, const QPointF& pos) const;
    ResizeHandle getResizeHandle(const QPointF& pos, const QRectF& rect) const;
    void updateResizeCursor(ResizeHandle handle);
    Qt::CursorShape cursorForPosition(const QPointF& pos) const;
    void updateHoverCursor(const QPointF& pos);
    
public:
    /**
     * @brief Selects a marker by name.
     * @param name The name of the marker.
     */
    void setSelectedMarker(const QString& name);
    /**
     * @brief Removes the selected vertex from a polygon marker.
     * @return True if a vertex was removed.
     */
    bool removeSelectedVertex();
    /**
     * @brief Deletes the currently selected marker from all sprites.
     * @return True if a marker was deleted.
     */
    bool deleteSelectedMarker();
    /**
     * @brief Forces a layout update and redraw.
     */
    void updateLayout();
    const QString& selectedMarkerName() const { return m_selectedMarkerName; }
    bool hasContextMenuTargetAt(const QPointF& pos) const;
    bool consumeSuppressedViewContextMenu();
};
