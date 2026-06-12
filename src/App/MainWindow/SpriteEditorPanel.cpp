#include "SpriteEditorPanel.h"
#include "ElidedLabel.h"
#include "PreviewCanvas.h"
#include "models.h"

#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
SpriteEditorPanel::SpriteEditorPanel(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet("font-weight: normal;");

    const int groupMargin     = 4;
    const int groupTopPadding = 12;
    const int groupBottomMargin = 0;

    auto* box = new QVBoxLayout(this);
    box->setContentsMargins(groupMargin, groupTopPadding, groupMargin, groupBottomMargin);

    // ── Multi-selection indicator ────────────────────────────────────────────
    m_multiSelectionLabel = new QLabel(this);
    m_multiSelectionLabel->setStyleSheet("color: #5a9fd4; font-style: italic; padding: 2px 0;");
    m_multiSelectionLabel->setAlignment(Qt::AlignCenter);
    m_multiSelectionLabel->setVisible(false);
    box->addWidget(m_multiSelectionLabel);

    // ── Viewport row: visual-only options (not stored in exportation) ─────────
    auto* viewportRow = new QHBoxLayout();

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
        viewportRow->addWidget(zoomLabel);
    }

    m_previewZoomSpin = new QDoubleSpinBox(this);
    m_previewZoomSpin->setRange(10.0, 1600.0);
    m_previewZoomSpin->setValue(200.0);
    m_previewZoomSpin->setSuffix("%");
    m_previewZoomSpin->setSingleStep(10.0);
    viewportRow->addWidget(m_previewZoomSpin);

    m_showTrimRectBtn = new QPushButton(tr("Trim"), this);
    m_showTrimRectBtn->setCheckable(true);
    m_showTrimRectBtn->setToolTip(tr("Show the trimmed-content boundary rectangle"));
    m_showTrimRectBtn->setAccessibleName(tr("Show trim rect"));
    viewportRow->addWidget(m_showTrimRectBtn);

    viewportRow->addStretch();
    box->addLayout(viewportRow);

    // ── Sprite row: Name configs | separator | Markers config (stored in exportation) ──
    auto* spriteRow = new QHBoxLayout();

    spriteRow->addWidget(new QLabel(tr("Name:"), this));

    m_spriteNameEdit = new QLineEdit(this);
    m_spriteNameEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_spriteNameEdit->setEnabled(false);
    spriteRow->addWidget(m_spriteNameEdit);

    m_editAliasesBtn = new QPushButton(
        QApplication::style()->standardIcon(QStyle::SP_FileDialogContentsView), "", this);
    m_editAliasesBtn->setToolTip(tr("Edit sprite name aliases (alternative names that share markers and pivots)"));
    m_editAliasesBtn->setEnabled(false);
    spriteRow->addWidget(m_editAliasesBtn);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Sunken);
    spriteRow->addWidget(separator);

    // Crosshair icon for pivot/handle label
    {
        QPixmap pix(16, 16);
        pix.fill(Qt::transparent);
        QPainter painter(&pix);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().color(QPalette::WindowText), 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(8, 8), 3.5, 3.5);
        painter.drawLine(QPointF(8, 1),  QPointF(8, 4));
        painter.drawLine(QPointF(8, 12), QPointF(8, 15));
        painter.drawLine(QPointF(1, 8),  QPointF(4, 8));
        painter.drawLine(QPointF(12, 8), QPointF(15, 8));
        painter.end();
        auto* handleLabel = new QLabel(this);
        handleLabel->setPixmap(pix);
        handleLabel->setToolTip(tr("Selected marker: the pivot point, a named point, or a named area."));
        spriteRow->addWidget(handleLabel);
    }

    m_handleCombo = new QComboBox(this);
    m_handleCombo->addItem(tr("pivot"));
    m_handleCombo->setToolTip(tr("Select pivot or a named marker to edit"));
    m_handleCombo->setAccessibleName(tr("Handle selector"));
    spriteRow->addWidget(m_handleCombo);

    spriteRow->addWidget(new QLabel(tr("X:"), this));
    m_pivotXSpin = new QDoubleSpinBox(this);
    m_pivotXSpin->setEnabled(false);
    m_pivotXSpin->setDecimals(0);
    m_pivotXSpin->setRange(0, 9999);
    m_pivotXSpin->setToolTip(tr("Pivot X: horizontal origin for sprite rotation"));
    m_pivotXSpin->setAccessibleName(tr("Pivot X"));
    spriteRow->addWidget(m_pivotXSpin);

    spriteRow->addWidget(new QLabel(tr("Y:"), this));
    m_pivotYSpin = new QDoubleSpinBox(this);
    m_pivotYSpin->setEnabled(false);
    m_pivotYSpin->setDecimals(0);
    m_pivotYSpin->setRange(0, 9999);
    m_pivotYSpin->setToolTip(tr("Pivot Y: vertical origin for sprite rotation"));
    m_pivotYSpin->setAccessibleName(tr("Pivot Y"));
    spriteRow->addWidget(m_pivotYSpin);

    m_coordUnitCombo = new QComboBox(this);
    m_coordUnitCombo->addItem(tr("px"), int(CoordUnit::Pixels));
    m_coordUnitCombo->addItem(tr("%"),  int(CoordUnit::Percent));
    m_coordUnitCombo->setEnabled(false);
    m_coordUnitCombo->setToolTip(tr("Coordinate unit: pixels or percent of sprite dimensions"));
    spriteRow->addWidget(m_coordUnitCombo);

    m_configPointsBtn = new QPushButton(
        QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView), "", this);
    m_configPointsBtn->setToolTip(tr("Manage Markers: define named points on this sprite, such as hitboxes, spawn positions, or attachment points"));
    m_configPointsBtn->setAccessibleName(tr("Configure markers"));
    m_configPointsBtn->setEnabled(false);
    spriteRow->addWidget(m_configPointsBtn);

    box->addLayout(spriteRow);

    // ── Preview canvas ───────────────────────────────────────────────────────
    m_previewView = new PreviewCanvas(this);
    box->addWidget(m_previewView);

    // ── Footer: sprite name (elided) + sprite dimensions ────────────────────
    auto* footerRow = new QHBoxLayout();
    footerRow->setContentsMargins(8, 2, 8, 2);

    m_spriteNameFooterLabel = new ElidedLabel(this);
    m_spriteNameFooterLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_spriteNameFooterLabel->setVisible(false);
    footerRow->addWidget(m_spriteNameFooterLabel);

    footerRow->addStretch();

    m_spriteDimsLabel = new QLabel(this);
    m_spriteDimsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_spriteDimsLabel->setVisible(false);
    footerRow->addWidget(m_spriteDimsLabel);

    box->addLayout(footerRow);
}
