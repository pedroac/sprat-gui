#pragma once
#include <QDialog>
#include <QVector>
#include <QRect>
#include <QPixmap>
#include <QPushButton>
#include <QDoubleSpinBox>
#include "FrameDetectionCanvas.h"

class FrameDetectionDialog : public QDialog {
    Q_OBJECT
public:
    explicit FrameDetectionDialog(const QString& imagePath, const QVector<QRect>& initialFrames, QWidget* parent = nullptr);
    ~FrameDetectionDialog() override;

    QVector<QRect> getSelectedFrames() const;
    bool userAccepted() const;

private slots:
    void onAcceptClicked();
    void onRejectClicked();
    void onCancelClicked();
    
private:
    void setupUi(const QString& imagePath);

    QPixmap m_image;
    bool m_userAccepted = false;
    
    QPushButton* m_acceptBtn;
    QPushButton* m_rejectBtn;
    QPushButton* m_cancelBtn;
    FrameDetectionCanvas* m_canvas;
    QDoubleSpinBox* m_zoomSpin = nullptr;
};
