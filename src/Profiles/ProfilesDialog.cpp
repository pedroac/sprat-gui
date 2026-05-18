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
    p.preset = "quality";
    p.maxWidth = -1;
    p.maxHeight = -1;
    p.targetResolutionWidth = 1024;
    p.targetResolutionHeight = 1024;
    p.targetResolutionUseSource = false;
    p.resolutionReference = "largest";
    p.padding = 0;
    p.extrude = 0;
    p.threads = 0;
    p.trimTransparent = true;
    p.allowRotation = false;
    p.scale = 1.0;
    p.multipack = false;
    p.sort = "name";
    p.gpuCompress = "";
    p.dilate = 0;
    return p;
}

bool isCompactPreset(const QString& preset) {
    const QString p = preset.trimmed().toLower();
    return p == "quality" || p == "small";
}

}

ProfilesDialog::ProfilesDialog(const QVector<SpratProfile>& profiles, QWidget* parent)
    : QDialog(parent),
      m_profiles(profiles) {
    setWindowTitle(tr("Manage Profiles"));
    resize(680, 550);

    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    QHBoxLayout* contentLayout = new QHBoxLayout();

    QVBoxLayout* listLayout = new QVBoxLayout();
    m_listWidget = new QListWidget(this);
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    for (const SpratProfile& profile : m_profiles) {
        m_listWidget->addItem(profile.label.isEmpty() ? profile.name : profile.label);
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

    m_labelEdit = new QLineEdit(this);
    m_labelEdit->setPlaceholderText(tr("Optional readable name shown in the UI"));
    generalLayout->addRow(tr("Label:"), m_labelEdit);

    m_presetCombo = new QComboBox(this);
    m_presetCombo->addItem(tr("Fast: quick packing"),    "fast");
    m_presetCombo->addItem(tr("Quality: balanced"),      "quality");
    m_presetCombo->addItem(tr("Small: minimize size"),   "small");
    m_presetCombo->addItem(tr("POT: power-of-two atlas"), "pot");
    generalLayout->addRow(tr("Preset:"), m_presetCombo);

    m_scaleSpin = new QDoubleSpinBox(this);
    m_scaleSpin->setRange(1, 100);
    m_scaleSpin->setSingleStep(5);
    m_scaleSpin->setDecimals(0);
    m_scaleSpin->setSuffix("%");
    m_scaleSpin->setValue(100);
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
    m_resolutionReferenceCombo->addItem(tr("Largest: biggest sprite fits"),   "largest");
    m_resolutionReferenceCombo->addItem(tr("Smallest: smallest sprite fits"), "smallest");
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

    m_threadsSpin = new QSpinBox(this);
    m_threadsSpin->setRange(0, 256);
    m_threadsSpin->setSpecialValueText(tr("Auto"));
    advancedLayout->addRow(tr("Threads:"), m_threadsSpin);

    detailsScrollLayout->addWidget(advancedGroup);

    // === Group 4: Output Processing ===
    QGroupBox* outputGroup = new QGroupBox(tr("Output Processing"), this);
    QFormLayout* outputLayout = new QFormLayout(outputGroup);

    m_gpuCompressCombo = new QComboBox(this);
    m_gpuCompressCombo->addItem(tr("None"), "");
    m_gpuCompressCombo->addItem(tr("DXT1 (RGB, no alpha)"), "dxt1");
    m_gpuCompressCombo->addItem(tr("DXT5 (RGBA)"), "dxt5");
    outputLayout->addRow(tr("GPU compression:"), m_gpuCompressCombo);

    m_dilateSpin = new QSpinBox(this);
    m_dilateSpin->setRange(0, 16);
    m_dilateSpin->setValue(0);
    outputLayout->addRow(tr("Dilate (artifact reduction):"), m_dilateSpin);

    detailsScrollLayout->addWidget(outputGroup);
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
    connect(m_presetCombo, &QComboBox::currentTextChanged, this, [this](const QString&) {
        refreshThreadsEnabledState();
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
    p.label = m_labelEdit->text().trimmed();
    p.preset = m_presetCombo->currentData().toString();
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
    p.resolutionReference = m_resolutionReferenceCombo->currentData().toString();
    p.padding = m_paddingSpin->value();
    p.extrude = m_extrudeSpin->value();
    p.threads = m_threadsSpin->value();
    p.trimTransparent = m_trimTransparentCheck->isChecked();
    p.allowRotation = m_allowRotationCheck->isChecked();
    p.scale = m_scaleSpin->value() / 100.0;
    p.multipack = m_multipackCheck->isChecked();
    p.sort = m_sortCombo->currentData().toString();
    p.gpuCompress = m_gpuCompressCombo->currentData().toString();
    p.dilate = m_dilateSpin->value();

    const QString displayName = !p.label.isEmpty() ? p.label
                              : !p.name.isEmpty()  ? p.name
                              : tr("<unnamed>");
    if (QListWidgetItem* item = m_listWidget->item(row)) {
        item->setText(displayName);
    }
}

void ProfilesDialog::loadEditorsFromProfile(int row) {
    m_updatingEditors = true;

    const bool valid = row >= 0 && row < m_profiles.size();
    m_nameEdit->setEnabled(valid);
    m_labelEdit->setEnabled(valid);
    m_presetCombo->setEnabled(valid);
    m_useMaxWidthCheck->setEnabled(valid);
    m_maxWidthSpin->setEnabled(valid && m_useMaxWidthCheck->isChecked());
    m_useMaxHeightCheck->setEnabled(valid);
    m_maxHeightSpin->setEnabled(valid && m_useMaxHeightCheck->isChecked());
    m_targetResolutionCombo->setEnabled(valid);
    m_resolutionReferenceCombo->setEnabled(valid);
    m_paddingSpin->setEnabled(valid);
    m_extrudeSpin->setEnabled(valid);
    m_threadsSpin->setEnabled(valid);
    m_trimTransparentCheck->setEnabled(valid);
    m_allowRotationCheck->setEnabled(valid);
    m_scaleSpin->setEnabled(valid);
    m_multipackCheck->setEnabled(valid);
    m_sortCombo->setEnabled(valid);
    m_gpuCompressCombo->setEnabled(valid);
    m_dilateSpin->setEnabled(valid);

    if (!valid) {
        m_nameEdit->clear();
        m_labelEdit->clear();
        m_presetCombo->setCurrentIndex(m_presetCombo->findData("quality"));
        m_useMaxWidthCheck->setChecked(false);
        m_maxWidthSpin->setValue(1024);
        m_useMaxHeightCheck->setChecked(false);
        m_maxHeightSpin->setValue(1024);
        if (m_targetResolutionCombo->count() > 0) {
            m_targetResolutionCombo->setCurrentIndex(0);
        }
        m_resolutionReferenceCombo->setCurrentIndex(m_resolutionReferenceCombo->findData("largest"));
        m_paddingSpin->setValue(0);
        m_extrudeSpin->setValue(0);
        m_threadsSpin->setValue(0);
        m_trimTransparentCheck->setChecked(true);
        m_allowRotationCheck->setChecked(false);
        m_scaleSpin->setValue(100);
        m_multipackCheck->setChecked(false);
        m_sortCombo->setCurrentIndex(0);
        m_gpuCompressCombo->setCurrentIndex(0);
        m_dilateSpin->setValue(0);
        m_updatingEditors = false;
        return;
    }

    const SpratProfile& p = m_profiles[row];
    m_nameEdit->setText(p.name);
    m_labelEdit->setText(p.label);
    m_presetCombo->setCurrentIndex(m_presetCombo->findData(p.preset));

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
    m_resolutionReferenceCombo->setCurrentIndex(m_resolutionReferenceCombo->findData(p.resolutionReference));
    m_resolutionReferenceCombo->setEnabled(valid);

    m_paddingSpin->setValue(p.padding);
    m_extrudeSpin->setValue(p.extrude);
    m_threadsSpin->setValue(p.threads);
    m_trimTransparentCheck->setChecked(p.trimTransparent);
    m_allowRotationCheck->setChecked(p.allowRotation);
    m_scaleSpin->setValue(p.scale * 100.0);
    m_multipackCheck->setChecked(p.multipack);
    const int sortIndex = m_sortCombo->findData(p.sort);
    m_sortCombo->setCurrentIndex(sortIndex >= 0 ? sortIndex : 0);

    const int gpuCompressIndex = m_gpuCompressCombo->findData(p.gpuCompress);
    m_gpuCompressCombo->setCurrentIndex(gpuCompressIndex >= 0 ? gpuCompressIndex : 0);
    m_dilateSpin->setValue(p.dilate);

    m_updatingEditors = false;
    refreshThreadsEnabledState();
}

void ProfilesDialog::refreshThreadsEnabledState() {
    if (m_updatingEditors) {
        return;
    }
    const bool enabled = m_presetCombo->isEnabled() && isCompactPreset(m_presetCombo->currentData().toString());
    m_threadsSpin->setEnabled(enabled);
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
