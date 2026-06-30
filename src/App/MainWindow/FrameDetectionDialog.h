#pragma once
#include <QDialog>
#include <QVector>
#include <QRect>
#include <QPixmap>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QColor>
#include <QSpinBox>
#include <functional>
#include <QTimer>
#include "FrameDetectionCanvas.h"

class FrameDetectionDialog : public QDialog {
    Q_OBJECT
public:
    using RedetectFn = std::function<QVector<QRect>(const AppSettings&)>;

    explicit FrameDetectionDialog(const QString& imagePath, const QVector<QRect>& initialFrames, const AppSettings& settings, RedetectFn redetectFn = {}, const QColor& backgroundColor = QColor(), QWidget* parent = nullptr);
    ~FrameDetectionDialog() override;

    QVector<QRect> getSelectedFrames() const;
    bool userAccepted() const;
    AppSettings getUpdatedSettings() const;

    /** Hide the "Use as Single Frame" button (call before exec()). */
    void hideSingleFrameButton();

private slots:
    void onAcceptClicked();
    void onRejectClicked();
    void onCancelClicked();
    void onRedetectClicked();
    void onResetClicked();

private:
    QPixmap m_image;
    AppSettings m_settings;
    RedetectFn m_redetectFn;
    bool m_userAccepted = false;
    QVector<QRect> m_lastDetectedFrames;  // raw spratframes output — used by Reset
    QVector<QRect> m_lastDisplayedFrames; // filtered result last set on canvas — used for user-edit diff
    QVector<QRect> m_userRemovedRects;   // areas the user removed — persists across re-detections
    QVector<QRect> m_userAddedFrames;    // frames the user drew manually — persists across re-detections

    QPushButton* m_acceptBtn;
    QPushButton* m_rejectBtn;
    QPushButton* m_cancelBtn;
    QPushButton* m_resetBtn = nullptr;
    QTimer* m_redetectTimer = nullptr;
    FrameDetectionCanvas* m_canvas;
    QDoubleSpinBox* m_zoomSpin = nullptr;
    QPushButton* m_hasRectanglesToggle = nullptr;
    QPushButton* m_rectangleColorBtn = nullptr;
    QColor m_rectangleColor;
    QSpinBox* m_toleranceSpin = nullptr;
    QSpinBox* m_minSizeSpin = nullptr;
};
