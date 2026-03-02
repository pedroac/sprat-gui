#pragma once

#include <QGraphicsView>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QResizeEvent>

/**
 * @class ZoomableGraphicsView
 * @brief A base class for QGraphicsView that provides common zooming and panning functionality.
 */
class ZoomableGraphicsView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ZoomableGraphicsView(QWidget* parent = nullptr);

    /**
     * @brief Sets the zoom level of the view.
     * @param zoom Zoom factor (1.0 = 100%)
     */
    virtual void setZoom(double zoom);

    /**
     * @brief Gets the current zoom level.
     * @return Current zoom factor
     */
    double zoom() const;

    /**
     * @brief Checks if the zoom level has been manually changed by the user.
     */
    bool isZoomManual() const { return m_isZoomManual; }

    /**
     * @brief Sets whether the zoom level is considered manual.
     */
    void setZoomManual(bool manual) { m_isZoomManual = manual; }

    /**
     * @brief Centers the content and fits it to the view if necessary.
     */
    virtual void initialFit();

    /**
     * @brief Sets the zoom range.
     */
    void setZoomRange(double min, double max);

signals:
    /**
     * @brief Emitted when the zoom level changes.
     */
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

    bool m_isZoomManual = false;
    bool m_isPanning = false;
    bool m_spacePressed = false;
    QPoint m_lastMousePos;

    double m_minZoom = 0.1;
    double m_maxZoom = 16.0;
};
