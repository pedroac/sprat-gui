#include "SettingsDialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QFileDialog>
#include <QCheckBox>
#include <QApplication>
#include <QScrollArea>
#include <QStyle>
#include <QScrollBar>
#include <QTimer>

SettingsDialog::SettingsDialog(const AppSettings& settings, const CliPaths& cliPaths, QWidget* parent, Section section)
    : QDialog(parent), m_settings(settings), m_cliPaths(cliPaths), m_initialSection(section) {
    setupUi();
}

void SettingsDialog::setupUi() {
    switch (m_initialSection) {
        case Section::Styles: setWindowTitle(tr("Style Settings")); break;
        case Section::Spritesheet: setWindowTitle(tr("Atlas Sprites Settings")); break;
        case Section::FramesEditor: setWindowTitle(tr("Frames Editor Settings")); break;
#ifndef Q_OS_WASM
        case Section::CliTools: setWindowTitle(tr("CLI Tools Settings")); break;
#endif
    }

    QVBoxLayout* layout = new QVBoxLayout(this);

    QWidget* content = new QWidget(this);
    QVBoxLayout* contentLayout = new QVBoxLayout(content);

    // Styles Group
    m_stylesGroup = new QGroupBox(tr("Styles"), content);
    QFormLayout* stylesForm = new QFormLayout(m_stylesGroup);

    m_canvasColorBtn = createColorButton(m_settings.workspaceColor);
    m_canvasColorBtn->setToolTip(tr("Color of the workspace area outside sprites"));
    m_canvasColorBtn->setAccessibleName(tr("Workspace color"));
    connect(m_canvasColorBtn, &QPushButton::clicked, this, [this]() { pickColor(m_canvasColorBtn, m_settings.workspaceColor); });
    stylesForm->addRow(tr("Workspace Background:"), m_canvasColorBtn);

    m_frameColorBtn = createColorButton(m_settings.spriteFrameColor);
    m_frameColorBtn->setToolTip(tr("Background color of sprite frames"));
    m_frameColorBtn->setAccessibleName(tr("Sprite frame color"));
    connect(m_frameColorBtn, &QPushButton::clicked, this, [this]() { pickColor(m_frameColorBtn, m_settings.spriteFrameColor); });
    stylesForm->addRow(tr("Sprite Frame Background:"), m_frameColorBtn);

    m_checkerboardCheck = new QCheckBox(tr("Show Transparency Checkerboard"), this);
    m_checkerboardCheck->setChecked(m_settings.showCheckerboard);
    m_checkerboardCheck->setToolTip(tr("Show checkerboard pattern for transparent areas"));
    m_checkerboardCheck->setAccessibleName(tr("Transparency checkerboard"));
    stylesForm->addRow("", m_checkerboardCheck);

    m_borderColorBtn = createColorButton(m_settings.borderColor);
    m_borderColorBtn->setToolTip(tr("Color of borders around sprites"));
    m_borderColorBtn->setAccessibleName(tr("Border color"));
    connect(m_borderColorBtn, &QPushButton::clicked, this, [this]() { pickColor(m_borderColorBtn, m_settings.borderColor); });
    stylesForm->addRow(tr("Border Color:"), m_borderColorBtn);

    m_detectionSelectedColorBtn = createColorButton(m_settings.detectionSelectedColor);
    m_detectionSelectedColorBtn->setToolTip(tr("Color for frames selected in detection dialog"));
    m_detectionSelectedColorBtn->setAccessibleName(tr("Detection selected color"));
    connect(m_detectionSelectedColorBtn, &QPushButton::clicked, this, [this]() { pickColor(m_detectionSelectedColorBtn, m_settings.detectionSelectedColor); });
    stylesForm->addRow(tr("Detection Selected Color:"), m_detectionSelectedColorBtn);

    m_borderStyleCombo = new QComboBox(this);
    m_borderStyleCombo->addItem(tr("None"), (int)Qt::NoPen);
    m_borderStyleCombo->addItem(tr("Solid"), (int)Qt::SolidLine);
    m_borderStyleCombo->addItem(tr("Dash"), (int)Qt::DashLine);
    m_borderStyleCombo->addItem(tr("Dot"), (int)Qt::DotLine);
    m_borderStyleCombo->addItem(tr("DashDot"), (int)Qt::DashDotLine);
    m_borderStyleCombo->addItem(tr("DashDotDot"), (int)Qt::DashDotDotLine);

    int index = m_borderStyleCombo->findData((int)m_settings.borderStyle);
    if (index >= 0) {
        m_borderStyleCombo->setCurrentIndex(index);
    }

    m_borderStyleCombo->setToolTip(tr("Visual style for sprite borders"));
    m_borderStyleCombo->setAccessibleName(tr("Border style"));
    stylesForm->addRow(tr("Border Style:"), m_borderStyleCombo);

    contentLayout->addWidget(m_stylesGroup);
    m_stylesGroup->setVisible(m_initialSection == Section::Styles);

    // Atlas Sprites Group
    m_spritesheetGroup = new QGroupBox(tr("Atlas Sprites"), content);
    QFormLayout* spritesheetForm = new QFormLayout(m_spritesheetGroup);

    auto* dedupDesc = new QLabel(tr("Deduplication creates aliases for identical or similar sprites, "
                                    "allowing them to share the same space in the atlas."), this);
    dedupDesc->setWordWrap(true);
    dedupDesc->setStyleSheet("color: #666; margin-bottom: 4px;");
    spritesheetForm->addRow(dedupDesc);

    m_deduplicateModeCombo = new QComboBox(this);
    m_deduplicateModeCombo->addItem(tr("None (no deduplication)"), "none");
    m_deduplicateModeCombo->addItem(tr("Exact (identical images)"), "exact");
    m_deduplicateModeCombo->addItem(tr("Perceptual (similar images)"), "perceptual");

    int deducateIndex = m_deduplicateModeCombo->findData(m_settings.deduplicateMode);
    if (deducateIndex >= 0) {
        m_deduplicateModeCombo->setCurrentIndex(deducateIndex);
    }

    m_deduplicateModeCombo->setToolTip(tr("Remove duplicate sprites from the layout"));
    m_deduplicateModeCombo->setAccessibleName(tr("Deduplicate mode"));
    spritesheetForm->addRow(tr("Deduplicate Sprites:"), m_deduplicateModeCombo);

    // Sync controls
    m_syncModeCombo = new QComboBox(this);
    m_syncModeCombo->addItem(tr("None (no sync)"), (int)SyncMode::None);
    m_syncModeCombo->addItem(tr("Manual (sync on demand)"), (int)SyncMode::Manual);
    m_syncModeCombo->addItem(tr("Watch (live monitoring)"), (int)SyncMode::Watch);
    int syncIndex = m_syncModeCombo->findData((int)m_settings.syncMode);
    if (syncIndex >= 0) {
        m_syncModeCombo->setCurrentIndex(syncIndex);
    }
    m_syncModeCombo->setToolTip(tr("How to synchronize source folder with project"));
    m_syncModeCombo->setAccessibleName(tr("Sync mode"));
    connect(m_syncModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onSyncModeChanged);
    spritesheetForm->addRow(tr("Sync Mode:"), m_syncModeCombo);

    m_syncNowBtn = new QPushButton(tr("Sync Now"), this);
    m_syncNowBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_BrowserReload));
    m_syncNowBtn->setEnabled(m_settings.syncMode != SyncMode::None);
    connect(m_syncNowBtn, &QPushButton::clicked, this, &SettingsDialog::onSyncNowClicked);
    spritesheetForm->addRow("", m_syncNowBtn);

    contentLayout->addWidget(m_spritesheetGroup);
    m_spritesheetGroup->setVisible(m_initialSection == Section::Spritesheet);

    // Frames Editor Group
    m_framesEditorGroup = new QGroupBox(tr("Frames Editor"), content);
    QFormLayout* framesEditorForm = new QFormLayout(m_framesEditorGroup);

    m_onionSkinCheck = new QCheckBox(tr("Enable onion skin"), this);
    m_onionSkinCheck->setChecked(m_settings.onionSkinEnabled);
    m_onionSkinCheck->setToolTip(tr("Show other checked frames as transparent overlays behind the active frame"));
    framesEditorForm->addRow("", m_onionSkinCheck);

    m_propagateEditsCheck = new QCheckBox(tr("Apply edits to all checked frames"), this);
    m_propagateEditsCheck->setChecked(m_settings.propagateEditsToChecked);
    m_propagateEditsCheck->setToolTip(tr("Automatically apply pivot and marker edits to all checked frames simultaneously"));
    framesEditorForm->addRow("", m_propagateEditsCheck);

    contentLayout->addWidget(m_framesEditorGroup);
    m_framesEditorGroup->setVisible(m_initialSection == Section::FramesEditor);

