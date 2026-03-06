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

SettingsDialog::SettingsDialog(const AppSettings& settings, const CliPaths& cliPaths, QWidget* parent)
    : QDialog(parent), m_settings(settings), m_cliPaths(cliPaths) {
    setupUi();
}

void SettingsDialog::setupUi() {
    setWindowTitle(tr("Settings"));
    QVBoxLayout* layout = new QVBoxLayout(this);
    QFormLayout* form = new QFormLayout();

    m_canvasColorBtn = createColorButton(m_settings.workspaceColor);
    connect(m_canvasColorBtn, &QPushButton::clicked, this, [this]() { pickColor(m_canvasColorBtn, m_settings.workspaceColor); });
    form->addRow(tr("Workspace Background:"), m_canvasColorBtn);

    m_frameColorBtn = createColorButton(m_settings.spriteFrameColor);
    connect(m_frameColorBtn, &QPushButton::clicked, this, [this]() { pickColor(m_frameColorBtn, m_settings.spriteFrameColor); });
    form->addRow(tr("Sprite Frame Background:"), m_frameColorBtn);

    m_checkerboardCheck = new QCheckBox(tr("Show Transparency Checkerboard"), this);
    m_checkerboardCheck->setChecked(m_settings.showCheckerboard);
    form->addRow("", m_checkerboardCheck);

    m_borderColorBtn = createColorButton(m_settings.borderColor);
    connect(m_borderColorBtn, &QPushButton::clicked, this, [this]() { pickColor(m_borderColorBtn, m_settings.borderColor); });
    form->addRow(tr("Border Color:"), m_borderColorBtn);

    m_detectionSelectedColorBtn = createColorButton(m_settings.detectionSelectedColor);
    connect(m_detectionSelectedColorBtn, &QPushButton::clicked, this, [this]() { pickColor(m_detectionSelectedColorBtn, m_settings.detectionSelectedColor); });
    form->addRow(tr("Detection Selected Color:"), m_detectionSelectedColorBtn);

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

    form->addRow(tr("Border Style:"), m_borderStyleCombo);
    layout->addLayout(form);

    QGroupBox* cliGroup = new QGroupBox(tr("CLI Tools"), this);
    QFormLayout* cliForm = new QFormLayout(cliGroup);
    
    QHBoxLayout* baseDirLayout = new QHBoxLayout();
    m_cliBaseDirEdit = new QLineEdit(m_cliPaths.baseDir, this);
    m_cliBaseDirEdit->setReadOnly(true);
    m_cliBaseDirBtn = new QPushButton(tr("Change..."), this);
    connect(m_cliBaseDirBtn, &QPushButton::clicked, this, &SettingsDialog::pickCliBaseDir);
    baseDirLayout->addWidget(m_cliBaseDirEdit);
    baseDirLayout->addWidget(m_cliBaseDirBtn);
    cliForm->addRow(tr("Base Directory:"), baseDirLayout);

    m_installCliBtn = new QPushButton(tr("Install CLI Tools"), this);
    connect(m_installCliBtn, &QPushButton::clicked, this, &SettingsDialog::installCliToolsRequested);
    cliForm->addRow("", m_installCliBtn);
    
    layout->addWidget(cliGroup);

    updateCliUi();

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* resetBtn = buttons->addButton(tr("Reset"), QDialogButtonBox::ResetRole);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(resetBtn, &QPushButton::clicked, this, &SettingsDialog::resetToDefaults);
    layout->addWidget(buttons);
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
    int index = m_borderStyleCombo->findData((int)m_settings.borderStyle);
    if (index >= 0) {
        m_borderStyleCombo->setCurrentIndex(index);
    }
}

AppSettings SettingsDialog::getSettings() const {
    AppSettings s = m_settings;
    s.showCheckerboard = m_checkerboardCheck->isChecked();
    s.borderStyle = (Qt::PenStyle)m_borderStyleCombo->currentData().toInt();
    return s;
}

CliPaths SettingsDialog::getCliPaths() const {
    return m_cliPaths;
}
