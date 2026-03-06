#include "ProfilesDialog.h"
#include "ResolutionsConfig.h"
#include "ResolutionUtils.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QVBoxLayout>

namespace {
constexpr auto kTargetSameAsSourceValue = "same_source";

SpratProfile makeDefaultProfile(const QString& name) {
    SpratProfile p;
    p.name = name;
    p.mode = "compact";
    p.optimize = "gpu";
    p.maxWidth = -1;
    p.maxHeight = -1;
    p.targetResolutionWidth = 1024;
    p.targetResolutionHeight = 1024;
    p.targetResolutionUseSource = false;
    p.resolutionReference = "largest";
    p.padding = 0;
    p.extrude = 0;
    p.maxCombinations = 0;
    p.threads = 0;
    p.trimTransparent = true;
    p.allowRotation = false;
    p.scale = 1.0;
    p.multipack = false;
    p.sort = "name";
    return p;
}

bool isCompactMode(const QString& mode) {
    return mode.trimmed().compare("compact", Qt::CaseInsensitive) == 0;
}

}

ProfilesDialog::ProfilesDialog(const QVector<SpratProfile>& profiles, QWidget* parent)
    : QDialog(parent),
      m_profiles(profiles) {
    setWindowTitle(tr("Manage Profiles"));
    resize(850, 550);

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

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget* detailsWidget = new QWidget(this);
    QVBoxLayout* detailsScrollLayout = new QVBoxLayout(detailsWidget);
    detailsScrollLayout->setContentsMargins(0, 0, 5, 0);

    // === Group 1: General ===
    QGroupBox* generalGroup = new QGroupBox(tr("General"), this);
    QFormLayout* generalLayout = new QFormLayout(generalGroup);

    m_nameEdit = new QLineEdit(this);
    generalLayout->addRow(tr("Name:"), m_nameEdit);

    m_modeCombo = new QComboBox(this);
    m_modeCombo->setEditable(true);
    m_modeCombo->addItems({"compact", "pot", "fast"});
    generalLayout->addRow(tr("Mode:"), m_modeCombo);

    m_optimizeCombo = new QComboBox(this);
    m_optimizeCombo->setEditable(true);
    m_optimizeCombo->addItems({"gpu", "space"});
    generalLayout->addRow(tr("Optimize:"), m_optimizeCombo);

    m_scaleSpin = new QDoubleSpinBox(this);
    m_scaleSpin->setRange(0.01, 1.0);
    m_scaleSpin->setSingleStep(0.05);
    m_scaleSpin->setValue(1.0);
    generalLayout->addRow(tr("Scale:"), m_scaleSpin);

    m_sortCombo = new QComboBox(this);
    m_sortCombo->addItem(tr("By filename"), "name");
    m_sortCombo->addItem(tr("None"), "none");
    generalLayout->addRow(tr("Sort:"), m_sortCombo);

    detailsScrollLayout->addWidget(generalGroup);

    // === Group 2: Layout & Geometry ===
    QGroupBox* layoutGroup = new QGroupBox(tr("Layout & Geometry"), this);
    QFormLayout* layoutLayout = new QFormLayout(layoutGroup);

    QHBoxLayout* maxWidthLayout = new QHBoxLayout();
    m_useMaxWidthCheck = new QCheckBox(tr("Enable"), this);
    m_maxWidthSpin = new QSpinBox(this);
    m_maxWidthSpin->setRange(1, 16384);
    maxWidthLayout->addWidget(m_useMaxWidthCheck);
    maxWidthLayout->addWidget(m_maxWidthSpin);
    maxWidthLayout->addStretch();
    layoutLayout->addRow(tr("Max width:"), maxWidthLayout);

    QHBoxLayout* maxHeightLayout = new QHBoxLayout();
    m_useMaxHeightCheck = new QCheckBox(tr("Enable"), this);
    m_maxHeightSpin = new QSpinBox(this);
    m_maxHeightSpin->setRange(1, 16384);
    maxHeightLayout->addWidget(m_useMaxHeightCheck);
    maxHeightLayout->addWidget(m_maxHeightSpin);
    maxHeightLayout->addStretch();
    layoutLayout->addRow(tr("Max height:"), maxHeightLayout);

    QHBoxLayout* targetResolutionLayout = new QHBoxLayout();
    m_targetResolutionCombo = new QComboBox(this);
    m_targetResolutionCombo->addItem(tr("Same as source"), QString::fromLatin1(kTargetSameAsSourceValue));
    m_targetResolutionCombo->addItems(ResolutionsConfig::loadResolutionOptions());
    if (m_targetResolutionCombo->count() == 1) {
        m_targetResolutionCombo->addItem("1024x768");
    }
    targetResolutionLayout->addWidget(m_targetResolutionCombo);
    targetResolutionLayout->addStretch();
    layoutLayout->addRow(tr("Target resolution:"), targetResolutionLayout);

    m_resolutionReferenceCombo = new QComboBox(this);
    m_resolutionReferenceCombo->addItems({"largest", "smallest"});
    layoutLayout->addRow(tr("Resolution reference:"), m_resolutionReferenceCombo);

    m_paddingSpin = new QSpinBox(this);
    m_paddingSpin->setRange(0, 1024);
    layoutLayout->addRow(tr("Padding:"), m_paddingSpin);

    m_extrudeSpin = new QSpinBox(this);
    m_extrudeSpin->setRange(0, 512);
    layoutLayout->addRow(tr("Extrude:"), m_extrudeSpin);

    m_trimTransparentCheck = new QCheckBox(tr("Enabled"), this);
    layoutLayout->addRow(tr("Trim transparent:"), m_trimTransparentCheck);

    m_allowRotationCheck = new QCheckBox(tr("Enabled"), this);
    layoutLayout->addRow(tr("Allow rotation:"), m_allowRotationCheck);

    m_multipackCheck = new QCheckBox(tr("Enabled"), this);
    layoutLayout->addRow(tr("Multipack:"), m_multipackCheck);

    detailsScrollLayout->addWidget(layoutGroup);

    // === Group 3: Advanced ===
    QGroupBox* advancedGroup = new QGroupBox(tr("Advanced"), this);
    QFormLayout* advancedLayout = new QFormLayout(advancedGroup);

    m_maxCombinationsSpin = new QSpinBox(this);
    m_maxCombinationsSpin->setRange(0, 10000000);
    advancedLayout->addRow(tr("Max combinations:"), m_maxCombinationsSpin);

    QHBoxLayout* threadsLayout = new QHBoxLayout();
    m_useThreadsCheck = new QCheckBox(tr("Enable"), this);
    m_threadsSpin = new QSpinBox(this);
    m_threadsSpin->setRange(1, 256);
    threadsLayout->addWidget(m_useThreadsCheck);
    threadsLayout->addWidget(m_threadsSpin);
    threadsLayout->addStretch();
    advancedLayout->addRow(tr("Threads:"), threadsLayout);

    detailsScrollLayout->addWidget(advancedGroup);
    detailsScrollLayout->addStretch();

    scrollArea->setWidget(detailsWidget);
    contentLayout->addWidget(scrollArea, 2);

    rootLayout->addLayout(contentLayout);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    rootLayout->addWidget(m_buttonBox);

    connect(m_addButton, &QPushButton::clicked, this, &ProfilesDialog::onAddProfile);
    connect(m_removeButton, &QPushButton::clicked, this, &ProfilesDialog::onRemoveProfile);
    connect(m_listWidget, &QListWidget::currentRowChanged, this, &ProfilesDialog::onCurrentProfileChanged);
    connect(m_useMaxWidthCheck, &QCheckBox::toggled, m_maxWidthSpin, &QSpinBox::setEnabled);
    connect(m_useMaxHeightCheck, &QCheckBox::toggled, m_maxHeightSpin, &QSpinBox::setEnabled);
    connect(m_useThreadsCheck, &QCheckBox::toggled, m_threadsSpin, &QSpinBox::setEnabled);
    connect(m_modeCombo, &QComboBox::currentTextChanged, this, [this](const QString&) {
        refreshMaxCombinationsEnabledState();
    });
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
    int targetWidth = 1024;
    int targetHeight = 768;
    p.targetResolutionUseSource = false;
    if (m_targetResolutionCombo && m_targetResolutionCombo->currentData().toString() == QLatin1String(kTargetSameAsSourceValue)) {
        p.targetResolutionUseSource = true;
    } else if (m_targetResolutionCombo) {
        parseResolutionText(m_targetResolutionCombo->currentText(), targetWidth, targetHeight);
    }
    p.targetResolutionWidth = targetWidth;
    p.targetResolutionHeight = targetHeight;
    p.resolutionReference = m_resolutionReferenceCombo->currentText().trimmed();
    p.padding = m_paddingSpin->value();
    p.extrude = m_extrudeSpin->value();
    p.maxCombinations = m_maxCombinationsSpin->value();
    p.threads = m_useThreadsCheck->isChecked() ? m_threadsSpin->value() : 0;
    p.trimTransparent = m_trimTransparentCheck->isChecked();
    p.allowRotation = m_allowRotationCheck->isChecked();
    p.scale = m_scaleSpin->value();
    p.multipack = m_multipackCheck->isChecked();
    p.sort = m_sortCombo->currentData().toString();

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
    m_targetResolutionCombo->setEnabled(valid);
    m_resolutionReferenceCombo->setEnabled(valid);
    m_paddingSpin->setEnabled(valid);
    m_extrudeSpin->setEnabled(valid);
    m_maxCombinationsSpin->setEnabled(valid);
    m_useThreadsCheck->setEnabled(valid);
    m_threadsSpin->setEnabled(valid && m_useThreadsCheck->isChecked());
    m_trimTransparentCheck->setEnabled(valid);
    m_allowRotationCheck->setEnabled(valid);
    m_scaleSpin->setEnabled(valid);
    m_multipackCheck->setEnabled(valid);
    m_sortCombo->setEnabled(valid);

    if (!valid) {
        m_nameEdit->clear();
        m_modeCombo->setCurrentText("compact");
        m_optimizeCombo->setCurrentText("gpu");
        m_useMaxWidthCheck->setChecked(false);
        m_maxWidthSpin->setValue(1024);
        m_useMaxHeightCheck->setChecked(false);
        m_maxHeightSpin->setValue(1024);
        if (m_targetResolutionCombo->count() > 0) {
            m_targetResolutionCombo->setCurrentIndex(0);
        }
        m_resolutionReferenceCombo->setCurrentText("largest");
        m_paddingSpin->setValue(0);
        m_extrudeSpin->setValue(0);
        m_maxCombinationsSpin->setValue(0);
        m_useThreadsCheck->setChecked(false);
        m_threadsSpin->setValue(4);
        m_trimTransparentCheck->setChecked(true);
        m_allowRotationCheck->setChecked(false);
        m_scaleSpin->setValue(1.0);
        m_multipackCheck->setChecked(false);
        m_sortCombo->setCurrentIndex(0);
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

    if (m_targetResolutionCombo->count() > 0) {
        if (p.targetResolutionUseSource) {
            m_targetResolutionCombo->setCurrentIndex(0);
        } else {
            const QString targetResolution = formatResolutionText(
                p.targetResolutionWidth > 0 ? p.targetResolutionWidth : 1024,
                p.targetResolutionHeight > 0 ? p.targetResolutionHeight : 768);
            const int targetIndex = m_targetResolutionCombo->findText(targetResolution, Qt::MatchFixedString);
            m_targetResolutionCombo->setCurrentIndex(targetIndex >= 0 ? targetIndex : 0);
        }
    }
    m_targetResolutionCombo->setEnabled(valid);
    m_resolutionReferenceCombo->setCurrentText(p.resolutionReference);
    m_resolutionReferenceCombo->setEnabled(valid);

    m_paddingSpin->setValue(p.padding);
    m_extrudeSpin->setValue(p.extrude);
    m_maxCombinationsSpin->setValue(p.maxCombinations);
    const bool hasThreads = p.threads > 0;
    m_useThreadsCheck->setChecked(hasThreads);
    m_threadsSpin->setValue(hasThreads ? p.threads : 4);
    m_threadsSpin->setEnabled(hasThreads);
    m_trimTransparentCheck->setChecked(p.trimTransparent);
    m_allowRotationCheck->setChecked(p.allowRotation);
    m_scaleSpin->setValue(p.scale);
    m_multipackCheck->setChecked(p.multipack);
    const int sortIndex = m_sortCombo->findData(p.sort);
    m_sortCombo->setCurrentIndex(sortIndex >= 0 ? sortIndex : 0);

    m_updatingEditors = false;
    refreshMaxCombinationsEnabledState();
}

void ProfilesDialog::refreshMaxCombinationsEnabledState() {
    if (m_updatingEditors) {
        return;
    }
    m_maxCombinationsSpin->setEnabled(m_modeCombo->isEnabled() && isCompactMode(m_modeCombo->currentText()));
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
