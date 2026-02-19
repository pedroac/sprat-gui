#include "SaveDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QIcon>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QList>
#include <QLayoutItem>

SaveDialog::SaveDialog(const QString& defaultPath, QWidget* parent) : QDialog(parent) {
    setupUi();
    m_destEdit->setText(defaultPath);
    addScaleRow("default", 1.0);
}

void SaveDialog::setupUi() {
    setWindowTitle("Save Spritesheet");
    resize(600, 400);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Destination
    QGroupBox* destGroup = new QGroupBox("Destination", this);
    QVBoxLayout* destLayout = new QVBoxLayout(destGroup);
    QHBoxLayout* destRow = new QHBoxLayout();
    m_destEdit = new QLineEdit(this);
    destRow->addWidget(m_destEdit);
    
    QPushButton* browseFolderBtn = new QPushButton("Folder...", this);
    connect(browseFolderBtn, &QPushButton::clicked, this, &SaveDialog::onBrowseFolder);
    destRow->addWidget(browseFolderBtn);
    
    QPushButton* browseFileBtn = new QPushButton("File...", this);
    connect(browseFileBtn, &QPushButton::clicked, this, &SaveDialog::onBrowseFile);
    destRow->addWidget(browseFileBtn);
    
    destLayout->addLayout(destRow);
    mainLayout->addWidget(destGroup);
    
    // Options
    QGroupBox* optsGroup = new QGroupBox("Options", this);
    QFormLayout* optsLayout = new QFormLayout(optsGroup);
    m_transformCombo = new QComboBox(this);
    m_transformCombo->addItems({"none", "json", "csv", "xml", "css"});
    m_transformCombo->setCurrentText("json");
    optsLayout->addRow("Format (transform):", m_transformCombo);
    mainLayout->addWidget(optsGroup);
    
    // Scales
    QGroupBox* scalesGroup = new QGroupBox("Scales", this);
    QVBoxLayout* scalesGroupLayout = new QVBoxLayout(scalesGroup);
    
    m_scalesLayout = new QVBoxLayout();
    scalesGroupLayout->addLayout(m_scalesLayout);
    
    QPushButton* addScaleBtn = new QPushButton(QIcon::fromTheme("list-add"), "Add Scale", this);
    connect(addScaleBtn, &QPushButton::clicked, this, &SaveDialog::onAddScale);
    scalesGroupLayout->addWidget(addScaleBtn);
    
    mainLayout->addWidget(scalesGroup);
    
    // Buttons
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

/**
 * @brief Opens a directory picker for the destination.
 */
void SaveDialog::onBrowseFolder() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Destination Folder", m_destEdit->text());
    if (!dir.isEmpty()) {
        m_destEdit->setText(dir);
    }
}

/**
 * @brief Opens a file picker for the destination (zip).
 */
void SaveDialog::onBrowseFile() {
    QString file = QFileDialog::getSaveFileName(this, "Select Destination File", m_destEdit->text(), "Zip Files (*.zip)");
    if (!file.isEmpty()) {
        if (!file.endsWith(".zip", Qt::CaseInsensitive)) {
            file += ".zip";
        }
        m_destEdit->setText(file);
    }
}

/**
 * @brief Adds a new scale configuration row.
 */
void SaveDialog::onAddScale() {
    addScaleRow(QString("scale_%1").arg(m_scalesLayout->count() + 1), 1.0);
}

void SaveDialog::addScaleRow(const QString& name, double value) {
    QWidget* rowWidget = new QWidget(this);
    QHBoxLayout* rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    
    QLineEdit* nameEdit = new QLineEdit(name, this);
    nameEdit->setPlaceholderText("name");
    rowLayout->addWidget(nameEdit);
    
    QDoubleSpinBox* spin = new QDoubleSpinBox(this);
    spin->setRange(0.01, 1.0);
    spin->setSingleStep(0.1);
    spin->setValue(value);
    rowLayout->addWidget(spin);
    
    QPushButton* removeBtn = new QPushButton(QIcon::fromTheme("list-remove"), "", this);
    if (removeBtn->icon().isNull()) {
        removeBtn->setIcon(QIcon::fromTheme("edit-delete"));
    }
    if (removeBtn->icon().isNull()) {
        removeBtn->setText("Del");
    }
    removeBtn->setToolTip("Remove scale");
    rowLayout->addWidget(removeBtn);
    
    m_scalesLayout->addWidget(rowWidget);
    
    connect(removeBtn, &QPushButton::clicked, this, [this, rowWidget](){
        if (m_scalesLayout->count() > 1) {
            m_scalesLayout->removeWidget(rowWidget);
            delete rowWidget;
            updateRemoveButtonsState();
        }
    });
    
    updateRemoveButtonsState();
}

void SaveDialog::updateRemoveButtonsState() {
    bool canRemove = m_scalesLayout->count() > 1;
    for (int i = 0; i < m_scalesLayout->count(); ++i) {
        QLayoutItem* item = m_scalesLayout->itemAt(i);
        if (item && item->widget()) {
            QList<QPushButton*> btns = item->widget()->findChildren<QPushButton*>();
            for (auto* btn : btns) {
                btn->setEnabled(canRemove);
            }
        }
    }
}

/**
 * @brief Retrieves the configuration entered by the user.
 */
SaveConfig SaveDialog::getConfig() const {
    SaveConfig config;
    config.destination = m_destEdit->text();
    config.transform = m_transformCombo->currentText();
    
    for (int i = 0; i < m_scalesLayout->count(); ++i) {
        QWidget* w = m_scalesLayout->itemAt(i)->widget();
        QLineEdit* nameEdit = w->findChild<QLineEdit*>();
        QDoubleSpinBox* spin = w->findChild<QDoubleSpinBox*>();
        if (nameEdit && spin) {
            config.scales.append({nameEdit->text(), spin->value()});
        }
    }
    return config;
}