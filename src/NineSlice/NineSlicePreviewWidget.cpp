#include "NineSlicePreviewWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSpinBox>
#include <QVBoxLayout>

NineSlicePreviewWidget::NineSlicePreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    // Target-size controls
    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(new QLabel(tr("W:")));
    m_widthSpin = new QSpinBox;
    m_widthSpin->setRange(1, 4096);
    m_widthSpin->setValue(m_targetSize.width());
    m_widthSpin->setSuffix(QStringLiteral(" px"));
    sizeRow->addWidget(m_widthSpin);

    sizeRow->addWidget(new QLabel(tr("H:")));
    m_heightSpin = new QSpinBox;
    m_heightSpin->setRange(1, 4096);
    m_heightSpin->setValue(m_targetSize.height());
    m_heightSpin->setSuffix(QStringLiteral(" px"));
    sizeRow->addWidget(m_heightSpin);
    sizeRow->addStretch();
    layout->addLayout(sizeRow);

    // Preview label (shows the rendered pixmap)
    m_previewLabel = new QLabel;
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setMinimumSize(64, 64);
    m_previewLabel->setStyleSheet("background: #1a1a1a; border: 1px solid #333;");
    layout->addWidget(m_previewLabel, 1);

    connect(m_widthSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &NineSlicePreviewWidget::onTargetSizeChanged);
    connect(m_heightSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &NineSlicePreviewWidget::onTargetSizeChanged);
}

void NineSlicePreviewWidget::setSourceImage(const QPixmap& pixmap)
{
    m_source = pixmap;
    rebuildPreview();
}

void NineSlicePreviewWidget::setInsets(int left, int top, int right, int bottom)
{
    m_left   = left;
    m_top    = top;
    m_right  = right;
    m_bottom = bottom;
    rebuildPreview();
}

void NineSlicePreviewWidget::setFillModes(const QString& hMode, const QString& vMode)
{
    m_hMode = hMode;
    m_vMode = vMode;
    rebuildPreview();
}

void NineSlicePreviewWidget::setTargetSize(const QSize& size)
{
    m_targetSize = size;
    m_widthSpin->setValue(size.width());
    m_heightSpin->setValue(size.height());
    rebuildPreview();
}

void NineSlicePreviewWidget::onTargetSizeChanged()
{
    m_targetSize = QSize(m_widthSpin->value(), m_heightSpin->value());
    rebuildPreview();
}

// ---------------------------------------------------------------------------
// 9-slice rendering
// ---------------------------------------------------------------------------

void NineSlicePreviewWidget::drawTiled(QPainter& painter, const QPixmap& tile,
                                       const QRect& destRect,
                                       const QString& hMode, const QString& vMode) const
{
    if (tile.isNull() || destRect.isEmpty()) return;

    const int tw = tile.width();
    const int th = tile.height();
    if (tw <= 0 || th <= 0) return;

    const bool hStretch = (hMode == QLatin1String("stretch"));
    const bool vStretch = (vMode == QLatin1String("stretch"));

    if (hStretch && vStretch) {
        painter.drawPixmap(destRect, tile);
        return;
    }

    QPixmap tiled(destRect.size());
    tiled.fill(Qt::transparent);
    QPainter tp(&tiled);

    if (hStretch) {
        // Stretch horizontally, tile vertically
        for (int y = 0; y < destRect.height(); y += th) {
            QPixmap src = tile;
            if (vMode == QLatin1String("mirror") && ((y / th) & 1))
                src = src.transformed(QTransform().scale(1, -1));
            int drawH = qMin(th, destRect.height() - y);
            tp.drawPixmap(QRect(0, y, destRect.width(), drawH),
                          src, QRect(0, 0, tw, drawH));
        }
    } else if (vStretch) {
        // Tile horizontally, stretch vertically
        for (int x = 0; x < destRect.width(); x += tw) {
            QPixmap src = tile;
            if (hMode == QLatin1String("mirror") && ((x / tw) & 1))
                src = src.transformed(QTransform().scale(-1, 1));
            int drawW = qMin(tw, destRect.width() - x);
            tp.drawPixmap(QRect(x, 0, drawW, destRect.height()),
                          src, QRect(0, 0, drawW, th));
        }
    } else {
        // Tile both axes
        for (int y = 0; y < destRect.height(); y += th) {
            for (int x = 0; x < destRect.width(); x += tw) {
                QPixmap src = tile;
                if (hMode == QLatin1String("mirror") && ((x / tw) & 1))
                    src = src.transformed(QTransform().scale(-1, 1));
                if (vMode == QLatin1String("mirror") && ((y / th) & 1))
                    src = src.transformed(QTransform().scale(1, -1));
                int drawW = qMin(tw, destRect.width()  - x);
                int drawH = qMin(th, destRect.height() - y);
                tp.drawPixmap(x, y, src, 0, 0, drawW, drawH);
            }
        }
    }

    tp.end();
    painter.drawPixmap(destRect.topLeft(), tiled);
}

