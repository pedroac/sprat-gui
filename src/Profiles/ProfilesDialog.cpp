#include "ProfilesDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QVBoxLayout>

namespace {
SpratProfile makeDefaultProfile(const QString& name) {
    SpratProfile p;
    p.name = name;
    p.mode = "compact";
    p.optimize = "gpu";
    p.maxWidth = -1;
    p.maxHeight = -1;
    p.padding = 0;
    p.maxCombinations = 0;
    p.scale = 1.0;
    p.trimTransparent = true;
    return p;
}
}

ProfilesDialog::ProfilesDialog(const QVector<SpratProfile>& profiles, QWidget* parent)
    : QDialog(parent),
      m_profiles(profiles) {
    setWindowTitle(tr("Manage Profiles"));
    resize(760, 420);

    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    QHBoxLayout* contentLayout = new QHBoxLayout();

    QVBoxLayout* listLayout = new QVBoxLayout();
    m_listWidget = new QListWidget(this);
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    for (const SpratProfile& profile : m_profiles) {
        m_listWidget->addItem(profile.name);
    }
    listLayout->addWidget(m_listWidget);

    QHBoxLayout* listActionsLayout = new QHBoxLayout();
    m_addButton = new QPushButton(tr("Add"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    listActionsLayout->addWidget(m_addButton);
    listActionsLayout->addWidget(m_removeButton);
    listActionsLayout->addStretch();
    listLayout->addLayout(listActionsLayout);

    contentLayout->addLayout(listLayout, 1);

    QGroupBox* detailsGroup = new QGroupBox(tr("Profile Rules"), this);
    QFormLayout* detailsLayout = new QFormLayout(detailsGroup);

    m_nameEdit = new QLineEdit(this);
    detailsLayout->addRow(tr("Name:"), m_nameEdit);

    m_modeCombo = new QComboBox(this);
    m_modeCombo->setEditable(true);
    m_modeCombo->addItems({"compact", "pot", "fast"});
    detailsLayout->addRow(tr("Mode:"), m_modeCombo);

    m_optimizeCombo = new QComboBox(this);
    m_optimizeCombo->setEditable(true);
    m_optimizeCombo->addItems({"gpu", "space"});
    detailsLayout->addRow(tr("Optimize:"), m_optimizeCombo);

    QHBoxLayout* maxWidthLayout = new QHBoxLayout();
    m_useMaxWidthCheck = new QCheckBox(tr("Enable"), this);
    m_maxWidthSpin = new QSpinBox(this);
    m_maxWidthSpin->setRange(1, 16384);
    maxWidthLayout->addWidget(m_useMaxWidthCheck);
    maxWidthLayout->addWidget(m_maxWidthSpin);
    maxWidthLayout->addStretch();
    detailsLayout->addRow(tr("Max width:"), maxWidthLayout);

    QHBoxLayout* maxHeightLayout = new QHBoxLayout();
    m_useMaxHeightCheck = new QCheckBox(tr("Enable"), this);
    m_maxHeightSpin = new QSpinBox(this);
    m_maxHeightSpin->setRange(1, 16384);
    maxHeightLayout->addWidget(m_useMaxHeightCheck);
    maxHeightLayout->addWidget(m_maxHeightSpin);
    maxHeightLayout->addStretch();
    detailsLayout->addRow(tr("Max height:"), maxHeightLayout);

    m_paddingSpin = new QSpinBox(this);
    m_paddingSpin->setRange(0, 1024);
    detailsLayout->addRow(tr("Padding:"), m_paddingSpin);

    m_maxCombinationsSpin = new QSpinBox(this);
    m_maxCombinationsSpin->setRange(0, 10000000);
    detailsLayout->addRow(tr("Max combinations:"), m_maxCombinationsSpin);

    m_scaleSpin = new QDoubleSpinBox(this);
    m_scaleSpin->setDecimals(6);
    m_scaleSpin->setRange(0.000001, 1000000.0);
    m_scaleSpin->setValue(1.0);
    detailsLayout->addRow(tr("Scale:"), m_scaleSpin);

    m_trimTransparentCheck = new QCheckBox(tr("Enabled"), this);
    detailsLayout->addRow(tr("Trim transparent:"), m_trimTransparentCheck);

    contentLayout->addWidget(detailsGroup, 2);

    rootLayout->addLayout(contentLayout);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    rootLayout->addWidget(m_buttonBox);

    connect(m_addButton, &QPushButton::clicked, this, &ProfilesDialog::onAddProfile);
    connect(m_removeButton, &QPushButton::clicked, this, &ProfilesDialog::onRemoveProfile);
    connect(m_listWidget, &QListWidget::currentRowChanged, this, &ProfilesDialog::onCurrentProfileChanged);
    connect(m_useMaxWidthCheck, &QCheckBox::toggled, m_maxWidthSpin, &QSpinBox::setEnabled);
    connect(m_useMaxHeightCheck, &QCheckBox::toggled, m_maxHeightSpin, &QSpinBox::setEnabled);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &ProfilesDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
    } else {
        loadEditorsFromProfile(-1);
        m_removeButton->setEnabled(false);
    }
}

QVector<SpratProfile> ProfilesDialog::profiles() const {
    return m_profiles;
}

void ProfilesDialog::onAddProfile() {
    const int selected = m_listWidget->currentRow();
    if (selected >= 0) {
        saveEditorsToProfile(selected);
    }

    const QString name = uniqueProfileName("new-profile");
    const SpratProfile profile = makeDefaultProfile(name);
    m_profiles.append(profile);
    m_listWidget->addItem(profile.name);
    m_listWidget->setCurrentRow(m_profiles.size() - 1);
}

void ProfilesDialog::onRemoveProfile() {
    const int row = m_listWidget->currentRow();
    if (row < 0) {
        return;
    }

    m_profiles.removeAt(row);
    delete m_listWidget->takeItem(row);

    if (m_profiles.isEmpty()) {
        m_currentRow = -1;
        loadEditorsFromProfile(-1);
        m_removeButton->setEnabled(false);
        return;
    }
    m_listWidget->setCurrentRow(qMin(row, m_profiles.size() - 1));
}

void ProfilesDialog::onCurrentProfileChanged(int row) {
    if (m_updatingEditors) {
        return;
    }

    saveEditorsToProfile(m_currentRow);
    loadEditorsFromProfile(row);
    m_currentRow = row;
    m_removeButton->setEnabled(m_profiles.size() > 1);
}

void ProfilesDialog::accept() {
    saveEditorsToProfile(m_currentRow);

    for (int i = 0; i < m_profiles.size(); ++i) {
        const QString name = m_profiles[i].name.trimmed();
        if (name.isEmpty()) {
            QMessageBox::warning(this, tr("Invalid profile"), tr("Profile name cannot be empty."));
            m_listWidget->setCurrentRow(i);
            return;
        }
        if (hasDuplicateName(name, i)) {
            QMessageBox::warning(this, tr("Duplicate profile"), tr("A profile with this name already exists."));
            m_listWidget->setCurrentRow(i);
            return;
        }
        if (m_profiles[i].mode.trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("Invalid profile"), tr("Mode cannot be empty."));
            m_listWidget->setCurrentRow(i);
            return;
        }
        if (m_profiles[i].optimize.trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("Invalid profile"), tr("Optimize cannot be empty."));
            m_listWidget->setCurrentRow(i);
            return;
        }
    }

    QDialog::accept();
}

