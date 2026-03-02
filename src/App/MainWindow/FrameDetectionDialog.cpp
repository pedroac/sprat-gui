#include "FrameDetectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QLabel>

FrameDetectionDialog::FrameDetectionDialog(const QString& imagePath, const QVector<QRect>& detectedFrames, QWidget* parent)
    : QDialog(parent)
    , m_image(imagePath)
{
    setWindowTitle(tr("Frame Detection Review"));
    setModal(true);
    resize(1000, 800);
    
    m_canvas = new FrameDetectionCanvas(this);
    m_canvas->setImage(m_image);
    m_canvas->setFrames(detectedFrames);

    // Top Bar
    QHBoxLayout* topBarLayout = new QHBoxLayout();
    topBarLayout->addStretch();
    topBarLayout->addWidget(new QLabel(tr("Zoom:")));
    m_zoomSpin = new QDoubleSpinBox(this);
    m_zoomSpin->setRange(10.0, 5000.0);
    m_zoomSpin->setValue(100.0);
    m_zoomSpin->setSuffix("%");
    m_zoomSpin->setSingleStep(10.0);
    connect(m_zoomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value){
        m_canvas->setZoom(value / 100.0);
    });
    connect(m_canvas, &FrameDetectionCanvas::zoomChanged, this, [this](double zoom){
        const bool blocked = m_zoomSpin->blockSignals(true);
        m_zoomSpin->setValue(zoom * 100.0);
        m_zoomSpin->blockSignals(blocked);
    });
    topBarLayout->addWidget(m_zoomSpin);

    m_acceptBtn = new QPushButton(tr("Accept Frames"), this);
    m_rejectBtn = new QPushButton(tr("Use as Single Frame"), this);
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    
    connect(m_acceptBtn, &QPushButton::clicked, this, &FrameDetectionDialog::onAcceptClicked);
    connect(m_rejectBtn, &QPushButton::clicked, this, &FrameDetectionDialog::onRejectClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &FrameDetectionDialog::onCancelClicked);
    
    // Layout
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_cancelBtn);
    buttonLayout->addWidget(m_rejectBtn);
    buttonLayout->addWidget(m_acceptBtn);
    
    QVBoxLayout* mainLayout = new QVBoxLayout();
    mainLayout->addLayout(topBarLayout);
    mainLayout->addWidget(m_canvas);
    mainLayout->addLayout(buttonLayout);
    
    setLayout(mainLayout);
}

FrameDetectionDialog::~FrameDetectionDialog() = default;

QVector<QRect> FrameDetectionDialog::getSelectedFrames() const {
    return m_canvas->getFrames();
}

bool FrameDetectionDialog::userAccepted() const {
    return m_userAccepted;
}

void FrameDetectionDialog::onAcceptClicked() {
    m_userAccepted = true;
    accept();
}

void FrameDetectionDialog::onRejectClicked() {
    m_userAccepted = false;
    accept();
}

void FrameDetectionDialog::onCancelClicked() {
    reject();
}
