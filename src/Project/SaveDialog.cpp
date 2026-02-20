#include "SaveDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QMessageBox>

namespace {
QStringList uniqueProfileNames(const QVector<SpratProfile>& profiles) {
    QStringList names;
    for (const SpratProfile& profile : profiles) {
        const QString trimmedName = profile.name.trimmed();
        if (!trimmedName.isEmpty() && !names.contains(trimmedName)) {
            names.append(trimmedName);
        }
    }
    return names;
}
}

SaveDialog::SaveDialog(const QString& defaultPath,
                       const QVector<SpratProfile>& availableProfiles,
                       const QString& selectedProfileName,
                       QWidget* parent)
    : QDialog(parent) {
    setupUi();
    m_destEdit->setText(defaultPath);

    QStringList profileNames = uniqueProfileNames(availableProfiles);
    if (profileNames.isEmpty()) {
        const QString fallbackProfile = selectedProfileName.trimmed().isEmpty() ? tr("default") : selectedProfileName.trimmed();
        profileNames.append(fallbackProfile);
    }
    for (const QString& profileName : profileNames) {
        QCheckBox* checkBox = new QCheckBox(profileName, this);
        checkBox->setChecked(profileName == selectedProfileName);
        connect(checkBox, &QCheckBox::toggled, this, [this]() { updateProfileSelectionState(); });
        m_profilesLayout->addWidget(checkBox);
        m_profileChecks.append(checkBox);
    }

    if (!m_profileChecks.isEmpty() && selectedProfileName.trimmed().isEmpty()) {
        m_profileChecks.first()->setChecked(true);
    }
    updateProfileSelectionState();
}

void SaveDialog::setupUi() {
    setWindowTitle(tr("Save Spritesheet"));
    resize(600, 400);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Destination
    QGroupBox* destGroup = new QGroupBox(tr("Destination"), this);
    QVBoxLayout* destLayout = new QVBoxLayout(destGroup);
    QHBoxLayout* destRow = new QHBoxLayout();
    m_destEdit = new QLineEdit(this);
    destRow->addWidget(m_destEdit);
    
    QPushButton* browseFolderBtn = new QPushButton(tr("Folder..."), this);
    connect(browseFolderBtn, &QPushButton::clicked, this, &SaveDialog::onBrowseFolder);
    destRow->addWidget(browseFolderBtn);
    
    QPushButton* browseFileBtn = new QPushButton(tr("File..."), this);
    connect(browseFileBtn, &QPushButton::clicked, this, &SaveDialog::onBrowseFile);
    destRow->addWidget(browseFileBtn);
    
    destLayout->addLayout(destRow);
    mainLayout->addWidget(destGroup);
    
    // Options
    QGroupBox* optsGroup = new QGroupBox(tr("Options"), this);
    QFormLayout* optsLayout = new QFormLayout(optsGroup);
    m_transformCombo = new QComboBox(this);
    m_transformCombo->addItems({"none", "json", "csv", "xml", "css"});
    m_transformCombo->setCurrentText("json");
    optsLayout->addRow(tr("Format (transform):"), m_transformCombo);
    mainLayout->addWidget(optsGroup);
    
    // Profiles
    QGroupBox* profilesGroup = new QGroupBox(tr("Profiles"), this);
    m_profilesLayout = new QVBoxLayout(profilesGroup);
    mainLayout->addWidget(profilesGroup);
    
    // Buttons
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        int selectedCount = 0;
        for (QCheckBox* checkBox : m_profileChecks) {
            if (checkBox && checkBox->isChecked()) {
                ++selectedCount;
            }
        }
        if (selectedCount == 0) {
            QMessageBox::warning(this, tr("Missing profile"), tr("Select at least one profile to save."));
            return;
        }
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

/**
 * @brief Opens a directory picker for the destination.
 */
void SaveDialog::onBrowseFolder() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Destination Folder"), m_destEdit->text());
    if (!dir.isEmpty()) {
        m_destEdit->setText(dir);
    }
}

/**
 * @brief Opens a file picker for the destination (zip).
 */
void SaveDialog::onBrowseFile() {
    QString file = QFileDialog::getSaveFileName(this, tr("Select Destination File"), m_destEdit->text(), tr("Zip Files (*.zip)"));
    if (!file.isEmpty()) {
        if (!file.endsWith(".zip", Qt::CaseInsensitive)) {
            file += ".zip";
        }
        m_destEdit->setText(file);
    }
}

/**
 * @brief Retrieves the configuration entered by the user.
 */
SaveConfig SaveDialog::getConfig() const {
    SaveConfig config;
    config.destination = m_destEdit->text();
    config.transform = m_transformCombo->currentText();

    for (QCheckBox* checkBox : m_profileChecks) {
        if (checkBox && checkBox->isChecked()) {
            config.profiles.append(checkBox->text().trimmed());
        }
    }
    return config;
}

void SaveDialog::updateProfileSelectionState() {
    int selectedCount = 0;
    int lastSelectedIndex = -1;
    for (int i = 0; i < m_profileChecks.size(); ++i) {
        QCheckBox* checkBox = m_profileChecks[i];
        if (!checkBox) {
            continue;
        }
        if (checkBox->isChecked()) {
            ++selectedCount;
            lastSelectedIndex = i;
        }
    }

    for (int i = 0; i < m_profileChecks.size(); ++i) {
        QCheckBox* checkBox = m_profileChecks[i];
        if (!checkBox) {
            continue;
        }
        const bool isOnlySelected = (selectedCount == 1 && i == lastSelectedIndex);
        checkBox->setEnabled(!isOnlySelected);
    }
}