bool ProfilesDialog::hasDuplicateName(const QString& name, int exceptRow) const {
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (i == exceptRow) {
            continue;
        }
        if (m_profiles[i].name.compare(name, Qt::CaseSensitive) == 0) {
            return true;
        }
    }
    return false;
}

void ProfilesDialog::saveEditorsToProfile(int row) {
    if (row < 0 || row >= m_profiles.size() || m_updatingEditors) {
        return;
    }

    SpratProfile& p = m_profiles[row];
    p.name = m_nameEdit->text().trimmed();
    p.mode = m_modeCombo->currentText().trimmed();
    p.optimize = m_optimizeCombo->currentText().trimmed();
    p.maxWidth = m_useMaxWidthCheck->isChecked() ? m_maxWidthSpin->value() : -1;
    p.maxHeight = m_useMaxHeightCheck->isChecked() ? m_maxHeightSpin->value() : -1;
    p.padding = m_paddingSpin->value();
    p.maxCombinations = m_maxCombinationsSpin->value();
    p.scale = m_scaleSpin->value();
    p.trimTransparent = m_trimTransparentCheck->isChecked();

    const QString displayName = p.name.isEmpty() ? tr("<unnamed>") : p.name;
    if (QListWidgetItem* item = m_listWidget->item(row)) {
        item->setText(displayName);
    }
}

