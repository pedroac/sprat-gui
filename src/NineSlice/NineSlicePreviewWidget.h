#pragma once
#include <QPixmap>
#include <QWidget>

class QSpinBox;
class QLabel;

/**
 * @brief Renders a 9-slice composite preview by splitting a source image into
 *        9 regions using inset values and drawing them onto a configurable
 *        target size, respecting fill modes (stretch, repeat, mirror).
 *
 * Includes width/height spin boxes above the preview for target-size input.
 */
class NineSlicePreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit NineSlicePreviewWidget(QWidget* parent = nullptr);

    void setSourceImage(const QPixmap& pixmap);
    void setInsets(int left, int top, int right, int bottom);
    void setFillModes(const QString& hMode, const QString& vMode);
    void setTargetSize(const QSize& size);

private slots:
    void onTargetSizeChanged();

private:
    void rebuildPreview();

    /** Fill a rectangular region with a source tile using the given fill mode. */
    void drawTiled(QPainter& painter, const QPixmap& tile,
                   const QRect& destRect, const QString& hMode, const QString& vMode) const;

    QPixmap m_source;
    int m_left = 0, m_top = 0, m_right = 0, m_bottom = 0;
    QString m_hMode = QStringLiteral("stretch");
    QString m_vMode = QStringLiteral("stretch");
    QSize m_targetSize{256, 256};

    QSpinBox* m_widthSpin  = nullptr;
    QSpinBox* m_heightSpin = nullptr;
    QLabel*   m_previewLabel = nullptr;
};
