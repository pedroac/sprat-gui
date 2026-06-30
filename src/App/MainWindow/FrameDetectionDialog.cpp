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
#include <QColorDialog>

FrameDetectionDialog::FrameDetectionDialog(const QString& imagePath, const QVector<QRect>& detectedFrames, const AppSettings& settings, RedetectFn redetectFn, const QColor& backgroundColor, QWidget* parent)
    : QDialog(parent)
    , m_image(imagePath)
    , m_settings(settings)
    , m_redetectFn(std::move(redetectFn))
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
    m_lastDetectedFrames  = detectedFrames;
    m_lastDisplayedFrames = detectedFrames;

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

    // Options row
    QHBoxLayout* optionsLayout = new QHBoxLayout();
    m_hasRectanglesToggle = new QPushButton(tr("Rectangle colors"), this);
    m_hasRectanglesToggle->setCheckable(true);
    m_hasRectanglesToggle->setChecked(settings.framesHasRectangles);
    m_hasRectanglesToggle->setToolTip(tr("Enable when sprites are outlined by colored rectangle borders"));
    optionsLayout->addWidget(m_hasRectanglesToggle);
    {
        QStringList parts = settings.framesRectangleColor.split(',');
        if (parts.size() == 3) {
            bool ok1, ok2, ok3;
            int r = parts[0].trimmed().toInt(&ok1);
            int g = parts[1].trimmed().toInt(&ok2);
            int b = parts[2].trimmed().toInt(&ok3);
            if (ok1 && ok2 && ok3)
                m_rectangleColor = QColor(r, g, b);
        }
        if (!m_rectangleColor.isValid())
            m_rectangleColor = QColor(255, 0, 255);
    }
    m_rectangleColorBtn = new QPushButton(this);
    m_rectangleColorBtn->setStyleSheet(
        QString("QPushButton { background-color: %1; border: 1px solid #555; }"
                "QPushButton:disabled { background-color: rgba(%2,%3,%4,80); border: 1px solid #777;"
                " image: url(:/icons/disabled.svg); }")
            .arg(m_rectangleColor.name())
            .arg(m_rectangleColor.red()).arg(m_rectangleColor.green()).arg(m_rectangleColor.blue()));
    m_rectangleColorBtn->setFixedWidth(48);
    m_rectangleColorBtn->setEnabled(settings.framesHasRectangles);
    optionsLayout->addWidget(m_rectangleColorBtn);

    connect(m_hasRectanglesToggle, &QPushButton::toggled, m_rectangleColorBtn, &QPushButton::setEnabled);
    connect(m_rectangleColorBtn, &QPushButton::clicked, this, [this]() {
        QColor c = QColorDialog::getColor(m_rectangleColor, this, tr("Select Rectangle Color"));
        if (c.isValid()) {
            m_rectangleColor = c;
            m_rectangleColorBtn->setStyleSheet(
                QString("QPushButton { background-color: %1; border: 1px solid #555; }"
                        "QPushButton:disabled { background-color: rgba(%2,%3,%4,80); border: 1px solid #777;"
                        " image: url(:/icons/disabled.svg); }")
                    .arg(m_rectangleColor.name())
                    .arg(m_rectangleColor.red()).arg(m_rectangleColor.green()).arg(m_rectangleColor.blue()));
            m_redetectTimer->start();
        }
    });

    optionsLayout->addWidget(new QLabel(tr("Tolerance:"), this));
    m_toleranceSpin = new QSpinBox(this);
    m_toleranceSpin->setRange(1, 100);
    m_toleranceSpin->setValue(settings.framesTolerance);
    optionsLayout->addWidget(m_toleranceSpin);

    optionsLayout->addWidget(new QLabel(tr("Min size:"), this));
    m_minSizeSpin = new QSpinBox(this);
    m_minSizeSpin->setRange(1, 9999);
    m_minSizeSpin->setValue(settings.framesMinSize);
    optionsLayout->addWidget(m_minSizeSpin);

    optionsLayout->addStretch();

    m_redetectTimer = new QTimer(this);
    m_redetectTimer->setSingleShot(true);
    m_redetectTimer->setInterval(400);
    connect(m_redetectTimer, &QTimer::timeout, this, &FrameDetectionDialog::onRedetectClicked);
    connect(m_hasRectanglesToggle, &QPushButton::toggled,
            this, [this](bool) { m_redetectTimer->start(); });
    connect(m_toleranceSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { m_redetectTimer->start(); });
    connect(m_minSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { m_redetectTimer->start(); });

    auto* style_ = QApplication::style();
    m_acceptBtn = new QPushButton(style_->standardIcon(QStyle::SP_DialogApplyButton), tr("Accept Frames"), this);
    m_rejectBtn = new QPushButton(style_->standardIcon(QStyle::SP_FileIcon), tr("Use as Single Frame"), this);
    m_cancelBtn = new QPushButton(style_->standardIcon(QStyle::SP_DialogCancelButton), tr("Cancel"), this);
    m_resetBtn  = new QPushButton(style_->standardIcon(QStyle::SP_DialogResetButton), tr("Reset"), this);
    m_resetBtn->setToolTip(tr("Discard all manual edits and restore the last detected frames"));

    connect(m_acceptBtn, &QPushButton::clicked, this, &FrameDetectionDialog::onAcceptClicked);
    connect(m_rejectBtn, &QPushButton::clicked, this, &FrameDetectionDialog::onRejectClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &FrameDetectionDialog::onCancelClicked);
    connect(m_resetBtn,  &QPushButton::clicked, this, &FrameDetectionDialog::onResetClicked);

    // Layout
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(m_resetBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_cancelBtn);
    buttonLayout->addWidget(m_rejectBtn);
    buttonLayout->addWidget(m_acceptBtn);
    
    QVBoxLayout* mainLayout = new QVBoxLayout();
    auto* descLabel = new QLabel(tr("Review the automatically detected frames. Accept to add them to the layout, or import the image as a single frame."), this);
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);
    mainLayout->addLayout(topBarLayout);
    mainLayout->addLayout(optionsLayout);
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

