#include "FrameDetectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QIcon>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QShortcut>
#include <QDialog>
#include <QSpinBox>

FrameDetectionDialog::FrameDetectionDialog(const QString& imagePath, const QVector<QRect>& detectedFrames, const AppSettings& settings, const QColor& backgroundColor, QWidget* parent)
    : QDialog(parent)
    , m_image(imagePath)
{
    setWindowTitle(tr("Frame Detection Review"));
    setModal(true);
    resize(1000, 800);
    
    m_canvas = new FrameDetectionCanvas(this);
    m_canvas->setSettings(settings);
    if (backgroundColor.isValid()) {
        m_canvas->setTransparentColor(backgroundColor);
    }
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
    m_acceptBtn->setIcon(QIcon::fromTheme("document-save"));
    m_rejectBtn = new QPushButton(tr("Use as Single Frame"), this);
    m_rejectBtn->setIcon(QIcon::fromTheme("document-new"));
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_cancelBtn->setIcon(QIcon::fromTheme("process-stop"));

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

    // Add zoom shortcuts
    QShortcut* s100 = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_1), this);
    connect(s100, &QShortcut::activated, this, [this]() {
        m_canvas->setZoomManual(true);
        m_zoomSpin->setValue(100.0);
    });

    QShortcut* sFit = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this);
    connect(sFit, &QShortcut::activated, this, [this]() {
        m_canvas->setZoomManual(false);
        m_canvas->initialFit();
    });
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
