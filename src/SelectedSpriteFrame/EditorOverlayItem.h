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
    QString m_dragHandle; // "nw", "e", etc for rect
    QPointF m_dragStartPos;
    QPoint m_dragOriginalPos; // x,y or w,h depending on mode
    int m_dragOriginalRadius = 0;
    QVector<QPoint> m_dragOriginalPoly;
    
    // Helpers
    void drawPivot(QPainter* painter, int x, int y);
    void drawMarkers(QPainter* painter, bool drawSelected);
    double getScale() const;
    NamedPoint* getNamedPoint(const QString& name);
    double distancePointToSegment(const QPointF& p, const QPointF& a, const QPointF& b) const;
    int getPolygonHitEdge(const NamedPoint* p, const QPointF& pos) const;
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
     * @brief Forces a layout update and redraw.
     */
    void updateLayout();
};
