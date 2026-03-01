#pragma once

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>

class AnimationCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit AnimationCanvas(QWidget* parent = nullptr);

    void setPixmap(const QPixmap& pixmap);
    void setZoom(double zoom);
    double zoom() const;
    bool isZoomManual() const { return m_isZoomManual; }
    void setZoomManual(bool manual) { m_isZoomManual = manual; }
    void initialFit();
    void centerContent();

signals:
    void zoomChanged(double zoom);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QGraphicsScene* m_scene;
    QGraphicsPixmapItem* m_pixmapItem;
    bool m_spacePressed = false;
    bool m_isPanning = false;
    bool m_isZoomManual = false;
    QPoint m_lastMousePos;
};
