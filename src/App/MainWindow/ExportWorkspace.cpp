#include "ExportWorkspace.h"
#include "IAtlasViewport.h"
#include "ZoomableGraphicsView.h"
#include "CliToolsConfig.h"
#include "MessageDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QSplitter>
#include <QStyle>
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

    const QString queriedDir = CliToolsConfig::queryTransformsDir(cliPaths.convertBinary);
    if (!queriedDir.isEmpty()) {
        searchDirs << queriedDir;
    }

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

ExportWorkspace::ExportWorkspace(QWidget* parent)
    : QWidget(parent) {
    setupUi();
}

void ExportWorkspace::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    // Left pane: preview (viewport widget reparented here during export)
    m_previewPane = new QWidget(this);
    m_previewPane->setLayout(new QVBoxLayout(m_previewPane));
    m_previewPane->layout()->setContentsMargins(0, 0, 0, 0);
    m_previewPane->setMinimumWidth(200);
    splitter->addWidget(m_previewPane);

    // Right pane: profiles + export options
    auto* rightWidget = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(12, 12, 12, 12);
    rightLayout->setSpacing(8);
    rightWidget->setMinimumWidth(300);

    // Preview group
    auto* profilesGroup = new QGroupBox(tr("Preview"), rightWidget);
    auto* profilesLayout = new QVBoxLayout(profilesGroup);
    profilesLayout->setAlignment(Qt::AlignTop);

    // Atlas preview selector
    auto* previewAtlasRow = new QHBoxLayout();
    previewAtlasRow->addWidget(new QLabel(tr("Atlas:"), profilesGroup));
    m_previewAtlasCombo = new QComboBox(profilesGroup);
    m_previewAtlasCombo->setToolTip(tr("Atlas to preview"));
    connect(m_previewAtlasCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportWorkspace::onAnyComboChanged);
    previewAtlasRow->addWidget(m_previewAtlasCombo, 1);
    profilesLayout->addLayout(previewAtlasRow);

    auto* profileRow = new QHBoxLayout();
    profileRow->addWidget(new QLabel(tr("Profile:"), profilesGroup));
    m_profileCombo = new QComboBox(profilesGroup);
    m_profileCombo->setToolTip(tr("Select the output profile for this export"));
    connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportWorkspace::onAnyComboChanged);
    profileRow->addWidget(m_profileCombo, 1);
    profilesLayout->addLayout(profileRow);

    auto* zoomRow = new QHBoxLayout();
    zoomRow->addWidget(new QLabel(tr("Zoom:"), profilesGroup));
    m_zoomSpin = new QDoubleSpinBox(profilesGroup);
    m_zoomSpin->setRange(10.0, 1600.0);
    m_zoomSpin->setValue(100.0);
    m_zoomSpin->setSuffix("%");
    m_zoomSpin->setSingleStep(10.0);
    m_zoomSpin->setToolTip(tr("Zoom level for preview (Ctrl+±0/1 for hotkeys)"));
    zoomRow->addWidget(m_zoomSpin, 1);
    profilesLayout->addLayout(zoomRow);
    connect(m_zoomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ExportWorkspace::onZoomSpinChanged);
    rightLayout->addWidget(profilesGroup);

    // Export options group
    auto* exportGroup = new QGroupBox(tr("Export"), rightWidget);
    auto* exportLayout = new QVBoxLayout(exportGroup);
    exportLayout->setSpacing(8);

    // Output folder row
    auto* folderLabel = new QLabel(tr("Output folder:"), exportGroup);
    exportLayout->addWidget(folderLabel);

    auto* folderRow = new QHBoxLayout();
    m_outputPathEdit = new QLineEdit(exportGroup);
    m_outputPathEdit->setPlaceholderText(tr("Not set — choose a folder to enable Export"));
    m_outputPathEdit->setReadOnly(true);
    auto* browseBtn = new QPushButton(tr("Browse..."), exportGroup);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString start = m_outputPathEdit->text().isEmpty()
            ? QString()
            : m_outputPathEdit->text();
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Export Folder"), start);
        if (!dir.isEmpty()) {
            m_outputPathEdit->setText(dir);
            if (m_exportBtn) {
                m_exportBtn->setEnabled(true);
            }
        }
    });
    folderRow->addWidget(m_outputPathEdit, 1);
    folderRow->addWidget(browseBtn);
    exportLayout->addLayout(folderRow);

    // Format row
    auto* formatLabel = new QLabel(tr("Format:"), exportGroup);
    exportLayout->addWidget(formatLabel);

    m_transformCombo = new QComboBox(exportGroup);
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
    exportLayout->addWidget(m_transformCombo);

    // Scale filter row
    auto* scaleFilterLabel = new QLabel(tr("Scale filter:"), exportGroup);
    exportLayout->addWidget(scaleFilterLabel);

    m_scaleFilterCombo = new QComboBox(exportGroup);
    m_scaleFilterCombo->addItem(tr("Nearest (default)"), QStringLiteral("nearest"));
    m_scaleFilterCombo->addItem(tr("Bilinear"),          QStringLiteral("bilinear"));
    m_scaleFilterCombo->addItem(tr("Bicubic"),           QStringLiteral("bicubic"));
    m_scaleFilterCombo->addItem(tr("Mitchell"),          QStringLiteral("mitchell"));
    connect(m_scaleFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportWorkspace::onAnyComboChanged);
    exportLayout->addWidget(m_scaleFilterCombo);

    rightLayout->addWidget(exportGroup);

    // Button row immediately below the Export group
    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    auto* cancelBtn = new QPushButton(tr("Cancel"), rightWidget);
    cancelBtn->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));
    m_exportBtn = new QPushButton(tr("Export"), rightWidget);
    m_exportBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_exportBtn->setDefault(true);
    connect(cancelBtn, &QPushButton::clicked, this, &ExportWorkspace::cancelled);
    connect(m_exportBtn, &QPushButton::clicked, this, &ExportWorkspace::onExportClicked);
    buttonRow->addWidget(cancelBtn);
    buttonRow->addWidget(m_exportBtn);
    rightLayout->addLayout(buttonRow);
    rightLayout->addStretch();

    splitter->addWidget(rightWidget);
    splitter->setSizes({650, 350});

    mainLayout->addWidget(splitter);
}

