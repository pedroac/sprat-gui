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

SettingsDialog::SettingsDialog(const AppSettings& settings, const CliPaths& cliPaths, QWidget* parent)
    : QDialog(parent), m_settings(settings), m_cliPaths(cliPaths) {
    setupUi();
}

void SettingsDialog::setupUi() {
    setWindowTitle("Settings");
    QVBoxLayout* layout = new QVBoxLayout(this);
    QFormLayout* form = new QFormLayout();

    m_canvasColorBtn = createColorButton(m_settings.canvasColor);
    connect(m_canvasColorBtn, &QPushButton::clicked, this, [this]() { pickColor(m_canvasColorBtn, m_settings.canvasColor); });
    form->addRow("Canvas Background:", m_canvasColorBtn);

    m_frameColorBtn = createColorButton(m_settings.frameColor);
    connect(m_frameColorBtn, &QPushButton::clicked, this, [this]() { pickColor(m_frameColorBtn, m_settings.frameColor); });
    form->addRow("Frame Background:", m_frameColorBtn);

    m_borderColorBtn = createColorButton(m_settings.borderColor);
    connect(m_borderColorBtn, &QPushButton::clicked, this, [this]() { pickColor(m_borderColorBtn, m_settings.borderColor); });
    form->addRow("Border Color:", m_borderColorBtn);

    m_borderStyleCombo = new QComboBox(this);
    m_borderStyleCombo->addItem("None", (int)Qt::NoPen);
    m_borderStyleCombo->addItem("Solid", (int)Qt::SolidLine);
    m_borderStyleCombo->addItem("Dash", (int)Qt::DashLine);
    m_borderStyleCombo->addItem("Dot", (int)Qt::DotLine);
    m_borderStyleCombo->addItem("DashDot", (int)Qt::DashDotLine);
    m_borderStyleCombo->addItem("DashDotDot", (int)Qt::DashDotDotLine);

    int index = m_borderStyleCombo->findData((int)m_settings.borderStyle);
    if (index >= 0) {
        m_borderStyleCombo->setCurrentIndex(index);
    }

    form->addRow("Border Style:", m_borderStyleCombo);
    layout->addLayout(form);

    QGroupBox* cliGroup = new QGroupBox("CLI Tools", this);
    QVBoxLayout* cliGroupLayout = new QVBoxLayout(cliGroup);
    QFormLayout* cliForm = new QFormLayout();

    m_layoutPathEdit = new QLineEdit(m_cliPaths.layoutBinary, this);
    QPushButton* layoutBrowse = new QPushButton("Browse", this);
    connect(layoutBrowse, &QPushButton::clicked, this, [this]() { browseCliBinary(m_layoutPathEdit); });
    cliForm->addRow("spratlayout:", createCliPathWidget(m_layoutPathEdit, layoutBrowse));

    m_packPathEdit = new QLineEdit(m_cliPaths.packBinary, this);
    QPushButton* packBrowse = new QPushButton("Browse", this);
    connect(packBrowse, &QPushButton::clicked, this, [this]() { browseCliBinary(m_packPathEdit); });
    cliForm->addRow("spratpack:", createCliPathWidget(m_packPathEdit, packBrowse));

    m_convertPathEdit = new QLineEdit(m_cliPaths.convertBinary, this);
    QPushButton* convertBrowse = new QPushButton("Browse", this);
    connect(convertBrowse, &QPushButton::clicked, this, [this]() { browseCliBinary(m_convertPathEdit); });
    cliForm->addRow("spratconvert:", createCliPathWidget(m_convertPathEdit, convertBrowse));

    cliGroupLayout->addLayout(cliForm);
    m_installCliBtn = new QPushButton("Install CLI Tools", this);
    connect(m_installCliBtn, &QPushButton::clicked, this, &SettingsDialog::installCliToolsRequested);
    cliGroupLayout->addWidget(m_installCliBtn, 0, Qt::AlignLeft);
    layout->addWidget(cliGroup);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
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
    QColor newColor = QColorDialog::getColor(color, this, "Select Color");
    if (newColor.isValid()) {
        color = newColor;
        updateColorButton(btn, color);
    }
}

AppSettings SettingsDialog::getSettings() const {
    AppSettings s = m_settings;
    s.borderStyle = (Qt::PenStyle)m_borderStyleCombo->currentData().toInt();
    return s;
}

CliPaths SettingsDialog::getCliPaths() const {
    CliPaths paths;
    paths.layoutBinary = m_layoutPathEdit->text().trimmed();
    paths.packBinary = m_packPathEdit->text().trimmed();
    paths.convertBinary = m_convertPathEdit->text().trimmed();
    return paths;
}

QWidget* SettingsDialog::createCliPathWidget(QLineEdit* edit, QPushButton* btn) {
    QWidget* container = new QWidget(this);
    QHBoxLayout* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(edit);
    layout->addWidget(btn);
    return container;
}

void SettingsDialog::browseCliBinary(QLineEdit* target) {
    QString path = QFileDialog::getOpenFileName(this, "Select CLI Binary", QString(), "Executable Files (*)");
    if (!path.isEmpty()) {
        target->setText(path);
    }
}
