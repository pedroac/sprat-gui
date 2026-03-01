#pragma once
#include <QDialog>
#include <QVector>
#include <QRect>
#include <QPixmap>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsLineItem>
#include <QPushButton>
#include <QDoubleSpinBox>

class FrameDetectionDialog : public QDialog {
    Q_OBJECT
public:
    enum ResizeHandle {
        NoHandle,
        TopLeft, Top, TopRight,
        Left, Right,
        BottomLeft, Bottom, BottomRight
    };

    explicit FrameDetectionDialog(const QString& imagePath, const QVector<QRect>& initialFrames, QWidget* parent = nullptr);
    ~FrameDetectionDialog() override;

    QVector<QRect> getSelectedFrames() const;
    bool userAccepted() const;

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setupUi(const QString& imagePath);
    void setupPanning();
    void showContextMenu(const QPoint& globalPos);
    void drawFrame(QPainter& painter, const QRect& rect, bool isSelected, bool isHovered);
    void drawFrameRectangles();
    void updateFrameVisuals();
    int findFrameAt(const QPoint& pos);
    void updateCursor(const QPoint& pos);
    void initialFit();
    
private slots:
    void onAcceptClicked();
    void onRejectClicked();
    void onCancelClicked();
    
private:
    // Interaction methods
    bool handleMousePress(QMouseEvent* event);
    bool handleMouseMove(QMouseEvent* event);
    bool handleMouseRelease(QMouseEvent* event);
    void createFrame(const QPointF& scenePos);
    void createDefaultFrame(const QPointF& scenePos);
    void deleteSelectedFrames();
    void moveFrame(int index, const QPointF& delta);
    void resizeFrame(int index, ResizeHandle handle, const QPointF& scenePos);
    ResizeHandle getResizeHandle(const QPoint& pos, const QRect& rect) const;
    void updateResizeCursor(ResizeHandle handle);
    void splitFrame(int index, Qt::Orientation orientation, int pos);

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
    QPushButton* m_cancelBtn;
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
    bool m_isZoomManual = false;
};
