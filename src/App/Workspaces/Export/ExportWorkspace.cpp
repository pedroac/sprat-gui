#include "ExportWorkspace.h"
#include "IAtlasViewport.h"
#include "ZoomableGraphicsView.h"
#include "CliToolsConfig.h"
#include "MessageDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
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
#include <QTableWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QInputDialog>
#include <QScrollArea>
#include <QSet>
#include <QDesktopServices>
#include <QUrl>

namespace {
QString formatFileSize(qint64 bytes) {
    if (bytes < 0)     return {};
    if (bytes < 1024)  return QString::number(bytes) + QLatin1String(" B");
    if (bytes < 1<<20) return QString::number(bytes / 1024.0, 'f', 1) + QLatin1String(" KB");
    return             QString::number(bytes / double(1<<20), 'f', 1) + QLatin1String(" MB");
}

struct TransformInfo {
    QString id;
    QString name;
};

const QVector<TransformInfo>& loadAvailableTransforms() {
    static const QVector<TransformInfo> cached = []() -> QVector<TransformInfo> {
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
    }();
    return cached;
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
    auto* previewPaneLayout = new QVBoxLayout(m_previewPane);
    previewPaneLayout->setContentsMargins(0, 0, 0, 0);
    m_previewPane->setMinimumWidth(200);

    m_noPreviewLabel = new QLabel(tr("No preview available"), m_previewPane);
    m_noPreviewLabel->setAlignment(Qt::AlignCenter);
    m_noPreviewLabel->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
    m_noPreviewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    previewPaneLayout->addWidget(m_noPreviewLabel);

    splitter->addWidget(m_previewPane);

    // Right pane: profiles + export options
    auto* rightWidget = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(12, 12, 12, 12);
    rightLayout->setSpacing(8);

    // --- Preset row at the very top ---
    {
        auto* presetRow = new QHBoxLayout();
        presetRow->addWidget(new QLabel(tr("Preset:"), rightWidget), 0, Qt::AlignVCenter);
        m_presetCombo = new QComboBox(rightWidget);
        m_presetCombo->setToolTip(tr("Load a saved export preset"));
        m_presetCombo->addItem(tr("— (no preset) —"), QString());
        presetRow->addWidget(m_presetCombo, 1, Qt::AlignVCenter);
        m_savePresetBtn = new QPushButton(tr("Save…"), rightWidget);
        m_savePresetBtn->setToolTip(tr("Save current settings as a named preset"));
        m_savePresetBtn->setEnabled(false);  // enabled once an output path is set
        m_delPresetBtn  = new QPushButton(tr("Delete"), rightWidget);
        m_delPresetBtn->setToolTip(tr("Delete selected preset"));
        m_delPresetBtn->setEnabled(false);
        presetRow->addWidget(m_savePresetBtn, 0, Qt::AlignVCenter);
        presetRow->addWidget(m_delPresetBtn, 0, Qt::AlignVCenter);
        rightLayout->addLayout(presetRow);

        connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &ExportWorkspace::onPresetComboChanged);

        connect(m_savePresetBtn, &QPushButton::clicked, this, [this]() {
            bool ok = false;
            QString name = QInputDialog::getText(this, tr("Save Export Preset"),
                tr("Preset name:"), QLineEdit::Normal, QString(), &ok);
            if (!ok || name.trimmed().isEmpty()) return;
            ExportPreset preset;
            preset.name               = name.trimmed();
            preset.outputPath         = m_outputPathEdit ? m_outputPathEdit->text().trimmed() : QString();
            preset.transform          = m_transformCombo ? m_transformCombo->currentData().toString() : QString();
            preset.scaleFilter        = m_scaleFilterCombo ? m_scaleFilterCombo->currentData().toString() : QString();
            preset.postExportCommand  = m_postExportCommandEdit ? m_postExportCommandEdit->text() : QString();
            if (m_profileCombo && m_profileCombo->count() > 0) {
                const QString prof = m_profileCombo->currentData().toString();
                if (!prof.isEmpty()) preset.profiles.append(prof);
            }
            emit savePresetRequested(preset);
        });

        connect(m_delPresetBtn, &QPushButton::clicked, this, [this]() {
            if (!m_presetCombo) return;
            const QString name = m_presetCombo->currentData().toString();
            if (name.isEmpty()) return;
            emit deletePresetRequested(name);
        });
    }