void ExportWorkspace::setPreviewWidget(QWidget* preview) {
    if (!m_previewPane || !preview) return;
    // Remove and hide any widget currently occupying the pane
    if (QLayout* layout = m_previewPane->layout()) {
        while (QLayoutItem* item = layout->takeAt(0)) {
            if (QWidget* w = item->widget()) {
                w->hide();
                w->setParent(nullptr);
            }
            delete item;
        }
    }
    preview->setParent(m_previewPane);
    m_previewPane->layout()->addWidget(preview);
    preview->show();
}

void ExportWorkspace::clearPreviewWidget() {
    if (!m_previewPane) return;
    QLayout* layout = m_previewPane->layout();
    if (!layout) return;
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
        }
        delete item;
    }
}

void ExportWorkspace::setViewport(IAtlasViewport* viewport) {
    if (!viewport) return;

    // Disconnect the previous view's zoomChanged signal
    if (m_currentView)
        disconnect(m_currentView, &ZoomableGraphicsView::zoomChanged,
                   this, &ExportWorkspace::onViewZoomChanged);

    setPreviewWidget(viewport->widget());

    m_currentView = qobject_cast<ZoomableGraphicsView*>(viewport->widget());
    if (m_currentView) {
        connect(m_currentView, &ZoomableGraphicsView::zoomChanged,
                this, &ExportWorkspace::onViewZoomChanged);
        // Sync spinbox to the current view zoom without triggering onZoomSpinChanged
        m_zoomSpin->blockSignals(true);
        m_zoomSpin->setValue(m_currentView->zoom() * 100.0);
        m_zoomSpin->blockSignals(false);
    }
}

void ExportWorkspace::clearViewport() {
    if (m_currentView)
        disconnect(m_currentView, &ZoomableGraphicsView::zoomChanged,
                   this, &ExportWorkspace::onViewZoomChanged);
    m_currentView = nullptr;
    clearPreviewWidget();
}

void ExportWorkspace::onZoomSpinChanged(double value) {
    if (!m_currentView) return;
    m_currentView->setZoomManual(true);
    m_currentView->setZoom(value / 100.0);
}

void ExportWorkspace::onViewZoomChanged(double zoom) {
    m_zoomSpin->blockSignals(true);
    m_zoomSpin->setValue(zoom * 100.0);
    m_zoomSpin->blockSignals(false);
}