#ifndef Q_OS_WASM
    m_cliGroup = new QGroupBox(tr("CLI Tools"), content);
    QFormLayout* cliForm = new QFormLayout(m_cliGroup);

    QHBoxLayout* baseDirLayout = new QHBoxLayout();
    m_cliBaseDirEdit = new QLineEdit(m_cliPaths.baseDir, this);
    m_cliBaseDirEdit->setReadOnly(true);
    m_cliBaseDirBtn = new QPushButton(tr("Change..."), this);
    m_cliBaseDirBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
    connect(m_cliBaseDirBtn, &QPushButton::clicked, this, &SettingsDialog::pickCliBaseDir);
    baseDirLayout->addWidget(m_cliBaseDirEdit);
    baseDirLayout->addWidget(m_cliBaseDirBtn);
    cliForm->addRow(tr("Base Directory:"), baseDirLayout);
    
    contentLayout->addWidget(m_cliGroup);
    m_cliGroup->setVisible(m_initialSection == Section::CliTools);
#endif

    contentLayout->addStretch(1);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidget(content);
    layout->addWidget(m_scrollArea, 1);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* resetBtn = buttons->addButton(tr("Reset"), QDialogButtonBox::ResetRole);
    resetBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogResetButton));

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(resetBtn, &QPushButton::clicked, this, &SettingsDialog::resetToDefaults);
    layout->addWidget(buttons);
}