    // Preview group
    auto* profilesGroup = new QGroupBox(tr("Preview"), rightWidget);
    auto* profilesLayout = new QVBoxLayout(profilesGroup);
    profilesLayout->setAlignment(Qt::AlignTop);

    auto* previewGrid = new QGridLayout();
    previewGrid->setHorizontalSpacing(6);
    previewGrid->setVerticalSpacing(8);
    previewGrid->setColumnStretch(1, 1);

    // Row 0: Atlas
    m_previewAtlasCombo = new QComboBox(profilesGroup);
    m_previewAtlasCombo->setToolTip(tr("Atlas to preview"));
    connect(m_previewAtlasCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() {
        updatePreviewProfileCombo();
        onAnyComboChanged();
    });
    previewGrid->addWidget(new QLabel(tr("Atlas:"), profilesGroup), 0, 0, Qt::AlignVCenter);
    previewGrid->addWidget(m_previewAtlasCombo, 0, 1);

    // Row 1: Profile
    m_profileCombo = new QComboBox(profilesGroup);
    m_profileCombo->setToolTip(tr("Select the output profile for this export"));
    connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportWorkspace::onAnyComboChanged);
    previewGrid->addWidget(new QLabel(tr("Profile:"), profilesGroup), 1, 0, Qt::AlignVCenter);
    previewGrid->addWidget(m_profileCombo, 1, 1);

    // Row 2: Zoom
    m_zoomSpin = new QDoubleSpinBox(profilesGroup);
    m_zoomSpin->setRange(10.0, 1600.0);
    m_zoomSpin->setValue(100.0);
    m_zoomSpin->setSuffix("%");
    m_zoomSpin->setSingleStep(10.0);
    m_zoomSpin->setToolTip(tr("Zoom level for preview (Ctrl+±0/1 for hotkeys)"));
    connect(m_zoomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ExportWorkspace::onZoomSpinChanged);
    previewGrid->addWidget(new QLabel(tr("Zoom:"), profilesGroup), 2, 0, Qt::AlignVCenter);
    previewGrid->addWidget(m_zoomSpin, 2, 1);

    profilesLayout->addLayout(previewGrid);
    profilesGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    rightLayout->addWidget(profilesGroup);

    // Export options group
    auto* exportGroup = new QGroupBox(tr("Export"), rightWidget);
    auto* exportLayout = new QVBoxLayout(exportGroup);
    exportLayout->setSpacing(10);

    // Three-column grid so all labels share the same width and all fields
    // align on both the left and right edges:
    //   col 0 – labels (auto-width from the widest label in the section)
    //   col 1 – main field (stretch = 1)
    //   col 2 – Browse button (row 0 only; Format/Scale filter combos span cols 1–2)
    auto* exportGrid = new QGridLayout();
    exportGrid->setHorizontalSpacing(6);
    exportGrid->setVerticalSpacing(8);
    exportGrid->setColumnStretch(1, 1);

    // Row 0: Output folder
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
            if (m_exportBtn)     m_exportBtn->setEnabled(true);
            if (m_savePresetBtn) m_savePresetBtn->setEnabled(true);
        }
    });
    exportGrid->addWidget(new QLabel(tr("Output folder:"), exportGroup), 0, 0, Qt::AlignVCenter);
    exportGrid->addWidget(m_outputPathEdit, 0, 1);
    exportGrid->addWidget(browseBtn, 0, 2);

    // Row 1: Format — combo spans cols 1–2 to reach the same right edge as Post-export command
    m_transformCombo = new QComboBox(exportGroup);
    m_transformCombo->addItem(tr("Raw (sprat-cli format)"), QStringLiteral("raw"));
    // Transform list is populated lazily on first populate() call so the
    // constructor doesn't block waiting for a subprocess (queryTransformsDir).
    exportGrid->addWidget(new QLabel(tr("Format:"), exportGroup), 1, 0, Qt::AlignVCenter);
    exportGrid->addWidget(m_transformCombo, 1, 1, 1, 2);

    // Row 2: Scale filter — same span as Format so both combos share the same width
    m_scaleFilterCombo = new QComboBox(exportGroup);
    m_scaleFilterCombo->addItem(tr("Nearest (default)"), QStringLiteral("nearest"));
    m_scaleFilterCombo->addItem(tr("Bilinear"),          QStringLiteral("bilinear"));
    m_scaleFilterCombo->addItem(tr("Bicubic"),           QStringLiteral("bicubic"));
    m_scaleFilterCombo->addItem(tr("Mitchell"),          QStringLiteral("mitchell"));
    connect(m_scaleFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportWorkspace::onAnyComboChanged);
    exportGrid->addWidget(new QLabel(tr("Scale filter:"), exportGroup), 2, 0, Qt::AlignVCenter);
    exportGrid->addWidget(m_scaleFilterCombo, 2, 1, 1, 2);

    exportLayout->addLayout(exportGrid);

    // Post-export command row
    auto* hookLabel = new QLabel(tr("Post-export command:"), exportGroup);
    exportLayout->addWidget(hookLabel);

    m_postExportCommandEdit = new QLineEdit(exportGroup);
    m_postExportCommandEdit->setPlaceholderText(tr("Optional shell command (SPRAT_EXPORT_PATH is set to the output folder)"));
    m_postExportCommandEdit->setToolTip(
        tr("Shell command executed after a successful export.\n"
           "SPRAT_EXPORT_PATH is set to the export destination path."));
    exportLayout->addWidget(m_postExportCommandEdit);

    exportGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    rightLayout->addWidget(exportGroup);

    // --- Per-atlas overrides group ---
    m_atlasOverridesGroup = new QGroupBox(tr("Per-atlas overrides"), rightWidget);
    auto* overridesLayout = new QVBoxLayout(m_atlasOverridesGroup);
    m_atlasOverridesTable = new QTableWidget(0, 3, m_atlasOverridesGroup);
    m_atlasOverridesTable->setHorizontalHeaderLabels(
        {tr("Atlas"), tr("Format"), tr("Scale filter")});
    m_atlasOverridesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_atlasOverridesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_atlasOverridesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_atlasOverridesTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_atlasOverridesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_atlasOverridesTable->verticalHeader()->hide();
    overridesLayout->addWidget(m_atlasOverridesTable);
    m_atlasOverridesGroup->setVisible(false);
    rightLayout->addWidget(m_atlasOverridesGroup);

    // Button row immediately below the overrides group
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

    // Export log panel
    m_logGroup = new QGroupBox(tr("Last export"), rightWidget);
    auto* logLayout = new QVBoxLayout(m_logGroup);
    logLayout->setContentsMargins(6, 6, 6, 6);
    logLayout->setSpacing(4);

    m_logTree = new QTreeWidget(m_logGroup);
    m_logTree->setColumnCount(2);
    m_logTree->setHeaderLabels({tr("File"), tr("Size")});
    m_logTree->header()->setStretchLastSection(false);
    m_logTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_logTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_logTree->setRootIsDecorated(false);
    m_logTree->setSelectionMode(QAbstractItemView::NoSelection);
    m_logTree->setFocusPolicy(Qt::NoFocus);
    m_logTree->setMaximumHeight(140);
    logLayout->addWidget(m_logTree);

    m_logRevealBtn = new QPushButton(
        style()->standardIcon(QStyle::SP_DirOpenIcon), tr("Reveal in Files"), m_logGroup);
    m_logRevealBtn->setEnabled(false);
    connect(m_logRevealBtn, &QPushButton::clicked, this, [this]() {
        if (m_logDestination.isEmpty()) return;
        const QFileInfo fi(m_logDestination);
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath()));
    });
    logLayout->addWidget(m_logRevealBtn);

    m_logGroup->setVisible(false);
    rightLayout->addWidget(m_logGroup);

    // Absorb any remaining vertical space so controls stay packed at the top
    rightLayout->addStretch(1);

    auto* rightScroll = new QScrollArea(this);
    rightScroll->setWidget(rightWidget);
    rightScroll->setWidgetResizable(true);
    rightScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rightScroll->setFrameShape(QFrame::NoFrame);
    rightScroll->setMinimumWidth(300);
    splitter->addWidget(rightScroll);
    splitter->setSizes({650, 350});

    mainLayout->addWidget(splitter);
}