void FrameDetectionDialog::hideSingleFrameButton() {
    m_rejectBtn->hide();
}

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

void FrameDetectionDialog::onResetClicked() {
    m_userRemovedRects.clear();
    m_userAddedFrames.clear();
    m_canvas->setFrames(m_lastDetectedFrames);
    m_lastDisplayedFrames = m_lastDetectedFrames;
}

void FrameDetectionDialog::onRedetectClicked() {
    if (!m_redetectFn) return;

    const QVector<QRect> current = m_canvas->getFrames();

    // Diff against what was last DISPLAYED (filtered result), not the raw
    // detection output.  This ensures only genuine user edits are recorded —
    // frames that were suppressed by the filter are absent from both
    // m_lastDisplayedFrames and current, so they never accumulate as phantom
    // "user removals".

    // Frames that were displayed but are now missing → user removed them
    for (const QRect& r : m_lastDisplayedFrames) {
        if (!current.contains(r) && !m_userRemovedRects.contains(r))
            m_userRemovedRects.append(r);
    }
    // If the user re-drew a rect that was in the removed list, unblock it
    for (const QRect& r : current)
        m_userRemovedRects.removeAll(r);

    // Frames on canvas that were not in the last displayed set → user added them
    for (const QRect& r : current) {
        if (!m_lastDisplayedFrames.contains(r) && !m_userAddedFrames.contains(r))
            m_userAddedFrames.append(r);
    }
    // Drop manually added frames the user has since removed
    QVector<QRect> stillAdded;
    for (const QRect& r : m_userAddedFrames) {
        if (current.contains(r))
            stillAdded.append(r);
    }
    m_userAddedFrames = stillAdded;

    // Run new detection (m_lastDetectedFrames kept for the Reset button)
    m_lastDetectedFrames = m_redetectFn(getUpdatedSettings());

    // For each detected frame, subtract every removed rect from it.
    // subtractRect returns the pieces of `r` that lie outside `removed`
    // (up to 4 axis-aligned strips).  An empty result means `r` was entirely
    // inside `removed` and is fully suppressed.  The pieces are a filtering
    // artefact — they are NOT recorded in m_userAddedFrames.
    auto subtractRect = [](const QRect& r, const QRect& removed) -> QVector<QRect> {
        const QRect ix = r.intersected(removed);
        if (!ix.isValid())
            return {r};
        QVector<QRect> out;
        if (ix.top() > r.top())
            out.append(QRect(r.left(), r.top(), r.width(), ix.top() - r.top()));
        if (ix.bottom() < r.bottom())
            out.append(QRect(r.left(), ix.bottom() + 1, r.width(), r.bottom() - ix.bottom()));
        if (ix.left() > r.left())
            out.append(QRect(r.left(), ix.top(), ix.left() - r.left(), ix.height()));
        if (ix.right() < r.right())
            out.append(QRect(ix.right() + 1, ix.top(), r.right() - ix.right(), ix.height()));
        return out; // empty when r was entirely inside removed
    };

    QVector<QRect> merged;
    for (const QRect& r : m_lastDetectedFrames) {
        QVector<QRect> pieces = {r};
        for (const QRect& removed : m_userRemovedRects) {
            QVector<QRect> next;
            for (const QRect& piece : pieces)
                next += subtractRect(piece, removed);
            pieces = next;
        }
        merged += pieces;
    }
    for (const QRect& r : m_userAddedFrames) {
        if (!merged.contains(r))
            merged.append(r);
    }

    m_canvas->setFrames(merged);
    m_lastDisplayedFrames = merged;
}

AppSettings FrameDetectionDialog::getUpdatedSettings() const {
    AppSettings s = m_settings;
    s.framesHasRectangles = m_hasRectanglesToggle->isChecked();
    s.framesRectangleColor = QString("%1,%2,%3")
        .arg(m_rectangleColor.red())
        .arg(m_rectangleColor.green())
        .arg(m_rectangleColor.blue());
    s.framesTolerance = m_toleranceSpin->value();
    s.framesMinSize = m_minSizeSpin->value();
    return s;
}
