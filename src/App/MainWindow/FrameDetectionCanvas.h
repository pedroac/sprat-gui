#pragma once

#include "ZoomableGraphicsView.h"
#include <QVector>
#include <QRect>
#include <QPixmap>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsLineItem>

/**
 * @class FrameDetectionCanvas
 * @brief Custom canvas for reviewing and editing detected frames.
 */
class FrameDetectionCanvas : public ZoomableGraphicsView {
    Q_OBJECT
public:
    enum ResizeHandle {
        NoHandle,
        TopLeft, Top, TopRight,
        Left, Right,
        BottomLeft, Bottom, BottomRight
    };

    explicit FrameDetectionCanvas(QWidget* parent = nullptr);

    void setImage(const QPixmap& image);
    void setFrames(const QVector<QRect>& frames);
    QVector<QRect> getFrames() const { return m_frames; }

    void setSplitMode(bool enabled);
    bool isSplitMode() const { return m_splitMode; }

    void deleteSelectedFrames();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void drawFrameRectangles();
    void updateFrameVisuals();
    void createFrame(const QPointF& scenePos);
    void createDefaultFrame(const QPointF& scenePos);
    void moveFrame(int index, const QPointF& delta);
    void resizeFrame(int index, ResizeHandle handle, const QPointF& scenePos);
    ResizeHandle getResizeHandle(const QPoint& pos, const QRect& rect) const;
    void updateResizeCursor(ResizeHandle handle);
    void splitFrame(int index, Qt::Orientation orientation, int pos);
    int findFrameAt(const QPoint& pos);

    QPixmap m_image;
    QVector<QRect> m_frames;
    QVector<bool> m_selected;
    QVector<bool> m_hovered;

    QGraphicsScene* m_scene;
    QGraphicsPixmapItem* m_imageItem = nullptr;
    QVector<QGraphicsRectItem*> m_frameItems;
    QGraphicsLineItem* m_splitLineItem = nullptr;

    bool m_dragging = false;
    bool m_isResizing = false;
    int m_draggedFrameIndex = -1;
    QRect m_dragOriginalRect;
    QPointF m_dragStartScenePos;
    QPoint m_dragStartPos;
    ResizeHandle m_resizeHandle = NoHandle;

    bool m_splitMode = false;
    int m_splitFrameIndex = -1;
    Qt::Orientation m_splitOrientation = Qt::Horizontal;
    int m_splitPos = 0;
};