void ExportWorkspace::setPreviewWidget(QWidget* preview) {
    if (!m_previewPane || !preview) return;
    // Remove the previous viewport if there is one
    if (m_viewportWidget && m_viewportWidget != preview) {
        m_previewPane->layout()->removeWidget(m_viewportWidget);
        m_viewportWidget->hide();
        m_viewportWidget->setParent(nullptr);
        m_viewportWidget = nullptr;
    }
    if (m_noPreviewLabel) m_noPreviewLabel->hide();
    preview->setParent(m_previewPane);
    m_previewPane->layout()->addWidget(preview);
    preview->show();
    m_viewportWidget = preview;
}

void ExportWorkspace::clearPreviewWidget() {
    if (!m_previewPane) return;
    if (m_viewportWidget) {
        m_previewPane->layout()->removeWidget(m_viewportWidget);
        m_viewportWidget->hide();
        m_viewportWidget->setParent(nullptr);
        m_viewportWidget = nullptr;
    }
    if (m_noPreviewLabel) m_noPreviewLabel->show();
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
        // Restore saved zoom from previous visit
        if (m_savedZoom > 0.0) {
            m_currentView->setZoomManual(true);
            m_currentView->setZoom(m_savedZoom);
        }
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
    if (m_noPreviewLabel) m_noPreviewLabel->setText(tr("No preview available"));
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
    // Store all globally-enabled profiles so updatePreviewProfileCombo() can re-filter per atlas.
    m_allProfiles = profiles;

    if (m_scaleFilterCombo) m_scaleFilterCombo->blockSignals(true);

    // Populate the profile combo filtered for the current atlas selection.
    const QString preferredProfile = !lastConfig.profiles.isEmpty()
        ? lastConfig.profiles.first()
        : selectedProfileName.trimmed();
    updatePreviewProfileCombo(preferredProfile);

    // Restore output path
    if (!lastConfig.outputPath.isEmpty()) {
        m_outputPathEdit->setText(lastConfig.outputPath);
    } else if (!startDir.isEmpty()) {
        m_outputPathEdit->setText(startDir);
    } else {
        m_outputPathEdit->clear();
    }

    // Populate transform combo on first visit (deferred from constructor to avoid
    // blocking the main thread at startup with a queryTransformsDir subprocess).
    if (m_transformCombo->count() == 1) {
        const QVector<TransformInfo> transforms = loadAvailableTransforms();
        if (!transforms.isEmpty()) {
            for (const TransformInfo& t : transforms)
                m_transformCombo->addItem(t.name, t.id);
        } else {
            m_transformCombo->addItem(QStringLiteral("json"), QStringLiteral("json"));
            m_transformCombo->addItem(QStringLiteral("csv"),  QStringLiteral("csv"));
            m_transformCombo->addItem(QStringLiteral("xml"),  QStringLiteral("xml"));
            m_transformCombo->addItem(QStringLiteral("css"),  QStringLiteral("css"));
        }
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

    // Restore post-export command
    if (m_postExportCommandEdit)
        m_postExportCommandEdit->setText(lastConfig.postExportCommand);

    // Save… only makes sense once an output path has been chosen
    if (m_savePresetBtn)
        m_savePresetBtn->setEnabled(!m_outputPathEdit->text().trimmed().isEmpty());

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
    // Filter the profile combo for the now-selected atlas, then notify.
    updatePreviewProfileCombo();
    onAnyComboChanged();
}

void ExportWorkspace::setAtlasExportConfigs(const QList<QPair<int,AtlasExportConfig>>& configs,
                                             const QStringList& atlasNames) {
    m_atlasConfigs = configs;

    if (!m_atlasOverridesTable || !m_atlasOverridesGroup) return;

    // Only show for 2+ atlases
    const bool show = configs.size() >= 2;
    m_atlasOverridesGroup->setVisible(show);
    if (!show) return;

    m_atlasOverridesTable->setRowCount(0);

    // Build transform items list once
    const QVector<TransformInfo> transforms = loadAvailableTransforms();

    for (int row = 0; row < configs.size(); ++row) {
        const int sessionIdx = configs[row].first;
        const AtlasExportConfig& cfg = configs[row].second;

        m_atlasOverridesTable->insertRow(row);

        // Column 0: Atlas name — use the explicitly provided name when available,
        // fall back to the preview combo lookup for backward compatibility.
        QString atlasName;
        if (row < atlasNames.size()) {
            atlasName = atlasNames[row];
        } else if (m_previewAtlasCombo) {
            for (int ci = 0; ci < m_previewAtlasCombo->count(); ++ci) {
                if (m_previewAtlasCombo->itemData(ci).toInt() == sessionIdx) {
                    atlasName = m_previewAtlasCombo->itemText(ci);
                    break;
                }
            }
        }
        auto* nameItem = new QTableWidgetItem(atlasName);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_atlasOverridesTable->setItem(row, 0, nameItem);

        // Column 1: Transform/format combo
        auto* transformOverrideCombo = new QComboBox(m_atlasOverridesTable);
        transformOverrideCombo->addItem(tr("(default)"), QString());
        transformOverrideCombo->addItem(tr("Raw (sprat-cli format)"), QStringLiteral("raw"));
        if (!transforms.isEmpty()) {
            for (const TransformInfo& t : transforms)
                transformOverrideCombo->addItem(t.name, t.id);
        } else {
            transformOverrideCombo->addItem(QStringLiteral("json"), QStringLiteral("json"));
            transformOverrideCombo->addItem(QStringLiteral("csv"),  QStringLiteral("csv"));
            transformOverrideCombo->addItem(QStringLiteral("xml"),  QStringLiteral("xml"));
        }
        if (!cfg.transform.isEmpty()) {
            const int idx = transformOverrideCombo->findData(cfg.transform);
            if (idx >= 0) transformOverrideCombo->setCurrentIndex(idx);
        }
        m_atlasOverridesTable->setCellWidget(row, 1, transformOverrideCombo);

        // Column 2: Scale filter combo
        auto* sfOverrideCombo = new QComboBox(m_atlasOverridesTable);
        sfOverrideCombo->addItem(tr("(default)"), QString());
        sfOverrideCombo->addItem(tr("Nearest"),   QStringLiteral("nearest"));
        sfOverrideCombo->addItem(tr("Bilinear"),  QStringLiteral("bilinear"));
        sfOverrideCombo->addItem(tr("Bicubic"),   QStringLiteral("bicubic"));
        sfOverrideCombo->addItem(tr("Mitchell"),  QStringLiteral("mitchell"));
        if (!cfg.scaleFilter.isEmpty()) {
            const int idx = sfOverrideCombo->findData(cfg.scaleFilter);
            if (idx >= 0) sfOverrideCombo->setCurrentIndex(idx);
        }
        m_atlasOverridesTable->setCellWidget(row, 2, sfOverrideCombo);

        // Connect combo changes → emit atlasExportConfigChanged
        // Note: profiles are read-only here (edited in Atlases workspace); preserve them.
        auto emitChange = [this, row, sessionIdx, transformOverrideCombo, sfOverrideCombo]() {
            AtlasExportConfig newCfg;
            // Preserve per-atlas profiles set from the Atlases workspace
            if (row < m_atlasConfigs.size())
                newCfg.profiles = m_atlasConfigs[row].second.profiles;
            newCfg.transform   = transformOverrideCombo->currentData().toString();
            newCfg.scaleFilter = sfOverrideCombo->currentData().toString();
            if (row < m_atlasConfigs.size())
                m_atlasConfigs[row].second = newCfg;
            emit atlasExportConfigChanged(sessionIdx, newCfg);
        };

        connect(transformOverrideCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, emitChange);
        connect(sfOverrideCombo,        QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, emitChange);
    }
}

void ExportWorkspace::setPresets(const QVector<ExportPreset>& presets) {
    if (!m_presetCombo) return;
    m_presetCombo->blockSignals(true);
    const QString currentName = m_presetCombo->currentData().toString();
    m_presetCombo->clear();
    m_presetCombo->addItem(tr("— (no preset) —"), QString());
    for (const ExportPreset& p : presets) {
        m_presetCombo->addItem(p.name, p.name);
    }
    // Restore previous selection if still present
    const int idx = m_presetCombo->findData(currentName);
    m_presetCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    m_presetCombo->blockSignals(false);
    onPresetComboChanged(m_presetCombo->currentIndex());
}

void ExportWorkspace::onPresetComboChanged(int /*index*/) {
    if (!m_presetCombo) return;
    const QString name = m_presetCombo->currentData().toString();
    m_delPresetBtn->setEnabled(!name.isEmpty());
}

void ExportWorkspace::updatePreviewProfileCombo(const QString& preferredProfile) {
    if (!m_profileCombo) return;

    // Determine which profiles are enabled for the currently selected atlas.
    // Per-atlas override takes priority; fall back to showing all globally-enabled profiles.
    const int sessionIdx = m_previewAtlasCombo ? m_previewAtlasCombo->currentData().toInt() : -1;
    QStringList enabledNames;
    for (const auto& pair : m_atlasConfigs) {
        if (pair.first == sessionIdx) {
            enabledNames = pair.second.profiles;
            break;
        }
    }
    // No per-atlas override → show all globally-enabled profiles (m_allProfiles already filtered).

    const QString current = preferredProfile.isEmpty()
        ? m_profileCombo->currentData().toString()
        : preferredProfile;

    m_profileCombo->blockSignals(true);
    m_profileCombo->clear();
    const QSet<QString> enabledSet(enabledNames.cbegin(), enabledNames.cend());
    for (const SpratProfile& profile : m_allProfiles) {
        const QString name = profile.name.trimmed();
        if (name.isEmpty()) continue;
        if (!enabledSet.isEmpty() && !enabledSet.contains(name)) continue;
        const QString label = profile.label.trimmed();
        m_profileCombo->addItem(label.isEmpty() ? name : label, name);
    }
    // If per-atlas filter left nothing, fall back to all globally-enabled profiles.
    if (m_profileCombo->count() == 0) {
        for (const SpratProfile& profile : m_allProfiles) {
            const QString name = profile.name.trimmed();
            if (name.isEmpty()) continue;
            const QString label = profile.label.trimmed();
            m_profileCombo->addItem(label.isEmpty() ? name : label, name);
        }
    }
    const int idx = m_profileCombo->findData(current);
    m_profileCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    m_profileCombo->blockSignals(false);
}

void ExportWorkspace::onAnyComboChanged() {
    if (m_noPreviewLabel && !m_viewportWidget)
        m_noPreviewLabel->setText(tr("Generating preview\u2026"));
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
    if (m_postExportCommandEdit)
        config.postExportCommand = m_postExportCommandEdit->text();
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

void ExportWorkspace::showExportLog(const QVector<ExportLogEntry>& entries,
                                    const QString& destination)
{
    if (!m_logTree || !m_logGroup) return;
    m_logDestination = destination;
    m_logTree->clear();

    const bool isZip     = destination.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive);
    const QString prefix = isZip ? QString() : destination;

    qint64 totalSize = 0;
    int    fileCount = 0;
    bool   hasErrors = false;

    for (const auto& e : entries) {
        auto* item = new QTreeWidgetItem();
        switch (e.kind) {
        case ExportLogEntry::Kind::FileWritten: {
            QString disp = e.path;
            if (!prefix.isEmpty() && disp.startsWith(prefix)) {
                disp = disp.mid(prefix.length());
                if (!disp.isEmpty() && (disp[0] == u'/' || disp[0] == u'\\'))
                    disp = disp.mid(1);
            }
            item->setText(0, disp.isEmpty() ? e.path : disp);
            item->setText(1, formatFileSize(e.size));
            if (e.size >= 0) totalSize += e.size;
            ++fileCount;
            break;
        }
        case ExportLogEntry::Kind::Error:
            item->setText(0, e.path);
            item->setForeground(0, Qt::red);
            hasErrors = true;
            break;
        case ExportLogEntry::Kind::Info:
            item->setText(0, e.path);
            item->setForeground(0, QColor(110, 110, 110));
            break;
        }
        m_logTree->addTopLevelItem(item);
    }

    const QString title = hasErrors
        ? tr("Last export: errors — %1 file(s) written").arg(fileCount)
        : tr("Last export: %1 file(s), %2").arg(fileCount).arg(formatFileSize(totalSize));
    m_logGroup->setTitle(title);
    m_logGroup->setVisible(true);
    if (m_logRevealBtn) m_logRevealBtn->setEnabled(!destination.isEmpty());
}

// ---------------------------------------------------------------------------
// IWorkspace
// ---------------------------------------------------------------------------

void ExportWorkspace::enter()
{
    // The populate/setAtlasNames/setAtlasExportConfigs/setPresets setters are
    // called by MainWindow before enter() is invoked, so all data is already in
    // place.  Nothing extra to do here beyond what the setters already set up.
}

void ExportWorkspace::leave()
{
    // Persist the current viewport zoom so it can be restored on the next visit.
    if (m_currentView) {
        const double z = m_currentView->zoom();
        if (z > 0.0) m_savedZoom = z;
    }
    clearViewport();
}
