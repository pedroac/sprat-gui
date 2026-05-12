#include "SettingsDialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QFileDialog>
#include <QCheckBox>
#include <QIcon>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

SettingsDialog::SettingsDialog(const AppSettings& settings, const CliPaths& cliPaths, QWidget* parent, Section section)
    : QDialog(parent), m_settings(settings), m_cliPaths(cliPaths), m_initialSection(section) {
    setupUi();
}

void SettingsDialog::setupUi() {
    switch (m_initialSection) {
        case Section::Styles: setWindowTitle(tr("Style Settings")); break;
        case Section::Spritesheet: setWindowTitle(tr("Spritesheet Settings")); break;
        case Section::CliTools: setWindowTitle(tr("CLI Tools Settings")); break;
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

    // Spritesheet Group
    m_spritesheetGroup = new QGroupBox(tr("Spritesheet"), content);
    QFormLayout* spritesheetForm = new QFormLayout(m_spritesheetGroup);

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
    m_syncNowBtn->setIcon(QIcon::fromTheme("edit-paste", QIcon::fromTheme("document-save")));
    m_syncNowBtn->setEnabled(m_settings.syncMode != SyncMode::None);
    connect(m_syncNowBtn, &QPushButton::clicked, this, &SettingsDialog::onSyncNowClicked);
    spritesheetForm->addRow("", m_syncNowBtn);

    contentLayout->addWidget(m_spritesheetGroup);
    m_spritesheetGroup->setVisible(m_initialSection == Section::Spritesheet);

#ifndef Q_OS_WASM
    m_cliGroup = new QGroupBox(tr("CLI Tools"), content);
    QFormLayout* cliForm = new QFormLayout(m_cliGroup);

    QHBoxLayout* baseDirLayout = new QHBoxLayout();
    m_cliBaseDirEdit = new QLineEdit(m_cliPaths.baseDir, this);
    m_cliBaseDirEdit->setReadOnly(true);
    m_cliBaseDirBtn = new QPushButton(tr("Change..."), this);
    m_cliBaseDirBtn->setIcon(QIcon::fromTheme("document-open-folder", QIcon::fromTheme("document-open")));
    connect(m_cliBaseDirBtn, &QPushButton::clicked, this, &SettingsDialog::pickCliBaseDir);
    baseDirLayout->addWidget(m_cliBaseDirEdit);
    baseDirLayout->addWidget(m_cliBaseDirBtn);
    cliForm->addRow(tr("Base Directory:"), baseDirLayout);

    m_installCliBtn = new QPushButton(tr("Install CLI Tools"), this);
    m_installCliBtn->setIcon(QIcon::fromTheme("document-save"));
    connect(m_installCliBtn, &QPushButton::clicked, this, &SettingsDialog::installCliToolsRequested);
    cliForm->addRow("", m_installCliBtn);
    
    contentLayout->addWidget(m_cliGroup);
    m_cliGroup->setVisible(m_initialSection == Section::CliTools);

    updateCliUi();
#endif

    contentLayout->addStretch(1);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidget(content);
    layout->addWidget(m_scrollArea, 1);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* resetBtn = buttons->addButton(tr("Reset"), QDialogButtonBox::ResetRole);
    resetBtn->setIcon(QIcon::fromTheme("edit-undo"));

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

void SettingsDialog::pickCliBaseDir() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select CLI Tools Directory"), m_cliPaths.baseDir);
    if (!dir.isEmpty()) {
        m_cliPaths.baseDir = dir;
        m_cliBaseDirEdit->setText(dir);
        updateCliUi();
    }
}

void SettingsDialog::updateCliUi() {
    // Check if tools are present (either in baseDir or PATH)
    bool allFound = !m_cliPaths.layoutBinary.isEmpty() && 
                    !m_cliPaths.packBinary.isEmpty() && 
                    !m_cliPaths.framesBinary.isEmpty();
    
    if (allFound) {
        m_installCliBtn->setText(tr("Update CLI Tools"));
    } else {
        m_installCliBtn->setText(tr("Install CLI Tools"));
    }
}

void SettingsDialog::resetToDefaults() {
    m_settings = AppSettings();
    updateColorButton(m_canvasColorBtn, m_settings.workspaceColor);
    updateColorButton(m_frameColorBtn, m_settings.spriteFrameColor);
    updateColorButton(m_borderColorBtn, m_settings.borderColor);
    updateColorButton(m_detectionSelectedColorBtn, m_settings.detectionSelectedColor);
    m_checkerboardCheck->setChecked(m_settings.showCheckerboard);

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
