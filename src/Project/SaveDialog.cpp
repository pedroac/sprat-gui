#include "SaveDialog.h"
#include "CliToolsConfig.h"
#include "MessageDialog.h"
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
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace {
struct TransformInfo {
    QString id;
    QString name;
};

QVector<TransformInfo> loadAvailableTransforms() {
    const CliPaths cliPaths = CliToolsConfig::loadCliPaths();

    QStringList searchDirs;

    // Ask spratconvert where it keeps its transforms
    const QString queriedDir = CliToolsConfig::queryTransformsDir(cliPaths.convertBinary);
    if (!queriedDir.isEmpty()) {
        searchDirs << queriedDir;
    }

    // Dev/app-bundle fallbacks only
    const QString appDir = QCoreApplication::applicationDirPath();
    searchDirs << QDir(appDir).filePath("transforms");
    searchDirs << QDir(appDir).filePath("bin/transforms");
    searchDirs << QDir(appDir).filePath("cli/transforms");

    for (const QString& dirPath : searchDirs) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;
        const QStringList files = dir.entryList({"*.transform"}, QDir::Files, QDir::Name);
        if (files.isEmpty()) continue;

        QVector<TransformInfo> result;
        for (const QString& fileName : files) {
            QFile file(dir.filePath(fileName));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            TransformInfo info;
            info.id = QFileInfo(fileName).completeBaseName().trimmed().toLower();
            bool inMeta = false;
            QTextStream in(&file);
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (line.compare("[meta]", Qt::CaseInsensitive) == 0) { inMeta = true; continue; }
                if (line.startsWith('[')) { inMeta = false; continue; }
                if (!inMeta) continue;
                const int eq = line.indexOf('=');
                if (eq <= 0) continue;
                const QString key = line.left(eq).trimmed().toLower();
                const QString value = line.mid(eq + 1).trimmed();
                if (key == "name") info.name = value;
            }
            if (info.name.isEmpty()) {
                info.name = info.id;
            }
            if (!info.id.isEmpty()) result.append(info);
        }
        if (!result.isEmpty()) return result;
    }
    return {};
}
}

SaveDialog::SaveDialog(const QString& defaultPath,
                       const QVector<SpratProfile>& availableProfiles,
                       const QString& selectedProfileName,
                       const SaveConfig& lastConfig,
                       QWidget* parent)
    : QDialog(parent) {
    setupUi();
    
    Q_UNUSED(defaultPath);

    if (!lastConfig.transform.isEmpty()) {
        const int idx = m_transformCombo->findData(lastConfig.transform);
        if (idx >= 0) m_transformCombo->setCurrentIndex(idx);
    }

    QStringList seenNames;
    const auto addProfileCheck = [&](const QString& name, const QString& label) {
        if (name.isEmpty() || seenNames.contains(name)) return;
        seenNames.append(name);
        const QString display = label.trimmed().isEmpty() ? name : label.trimmed();
        QCheckBox* checkBox = new QCheckBox(display, this);
        checkBox->setProperty("profileName", name);
        if (!lastConfig.profiles.isEmpty()) {
            checkBox->setChecked(lastConfig.profiles.contains(name));
        } else {
            checkBox->setChecked(name == selectedProfileName);
        }
        connect(checkBox, &QCheckBox::toggled, this, [this]() { updateProfileSelectionState(); });
        m_profilesLayout->addWidget(checkBox);
        m_profileChecks.append(checkBox);
    };

    for (const SpratProfile& profile : availableProfiles) {
        addProfileCheck(profile.name.trimmed(), profile.label);
    }
    if (seenNames.isEmpty()) {
        const QString fallback = selectedProfileName.trimmed().isEmpty() ? tr("default") : selectedProfileName.trimmed();
        addProfileCheck(fallback, QString());
    }

    if (lastConfig.profiles.isEmpty() && !m_profileChecks.isEmpty() && selectedProfileName.trimmed().isEmpty()) {
        m_profileChecks.first()->setChecked(true);
    }
    updateProfileSelectionState();
}

void SaveDialog::setupUi() {
    setWindowTitle(tr("Export Spritesheet"));
    resize(500, 300);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    auto* descLabel = new QLabel(tr("Choose which profiles to include and the output format."), this);
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // Options
    QGroupBox* optsGroup = new QGroupBox(tr("Options"), this);
    QFormLayout* optsLayout = new QFormLayout(optsGroup);
    m_transformCombo = new QComboBox(this);
    m_transformCombo->addItem(tr("None (no metadata)"), QStringLiteral("none"));
    const QVector<TransformInfo> transforms = loadAvailableTransforms();
    if (!transforms.isEmpty()) {
        for (const TransformInfo& t : transforms) {
            m_transformCombo->addItem(t.name, t.id);
        }
    } else {
        m_transformCombo->addItem(QStringLiteral("json"), QStringLiteral("json"));
        m_transformCombo->addItem(QStringLiteral("csv"), QStringLiteral("csv"));
        m_transformCombo->addItem(QStringLiteral("xml"), QStringLiteral("xml"));
        m_transformCombo->addItem(QStringLiteral("css"), QStringLiteral("css"));
    }
    {
        const int jsonIdx = m_transformCombo->findData(QStringLiteral("json"));
        m_transformCombo->setCurrentIndex(jsonIdx >= 0 ? jsonIdx : 0);
    }
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
            MessageDialog::warning(this, tr("Missing profile"), tr("Select at least one profile to save."));
            return;
        }
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

/**
 * @brief Retrieves the configuration entered by the user.
 */
SaveConfig SaveDialog::getConfig() const {
    SaveConfig config;
    config.transform = m_transformCombo->currentData().toString();

    for (QCheckBox* checkBox : m_profileChecks) {
        if (checkBox && checkBox->isChecked()) {
            const QString name = checkBox->property("profileName").toString();
            if (!name.isEmpty()) config.profiles.append(name);
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
