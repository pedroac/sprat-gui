#include "AnimationPreviewPanel.h"
#include "AnimationCanvas.h"

#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QIcon>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
AnimationPreviewPanel::AnimationPreviewPanel(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet("font-weight: normal;");

    const int groupMargin      = 4;
    const int groupTopPadding  = 12;
    const int groupBottomMargin = 0;

    auto* box = new QVBoxLayout(this);
    box->setContentsMargins(groupMargin, groupTopPadding, groupMargin, groupBottomMargin);

    // ── Controls row ─────────────────────────────────────────────────────────
    auto* controls = new QHBoxLayout();
    auto* style_   = QApplication::style();

    m_prevBtn = new QPushButton(style_->standardIcon(QStyle::SP_MediaSkipBackward), "", this);
    m_prevBtn->setToolTip(tr("Step to previous frame"));
    m_prevBtn->setAccessibleName(tr("Previous frame"));
    controls->addWidget(m_prevBtn);

    m_playPauseBtn = new QPushButton(style_->standardIcon(QStyle::SP_MediaPlay), "", this);
    m_playPauseBtn->setToolTip(tr("Play or pause animation"));
    m_playPauseBtn->setAccessibleName(tr("Play or pause"));
    controls->addWidget(m_playPauseBtn);

    m_nextBtn = new QPushButton(style_->standardIcon(QStyle::SP_MediaSkipForward), "", this);
    m_nextBtn->setToolTip(tr("Step to next frame"));
    m_nextBtn->setAccessibleName(tr("Next frame"));
    controls->addWidget(m_nextBtn);

    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    controls->addWidget(sep);

    m_overlayBtn = new QToolButton(this);
    m_overlayBtn->setText(tr("Markers"));
    m_overlayBtn->setIcon(QIcon(":/icons/location.svg"));
    m_overlayBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_overlayBtn->setCheckable(true);
    m_overlayBtn->setToolTip(tr("Toggle pivot and marker overlay on animation preview"));
    controls->addWidget(m_overlayBtn);

    m_handleCombo = new QComboBox(this);
    m_handleCombo->addItem(tr("pivot"));
    m_handleCombo->setToolTip(tr("Active handle: only this pivot or marker can be dragged on the canvas"));
    m_handleCombo->setAccessibleName(tr("Active handle"));
    m_handleCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_handleCombo->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    controls->addWidget(m_handleCombo);

    m_onionSkinBtn = new QToolButton(this);
    m_onionSkinBtn->setText(tr("Onion"));
    m_onionSkinBtn->setIcon(QIcon(":/icons/animation.svg"));
    m_onionSkinBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_onionSkinBtn->setCheckable(true);
    m_onionSkinBtn->setChecked(false);
    m_onionSkinBtn->setToolTip(tr("Toggle onion skin: show adjacent frames as ghost overlays"));
    controls->addWidget(m_onionSkinBtn);

    controls->addStretch();

    // Zoom icon label
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
        controls->addWidget(zoomLabel);
    }

    m_zoomSpin = new QDoubleSpinBox(this);
    m_zoomSpin->setRange(10.0, 1600.0);
    m_zoomSpin->setValue(200.0);
    m_zoomSpin->setSuffix("%");
    m_zoomSpin->setSingleStep(10.0);
    m_zoomSpin->setToolTip(tr("Zoom level for animation preview"));
    m_zoomSpin->setAccessibleName(tr("Animation zoom"));
    controls->addWidget(m_zoomSpin);
    box->addLayout(controls);

    // ── Status label ─────────────────────────────────────────────────────────
    m_statusLabel = new QLabel(tr("Create/select a timeline and drag frames into it."), this);
    m_statusLabel->setStyleSheet("color: #808080;");
    box->addWidget(m_statusLabel);

    // ── Animation canvas ─────────────────────────────────────────────────────
    m_animCanvas = new AnimationCanvas(this);
    box->addWidget(m_animCanvas);

    // Internal: keep zoom spin in sync with canvas zoom
    connect(m_animCanvas, &AnimationCanvas::zoomChanged, this, [this](double zoom) {
        m_zoomSpin->blockSignals(true);
        m_zoomSpin->setValue(zoom * 100.0);
        m_zoomSpin->blockSignals(false);
    });
}