void SettingsDialog::focusSection(Section section) {
    Q_UNUSED(section);
}

QPushButton* SettingsDialog::createColorButton(const QColor& color) {
    QPushButton* btn = new QPushButton(this);
    updateColorButton(btn, color);
    return btn;
}

void SettingsDialog::updateColorButton(QPushButton* btn, const QColor& color) {
    QString qss = QString("background-color: %1; border: 1px solid #555;").arg(color.name());
    btn->setStyleSheet(qss);
    btn->setText(color.name());
}

void SettingsDialog::pickColor(QPushButton* btn, QColor& color) {
    QColor newColor = QColorDialog::getColor(color, this, tr("Select Color"));
    if (newColor.isValid()) {
        color = newColor;
        updateColorButton(btn, color);
    }
}

#ifndef Q_OS_WASM
void SettingsDialog::pickCliBaseDir() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select CLI Tools Directory"), m_cliPaths.baseDir);
    if (!dir.isEmpty()) {
        m_cliPaths.baseDir = dir;
        m_cliBaseDirEdit->setText(dir);
    }
}
#endif

void SettingsDialog::resetToDefaults() {
    m_settings = AppSettings();
    updateColorButton(m_canvasColorBtn, m_settings.workspaceColor);
    updateColorButton(m_frameColorBtn, m_settings.spriteFrameColor);
    updateColorButton(m_borderColorBtn, m_settings.borderColor);
    updateColorButton(m_detectionSelectedColorBtn, m_settings.detectionSelectedColor);
    m_checkerboardCheck->setChecked(m_settings.showCheckerboard);
    if (m_onionSkinCheck) m_onionSkinCheck->setChecked(AppSettings().onionSkinEnabled);
    if (m_propagateEditsCheck) m_propagateEditsCheck->setChecked(AppSettings().propagateEditsToChecked);

    int deduplicateIndex = m_deduplicateModeCombo->findData(m_settings.deduplicateMode);
    if (deduplicateIndex >= 0) {
        m_deduplicateModeCombo->setCurrentIndex(deduplicateIndex);
    }

    int borderIndex = m_borderStyleCombo->findData((int)m_settings.borderStyle);
    if (borderIndex >= 0) {
        m_borderStyleCombo->setCurrentIndex(borderIndex);
    }

    int syncIndex = m_syncModeCombo->findData((int)m_settings.syncMode);
    if (syncIndex >= 0) {
        m_syncModeCombo->setCurrentIndex(syncIndex);
    }
}

AppSettings SettingsDialog::getSettings() const {
    AppSettings s = m_settings;
    s.showCheckerboard = m_checkerboardCheck->isChecked();
    s.borderStyle = (Qt::PenStyle)m_borderStyleCombo->currentData().toInt();
    s.deduplicateMode = m_deduplicateModeCombo->currentData().toString();
    s.syncMode = (SyncMode)m_syncModeCombo->currentData().toInt();
    if (m_onionSkinCheck) s.onionSkinEnabled = m_onionSkinCheck->isChecked();
    if (m_propagateEditsCheck) s.propagateEditsToChecked = m_propagateEditsCheck->isChecked();
    return s;
}

CliPaths SettingsDialog::getCliPaths() const {
    return m_cliPaths;
}

void SettingsDialog::onSyncModeChanged(int index) {
    Q_UNUSED(index);
    // Enable "Sync Now" button only for Manual and Watch modes
    SyncMode mode = (SyncMode)m_syncModeCombo->currentData().toInt();
    m_syncNowBtn->setEnabled(mode != SyncMode::None);
}

void SettingsDialog::onSyncNowClicked() {
    qInfo() << "[SettingsDialog] Sync Now clicked, emitting signal";
    emit syncNowRequested();
}
