#pragma once
#include <QDialog>
#include <QPixmap>
#include <QVector>
#include <QRect>
#include <QString>
#include <QPoint>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QPushButton>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QWidget>
#include <QPainter>
#include <QObject>
#include <QEvent>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
class QDoubleSpinBox;

class FrameDetectionDialog : public QDialog {
    Q_OBJECT
    
public:
    explicit FrameDetectionDialog(const QString& imagePath, const QVector<QRect>& detectedFrames, QWidget* parent = nullptr);
    ~FrameDetectionDialog() override;
    
    QVector<QRect> getSelectedFrames() const;
    bool userAccepted() const;
    
private slots:
    void onAcceptClicked();
    void onRejectClicked();
    
private:
    enum ResizeHandle {
        NoHandle,
        TopLeft, Top, TopRight,
        Right, BottomRight, Bottom,
        BottomLeft, Left
    };
    
    ResizeHandle getResizeHandle(const QPoint& pos, const QRect& rect) const;
    void updateResizeCursor(ResizeHandle handle);
    void resizeFrame(int frameIndex, ResizeHandle handle, const QPointF& scenePos);
    void moveFrame(int frameIndex, const QPointF& sceneDelta);
    void splitFrame(int frameIndex, Qt::Orientation orientation, int pos);
    void createFrame(const QPointF& scenePos);
    void createDefaultFrame(const QPointF& center);
    void deleteSelectedFrames();
    
    bool handleMousePress(QMouseEvent* event);
    bool handleMouseMove(QMouseEvent* event);
    bool handleMouseRelease(QMouseEvent* event);
    
protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    void setupPanning();
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showContextMenu(const QPoint& globalPos);
    
private:
    void drawFrame(QPainter& painter, const QRect& rect, bool isSelected, bool isHovered);
    void drawFrameRectangles();
    void updateFrameVisuals();
    int findFrameAt(const QPoint& pos);
    void updateCursor(const QPoint& pos);
    
protected:
    void wheelEvent(QWheelEvent* event) override;
    
    QPixmap m_image;
    QVector<QRect> m_frames;
    QVector<bool> m_selected;
    QVector<bool> m_hovered;
    
    bool m_userAccepted = false;
    bool m_dragging = false;
    bool m_isPanning = false;
    bool m_spacePressed = false;
    bool m_isResizing = false;
    int m_draggedFrameIndex = -1;
    QPoint m_dragStartPos;
    QPoint m_lastMousePos;
    QRect m_dragOriginalRect;
    QPointF m_dragStartScenePos;
    ResizeHandle m_resizeHandle = NoHandle;
    
    QPushButton* m_acceptBtn;
    QPushButton* m_rejectBtn;
    QGraphicsView* m_imageView;
    QGraphicsScene* m_scene;
    QGraphicsPixmapItem* m_imageItem;
    QVector<QGraphicsRectItem*> m_frameItems;

    QGraphicsLineItem* m_splitLineItem = nullptr;
    int m_splitFrameIndex = -1;
    Qt::Orientation m_splitOrientation = Qt::Horizontal;
    int m_splitPos = 0;

    QDoubleSpinBox* m_zoomSpin = nullptr;
    bool m_splitMode = false;
};