void ExportWorkspace::populate(const QVector<SpratProfile>& profiles,
                               const QString& selectedProfileName,
                               const SaveConfig& lastConfig,
                               const QString& startDir) {
    // Block signals to avoid spurious previewRefreshRequested during population
    m_profileCombo->blockSignals(true);
    if (m_scaleFilterCombo) m_scaleFilterCombo->blockSignals(true);

    // Repopulate profile combo
    m_profileCombo->clear();
    for (const SpratProfile& profile : profiles) {
        const QString name = profile.name.trimmed();
        if (name.isEmpty()) continue;
        const QString display = profile.label.trimmed().isEmpty() ? name : profile.label.trimmed();
        m_profileCombo->addItem(display, name);
    }
    if (m_profileCombo->count() == 0 && !selectedProfileName.trimmed().isEmpty()) {
        m_profileCombo->addItem(selectedProfileName.trimmed(), selectedProfileName.trimmed());
    }

    // Select profile: prefer lastConfig.profiles.first(), then selectedProfileName
    const QString preferredProfile = !lastConfig.profiles.isEmpty()
        ? lastConfig.profiles.first()
        : selectedProfileName.trimmed();
    if (!preferredProfile.isEmpty()) {
        const int idx = m_profileCombo->findData(preferredProfile);
        if (idx >= 0) m_profileCombo->setCurrentIndex(idx);
    }

    // Restore output path
    if (!lastConfig.outputPath.isEmpty()) {
        m_outputPathEdit->setText(lastConfig.outputPath);
    } else if (!startDir.isEmpty()) {
        m_outputPathEdit->setText(startDir);
    } else {
        m_outputPathEdit->clear();
    }

    // Restore transform
    if (!lastConfig.transform.isEmpty()) {
        const int idx = m_transformCombo->findData(lastConfig.transform);
        if (idx >= 0) m_transformCombo->setCurrentIndex(idx);
    }

    // Restore scale filter
    if (!lastConfig.scaleFilter.isEmpty()) {
        const int idx = m_scaleFilterCombo->findData(lastConfig.scaleFilter);
        if (idx >= 0) m_scaleFilterCombo->setCurrentIndex(idx);
    }

    m_profileCombo->blockSignals(false);
    if (m_scaleFilterCombo) m_scaleFilterCombo->blockSignals(false);
}

void ExportWorkspace::setAtlasNames(const QStringList& names, int activeSessionIndex,
                                    const QList<int>& sessionIndices) {
    if (!m_previewAtlasCombo) return;
    m_previewAtlasCombo->blockSignals(true);
    m_previewAtlasCombo->clear();
    for (int i = 0; i < names.size(); ++i) {
        const int sessionIdx = (i < sessionIndices.size()) ? sessionIndices[i] : i;
        m_previewAtlasCombo->addItem(names[i], sessionIdx);
    }
    const int selectIdx = m_previewAtlasCombo->findData(activeSessionIndex);
    m_previewAtlasCombo->setCurrentIndex(selectIdx >= 0 ? selectIdx : 0);
    m_previewAtlasCombo->blockSignals(false);
    m_previewAtlasCombo->setVisible(true);
    // Always notify so the canvas and preview pack are initialised from the
    // selected atlas, regardless of whether the index actually changed.
    onAnyComboChanged();
}

void ExportWorkspace::onAnyComboChanged() {
    const int atlasIndex = m_previewAtlasCombo ? m_previewAtlasCombo->currentData().toInt() : -1;
    const QString profile = m_profileCombo ? m_profileCombo->currentData().toString() : QString();
    const QString sf = m_scaleFilterCombo ? m_scaleFilterCombo->currentData().toString() : QString();
    emit previewRefreshRequested(atlasIndex, profile, sf);
}

SaveConfig ExportWorkspace::getConfig() const {
    SaveConfig config;
    if (m_outputPathEdit) {
        config.outputPath = m_outputPathEdit->text().trimmed();
    }
    if (m_transformCombo) {
        config.transform = m_transformCombo->currentData().toString();
    }
    if (m_scaleFilterCombo) {
        config.scaleFilter = m_scaleFilterCombo->currentData().toString();
    }
    if (m_profileCombo && m_profileCombo->count() > 0) {
        const QString name = m_profileCombo->currentData().toString();
        if (!name.isEmpty()) config.profiles.append(name);
    }
    return config;
}

void ExportWorkspace::onExportClicked() {
    if (m_outputPathEdit && m_outputPathEdit->text().trimmed().isEmpty()) {
        MessageDialog::warning(this, tr("Missing output folder"),
                               tr("Select an output folder before exporting."));
        return;
    }
    if (m_profileCombo->count() == 0) {
        MessageDialog::warning(this, tr("Missing profile"),
                               tr("No profiles configured. Add at least one profile to export."));
        return;
    }
    emit exportRequested(getConfig());
}
