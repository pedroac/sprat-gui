#include "FrameDetectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPainter>
#include <QStyle>
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
    {
        QPixmap pix(16, 16);
        pix.fill(Qt::transparent);
        QPainter painter(&pix);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().color(QPalette::WindowText), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(6, 6), 4.5, 4.5);
        painter.drawLine(QPointF(9.2, 9.2), QPointF(14, 14));
        painter.end();
        auto* zoomLabel = new QLabel(this);
        zoomLabel->setPixmap(pix);
        zoomLabel->setToolTip(tr("Zoom"));
        topBarLayout->addWidget(zoomLabel);
    }
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

    auto* style_ = QApplication::style();
    m_acceptBtn = new QPushButton(style_->standardIcon(QStyle::SP_DialogApplyButton), tr("Accept Frames"), this);
    m_rejectBtn = new QPushButton(style_->standardIcon(QStyle::SP_FileIcon), tr("Use as Single Frame"), this);
    m_cancelBtn = new QPushButton(style_->standardIcon(QStyle::SP_DialogCancelButton), tr("Cancel"), this);

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
