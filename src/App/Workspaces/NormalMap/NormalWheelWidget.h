#pragma once

#include <QImage>
#include <QRect>
#include <QWidget>

class QSpinBox;

/**
 * @brief Circular widget that renders and allows picking from a normal-map hemisphere.
 *
 * The disc is encoded with standard normal-map RGB:
 *   R = (nx + 1) / 2 * 255,  G = (ny + 1) / 2 * 255,  B = nz * 255
 *
 * The centre of the disc represents (0, 0, 1) — the flat/straight-up normal
 * (classic (128, 128, 255) blue). Moving toward the edge tilts the normal
 * in that direction.  Only the upper hemisphere (nz ≥ 0) is represented.
 *
 * Click or drag to pick a normal direction.
 * Angle (0–360°) and Height (0–90°) spinboxes below the disc provide numeric input.
 * Height 0° = straight up (nz = 1), 90° = fully sideways (nz = 0).
 */
class NormalWheelWidget : public QWidget {
    Q_OBJECT
public:
    explicit NormalWheelWidget(QWidget* parent = nullptr);

    void  setNormal(float nx, float ny, float nz);
    float nx() const { return m_nx; }
    float ny() const { return m_ny; }
    float nz() const { return m_nz; }

    QSize sizeHint()        const override { return QSize(120, 152); }
    QSize minimumSizeHint() const override { return QSize(80,  110); }

signals:
    void normalChanged(float nx, float ny, float nz);

protected:
    void paintEvent(QPaintEvent*)        override;
    void mousePressEvent(QMouseEvent*)   override;
    void mouseMoveEvent(QMouseEvent*)    override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*)      override;

private:
    void rebuildWheel();
    void pickAt(QPoint pos);
    void updateSpinsFromNormal();
    void updateNormalFromSpins();

    QImage   m_wheelImage;
    QRect    m_discRect;   // area used for the disc (top portion of widget)
    QSpinBox* m_angleSpin  = nullptr;  // 0–360 °
    QSpinBox* m_heightSpin = nullptr;  // 0–90 °

    float m_nx       = 0.0f;
    float m_ny       = 0.0f;
    float m_nz       = 1.0f;
    bool  m_dragging = false;
};