void NineSlicePreviewWidget::rebuildPreview()
{
    if (m_source.isNull()) {
        m_previewLabel->setPixmap(QPixmap());
        return;
    }

    const int sw = m_source.width();
    const int sh = m_source.height();
    const int tw = m_targetSize.width();
    const int th = m_targetSize.height();

    // Clamp insets to source dimensions
    int l = qBound(0, m_left,   sw);
    int r = qBound(0, m_right,  sw - l);
    int t = qBound(0, m_top,    sh);
    int b = qBound(0, m_bottom, sh - t);

    // Source regions
    QRect srcTL(0,       0,       l,          t);
    QRect srcTC(l,       0,       sw - l - r, t);
    QRect srcTR(sw - r,  0,       r,          t);
    QRect srcML(0,       t,       l,          sh - t - b);
    QRect srcMC(l,       t,       sw - l - r, sh - t - b);
    QRect srcMR(sw - r,  t,       r,          sh - t - b);
    QRect srcBL(0,       sh - b,  l,          b);
    QRect srcBC(l,       sh - b,  sw - l - r, b);
    QRect srcBR(sw - r,  sh - b,  r,          b);

    // Destination dimensions for stretchable regions
    int destCenterW = qMax(0, tw - l - r);
    int destCenterH = qMax(0, th - t - b);

    // Destination regions
    QRect dstTL(0,       0,       l,          t);
    QRect dstTC(l,       0,       destCenterW, t);
    QRect dstTR(l + destCenterW, 0, r,        t);
    QRect dstML(0,       t,       l,          destCenterH);
    QRect dstMC(l,       t,       destCenterW, destCenterH);
    QRect dstMR(l + destCenterW, t, r,        destCenterH);
    QRect dstBL(0,       t + destCenterH, l,          b);
    QRect dstBC(l,       t + destCenterH, destCenterW, b);
    QRect dstBR(l + destCenterW, t + destCenterH, r,  b);

    QPixmap result(tw, th);
    result.fill(Qt::transparent);
    QPainter p(&result);

    // Corners (always drawn 1:1)
    p.drawPixmap(dstTL, m_source, srcTL);
    p.drawPixmap(dstTR, m_source, srcTR);
    p.drawPixmap(dstBL, m_source, srcBL);
    p.drawPixmap(dstBR, m_source, srcBR);

    // Edges — horizontal edges use hMode, vertical edges use vMode
    drawTiled(p, m_source.copy(srcTC), dstTC, m_hMode, QStringLiteral("stretch"));
    drawTiled(p, m_source.copy(srcBC), dstBC, m_hMode, QStringLiteral("stretch"));
    drawTiled(p, m_source.copy(srcML), dstML, QStringLiteral("stretch"), m_vMode);
    drawTiled(p, m_source.copy(srcMR), dstMR, QStringLiteral("stretch"), m_vMode);

    // Center
    drawTiled(p, m_source.copy(srcMC), dstMC, m_hMode, m_vMode);

    p.end();

    // Scale down for display if needed, keeping aspect ratio
    QPixmap display = result;
    QSize availSize = m_previewLabel->size() - QSize(4, 4);
    if (availSize.width() > 0 && availSize.height() > 0 &&
        (result.width() > availSize.width() || result.height() > availSize.height())) {
        display = result.scaled(availSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    m_previewLabel->setPixmap(display);
}
