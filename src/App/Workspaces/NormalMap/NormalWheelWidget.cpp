#include "NormalWheelWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QSpinBox>

#include <cmath>

static constexpr float kPi      = 3.14159265358979f;
static constexpr int   kSpinH   = 24;
static constexpr int   kSpinGap = 4;

NormalWheelWidget::NormalWheelWidget(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setCursor(Qt::CrossCursor);

    m_angleSpin = new QSpinBox(this);
    m_angleSpin->setRange(0, 360);
    m_angleSpin->setWrapping(true);
    m_angleSpin->setSuffix(QStringLiteral("°"));
    m_angleSpin->setToolTip(tr("Azimuth angle (0–360°): direction the normal points in XY"));
    m_angleSpin->setCursor(Qt::ArrowCursor);

    m_heightSpin = new QSpinBox(this);
    m_heightSpin->setRange(0, 90);
    m_heightSpin->setSuffix(QStringLiteral("°"));
    m_heightSpin->setToolTip(tr("Tilt from straight-up (0° = flat, 90° = fully sideways)"));
    m_heightSpin->setCursor(Qt::ArrowCursor);

    connect(m_angleSpin,  qOverload<int>(&QSpinBox::valueChanged),
            this, &NormalWheelWidget::updateNormalFromSpins);
    connect(m_heightSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &NormalWheelWidget::updateNormalFromSpins);
}

void NormalWheelWidget::setNormal(float nx, float ny, float nz)
{
    nz = std::max(0.0f, nz);
    const float len = std::sqrt(nx*nx + ny*ny + nz*nz);
    if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
    else             { nx = 0.0f; ny = 0.0f; nz = 1.0f; }
    m_nx = nx;
    m_ny = ny;
    m_nz = nz;
    updateSpinsFromNormal();
    update();
}

// ---------------------------------------------------------------------------
// Spin ↔ normal conversions
// ---------------------------------------------------------------------------

void NormalWheelWidget::updateSpinsFromNormal()
{
    const float height_rad = std::acos(qBound(-1.0f, m_nz, 1.0f));
    const int   height_deg = int(std::round(height_rad * 180.0f / kPi));

    float angle_deg = std::atan2(m_ny, m_nx) * 180.0f / kPi;
    if (angle_deg < 0.0f) angle_deg += 360.0f;
    const int angle_deg_int = int(std::round(angle_deg)) % 360;

    QSignalBlocker ba(m_angleSpin), bh(m_heightSpin);
    m_angleSpin->setValue(angle_deg_int);
    m_heightSpin->setValue(height_deg);
}

void NormalWheelWidget::updateNormalFromSpins()
{
    const float height_rad = m_heightSpin->value() * kPi / 180.0f;
    const float angle_rad  = m_angleSpin->value()  * kPi / 180.0f;
    m_nz = std::cos(height_rad);
    const float r = std::sin(height_rad);
    m_nx = r * std::cos(angle_rad);
    m_ny = r * std::sin(angle_rad);
    update();
    emit normalChanged(m_nx, m_ny, m_nz);
}

// ---------------------------------------------------------------------------
// Build cached wheel image (sized to the disc rect)
// ---------------------------------------------------------------------------

void NormalWheelWidget::rebuildWheel()
{
    const int W = m_discRect.width();
    const int H = m_discRect.height();
    if (W <= 0 || H <= 0) return;

    m_wheelImage = QImage(W, H, QImage::Format_ARGB32);
    m_wheelImage.fill(Qt::transparent);

    const float cx = W * 0.5f;
    const float cy = H * 0.5f;
    const float r  = std::min(cx, cy) - 2.0f;
    if (r <= 0.0f) return;

    for (int y = 0; y < H; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(m_wheelImage.scanLine(y));
        for (int x = 0; x < W; ++x) {
            const float dx = (x - cx) / r;
            const float dy = (y - cy) / r;
            const float d2 = dx*dx + dy*dy;
            if (d2 > 1.0f) { line[x] = qRgba(0, 0, 0, 0); continue; }

            const float nz = std::sqrt(1.0f - d2);
            const int R = qBound(0, int((dx + 1.0f) * 127.5f), 255);
            const int G = qBound(0, int((dy + 1.0f) * 127.5f), 255);
            const int B = qBound(0, int((nz + 1.0f) * 127.5f), 255);
            line[x] = qRgba(R, G, B, 255);
        }
    }
}

// ---------------------------------------------------------------------------
// Picking
// ---------------------------------------------------------------------------

void NormalWheelWidget::pickAt(QPoint pos)
{
    const float cx = m_discRect.x() + m_discRect.width()  * 0.5f;
    const float cy = m_discRect.y() + m_discRect.height() * 0.5f;
    const float r  = std::min(m_discRect.width(), m_discRect.height()) * 0.5f - 2.0f;
    if (r <= 0.0f) return;

    float dx = (pos.x() - cx) / r;
    float dy = (pos.y() - cy) / r;
    const float d2 = dx*dx + dy*dy;
    if (d2 > 1.0f) { const float d = std::sqrt(d2); dx /= d; dy /= d; }

    m_nx = dx;
    m_ny = dy;
    m_nz = std::sqrt(std::max(0.0f, 1.0f - dx*dx - dy*dy));
    updateSpinsFromNormal();
    update();
    emit normalChanged(m_nx, m_ny, m_nz);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void NormalWheelWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const float cx = m_discRect.x() + m_discRect.width()  * 0.5f;
    const float cy = m_discRect.y() + m_discRect.height() * 0.5f;
    const float r  = std::min(m_discRect.width(), m_discRect.height()) * 0.5f - 2.0f;

    if (!m_wheelImage.isNull()) {
        QPainterPath clip;
        clip.addEllipse(QPointF(cx, cy), r, r);
        p.setClipPath(clip);
        p.drawImage(m_discRect.x(), m_discRect.y(), m_wheelImage);
        p.setClipping(false);
    }

    p.setPen(QPen(palette().color(QPalette::WindowText), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(cx, cy), r, r);

    const float ix = cx + m_nx * r;
    const float iy = cy + m_ny * r;
    p.setPen(QPen(Qt::white, 2.0));
    p.drawLine(QLineF(ix - 5, iy, ix + 5, iy));
    p.drawLine(QLineF(ix, iy - 5, ix, iy + 5));
    p.setPen(QPen(Qt::black, 1.0));
    p.drawLine(QLineF(ix - 5, iy, ix + 5, iy));
    p.drawLine(QLineF(ix, iy - 5, ix, iy + 5));
    p.setBrush(Qt::white);
    p.setPen(QPen(Qt::black, 1.0));
    p.drawEllipse(QPointF(ix, iy), 3.0, 3.0);
}

void NormalWheelWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        pickAt(event->pos());
    }
}

void NormalWheelWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging)
        pickAt(event->pos());
}

void NormalWheelWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        m_dragging = false;
}

void NormalWheelWidget::resizeEvent(QResizeEvent*)
{
    const int discH = std::max(0, height() - kSpinH - kSpinGap);
    m_discRect = QRect(0, 0, width(), discH);

    const int spinY = discH + kSpinGap;
    const int hw    = (width() - 2) / 2;
    m_angleSpin->setGeometry(0,       spinY, hw,              kSpinH);
    m_heightSpin->setGeometry(hw + 2, spinY, width() - hw - 2, kSpinH);

    rebuildWheel();
}