void ProfilesDialog::loadEditorsFromProfile(int row) {
    m_updatingEditors = true;

    const bool valid = row >= 0 && row < m_profiles.size();
    m_nameEdit->setEnabled(valid);
    m_modeCombo->setEnabled(valid);
    m_optimizeCombo->setEnabled(valid);
    m_useMaxWidthCheck->setEnabled(valid);
    m_maxWidthSpin->setEnabled(valid && m_useMaxWidthCheck->isChecked());
    m_useMaxHeightCheck->setEnabled(valid);
    m_maxHeightSpin->setEnabled(valid && m_useMaxHeightCheck->isChecked());
    m_paddingSpin->setEnabled(valid);
    m_maxCombinationsSpin->setEnabled(valid);
    m_scaleSpin->setEnabled(valid);
    m_trimTransparentCheck->setEnabled(valid);

    if (!valid) {
        m_nameEdit->clear();
        m_modeCombo->setCurrentText("compact");
        m_optimizeCombo->setCurrentText("gpu");
        m_useMaxWidthCheck->setChecked(false);
        m_maxWidthSpin->setValue(1024);
        m_useMaxHeightCheck->setChecked(false);
        m_maxHeightSpin->setValue(1024);
        m_paddingSpin->setValue(0);
        m_maxCombinationsSpin->setValue(0);
        m_scaleSpin->setValue(1.0);
        m_trimTransparentCheck->setChecked(true);
        m_updatingEditors = false;
        return;
    }

    const SpratProfile& p = m_profiles[row];
    m_nameEdit->setText(p.name);
    m_modeCombo->setCurrentText(p.mode);
    m_optimizeCombo->setCurrentText(p.optimize);

    const bool hasMaxWidth = p.maxWidth > 0;
    m_useMaxWidthCheck->setChecked(hasMaxWidth);
    m_maxWidthSpin->setValue(hasMaxWidth ? p.maxWidth : 1024);
    m_maxWidthSpin->setEnabled(hasMaxWidth);

    const bool hasMaxHeight = p.maxHeight > 0;
    m_useMaxHeightCheck->setChecked(hasMaxHeight);
    m_maxHeightSpin->setValue(hasMaxHeight ? p.maxHeight : 1024);
    m_maxHeightSpin->setEnabled(hasMaxHeight);

    m_paddingSpin->setValue(p.padding);
    m_maxCombinationsSpin->setValue(p.maxCombinations);
    m_scaleSpin->setValue(p.scale);
    m_trimTransparentCheck->setChecked(p.trimTransparent);

    m_updatingEditors = false;
}

QString ProfilesDialog::uniqueProfileName(const QString& base) const {
    QString candidate = base;
    int suffix = 1;
    while (true) {
        bool exists = false;
        for (const SpratProfile& p : m_profiles) {
            if (p.name == candidate) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            return candidate;
        }
        candidate = QString("%1-%2").arg(base).arg(suffix++);
    }
}
