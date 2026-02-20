#include "MainWindow.h"

#include "AutosaveProjectStore.h"
#include "ImageDiscoveryService.h"
#include "ImageFolderSelectionDialog.h"
#include "ProjectFileLoader.h"
#include "ProjectPayloadCodec.h"
#include "ProjectSaveService.h"
#include "CliToolsConfig.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDoubleSpinBox>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSplitter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QApplication>
#include <QListWidgetItem>
#include <QStringList>
#include <QtGlobal>
#include <algorithm>

namespace {
bool parseResolution(const QString& value, int& width, int& height) {
    const QString normalized = value.trimmed().toLower();
    const QStringList parts = normalized.split('x', Qt::SkipEmptyParts);
    if (parts.size() != 2) {
        return false;
    }
    bool okW = false;
    bool okH = false;
    const int parsedWidth = parts[0].trimmed().toInt(&okW);
    const int parsedHeight = parts[1].trimmed().toInt(&okH);
    if (!okW || !okH || parsedWidth <= 0 || parsedHeight <= 0) {
        return false;
    }
    width = parsedWidth;
    height = parsedHeight;
    return true;
}

double sanitizeLayoutOptionScale(double scale) {
    if (scale <= 0.0 || scale > 1.0) {
        return 1.0;
    }
    return scale;
}

QString formatResolution(int width, int height) {
    return QString("%1x%2").arg(width).arg(height);
}

void syncSourceResolutionPresetSelection(QComboBox* combo, int width, int height) {
    if (!combo) {
        return;
    }
    const int presetIndex = combo->findText(formatResolution(width, height), Qt::MatchFixedString);
    const bool blocked = combo->blockSignals(true);
    combo->setCurrentIndex(presetIndex >= 0 ? presetIndex : 0);
    combo->blockSignals(blocked);
}
}
void MainWindow::cacheLayoutOutputFromPayload(const QJsonObject& payload) {
    QJsonObject layoutInfo = payload["layout"].toObject();
    m_cachedLayoutOutput = layoutInfo["output"].toString();
    m_cachedLayoutScale = layoutInfo["scale"].toDouble(1.0);
}
void MainWindow::loadAutosavedProject() {
    QJsonObject root;
    QString error;
    if (!AutosaveProjectStore::load(getAutosaveFilePath(), root, error)) {
        m_statusLabel->setText(error);
        return;
    }
    QJsonObject layoutOpts = root["layout_options"].toObject();
    int sourceResolutionWidth = 0;
    int sourceResolutionHeight = 0;
    const bool hasSourceResolution = parseResolution(layoutOpts["source_resolution"].toString(), sourceResolutionWidth, sourceResolutionHeight);
    const double layoutOptionScale = sanitizeLayoutOptionScale(layoutOpts["scale"].toDouble(1.0));
    syncSourceResolutionPresetSelection(
        m_sourceResolutionCombo,
        hasSourceResolution ? sourceResolutionWidth : 1024,
        hasSourceResolution ? sourceResolutionHeight : 1024);
    if (m_layoutScaleSpin) {
        const bool blocked = m_layoutScaleSpin->blockSignals(true);
        m_layoutScaleSpin->setValue(layoutOptionScale);
        m_layoutScaleSpin->blockSignals(blocked);
    }
    if (m_sourceResolutionCombo) {
        m_sourceResolutionCombo->setEnabled(true);
    }

    const QJsonObject layoutInfo = root["layout"].toObject();
    const QString folder = layoutInfo["folder"].toString();
    if (folder.isEmpty()) {
        m_statusLabel->setText(tr("Autosave missing folder."));
        return;
    }

    m_statusLabel->setText(tr("Loading autosaved project"));
    cacheLayoutOutputFromPayload(root);
    m_pendingProjectPayload = root;
    m_currentFolder = folder;
    const QString sourceMode = layoutInfo["source_mode"].toString();
    QStringList framePaths;
    for (const auto& frameVal : layoutInfo["frame_paths"].toArray()) {
        const QString framePath = frameVal.toString().trimmed();
        if (!framePath.isEmpty()) {
            framePaths.append(framePath);
        }
    }
    if (sourceMode == "list" && !framePaths.isEmpty()) {
        m_activeFramePaths = framePaths;
        if (!ensureFrameListInput()) {
            QMessageBox::warning(this, tr("Load Failed"), tr("Could not restore autosaved frame list; falling back to folder source."));
            m_layoutSourcePath = QDir(folder).absolutePath();
            m_layoutSourceIsList = false;
            if (!m_frameListPath.isEmpty()) {
                QFile::remove(m_frameListPath);
                m_frameListPath.clear();
            }
            m_folderLabel->setText(tr("Folder: ") + folder);
        }
    } else {
        m_layoutSourcePath = QDir(folder).absolutePath();
        m_layoutSourceIsList = false;
        if (!m_frameListPath.isEmpty()) {
            QFile::remove(m_frameListPath);
            m_frameListPath.clear();
        }
        m_folderLabel->setText(tr("Folder: ") + folder);
    }
    onRunLayout();
}

void MainWindow::onLoadProject() {
    QString file = QFileDialog::getOpenFileName(this, tr("Load Project"), "", tr("Project Files (*.json *.zip)"));
    if (!file.isEmpty()) {
        loadProject(file);
    }
}

void MainWindow::onSaveClicked() {
    QString defaultPath = QDir::currentPath() + "/export";
    if (!m_currentFolder.isEmpty()) {
        defaultPath = QFileInfo(m_currentFolder).dir().filePath("export");
    }

    SaveDialog dlg(defaultPath, configuredProfiles(), m_profileCombo ? m_profileCombo->currentText().trimmed() : QString(), this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    SaveConfig config = dlg.getConfig();
    saveProjectWithConfig(config);
}

bool MainWindow::saveProjectWithConfig(SaveConfig config) {
    if (m_spratLayoutBin.isEmpty() || m_spratPackBin.isEmpty()) {
        QMessageBox::critical(this, tr("Error"), tr("Missing spratlayout or spratpack binaries."));
        return false;
    }
    if (m_layoutSourcePath.isEmpty()) {
        QMessageBox::critical(this, tr("Error"), tr("No layout source selected."));
        return false;
    }

    m_loadingUiMessage = tr("Saving...");
    m_statusLabel->setText(tr("Saving..."));
    QApplication::processEvents();
    m_forceImmediateLoadingOverlay = true;
    QString savedDestination;
    bool ok = ProjectSaveService::save(
        this,
        config,
        m_layoutSourcePath,
        m_activeFramePaths,
        configuredProfiles(),
        m_profileCombo ? m_profileCombo->currentText().trimmed() : QString(),
        m_spratLayoutBin,
        m_spratPackBin,
        m_spratConvertBin,
        buildProjectPayload(config),
        savedDestination,
        [this](bool loading) { setLoading(loading); },
        [this](const QString& status) { m_statusLabel->setText(status); },
        [this](const QString& message) { appendDebugLog(message); });
    m_forceImmediateLoadingOverlay = false;
    if (ok) {
        m_statusLabel->setText(tr("Saved to ") + savedDestination);
        QMetaObject::invokeMethod(this, [this, savedDestination]() {
            QMessageBox::information(this, tr("Saved"), tr("Project saved successfully to:\n") + savedDestination);
        });
    }
    return ok;
}

QJsonObject MainWindow::buildProjectPayload(SaveConfig config) {
    ProjectPayloadBuildInput input;
    input.currentFolder = m_currentFolder;
    input.activeFramePaths = m_activeFramePaths;
    input.layoutSourceIsList = m_layoutSourceIsList;
    input.timelines = m_timelines;
    input.selectedTimelineIndex = m_selectedTimelineIndex;
    QVector<int> selectedTimelineRows;
    const QList<QListWidgetItem*> selectedFrameItems = m_timelineFramesList ? m_timelineFramesList->selectedItems() : QList<QListWidgetItem*>();
    selectedTimelineRows.reserve(selectedFrameItems.size());
    for (QListWidgetItem* item : selectedFrameItems) {
        selectedTimelineRows.append(m_timelineFramesList->row(item));
    }
    std::sort(selectedTimelineRows.begin(), selectedTimelineRows.end());
    input.selectedTimelineFrameRows = selectedTimelineRows;
    input.animationFrameIndex = m_animFrameIndex;
    input.animationPlaying = m_animPlaying;
    input.selectedSprite = m_selectedSprite;
    for (const auto& sprite : m_selectedSprites) {
        if (sprite) {
            input.selectedSpritePaths.append(sprite->path);
        }
    }
    input.primarySelectedSpritePath = m_selectedSprite ? m_selectedSprite->path : QString();
    input.selectedPointName = m_selectedPointName;
    input.layoutModel = m_layoutModel;
    input.layoutOutput = m_cachedLayoutOutput;
    input.layoutScale = m_cachedLayoutScale;
    input.profile = m_profileCombo->currentText();
    SpratProfile selectedProfile;
    const bool hasSelectedProfile = selectedProfileDefinition(selectedProfile);
    input.padding = hasSelectedProfile ? selectedProfile.padding : 0;
    input.trimTransparent = hasSelectedProfile ? selectedProfile.trimTransparent : false;
    input.layoutOptionScale = sanitizeLayoutOptionScale(m_layoutScaleSpin ? m_layoutScaleSpin->value() : 1.0);
    int sourceResolutionWidth = 0;
    int sourceResolutionHeight = 0;
    if (m_sourceResolutionCombo && parseResolution(m_sourceResolutionCombo->currentText(), sourceResolutionWidth, sourceResolutionHeight)) {
        input.sourceResolutionWidth = sourceResolutionWidth;
        input.sourceResolutionHeight = sourceResolutionHeight;
    }
    input.layoutZoom = m_layoutZoomSpin->value();
    input.previewZoom = m_previewZoomSpin->value();
    input.animationZoom = m_animZoomSpin->value();
    if (m_leftSplitter) {
        const QList<int> splitterSizes = m_leftSplitter->sizes();
        input.leftSplitterSizes = QVector<int>(splitterSizes.begin(), splitterSizes.end());
    }
    if (m_rightSplitter) {
        const QList<int> splitterSizes = m_rightSplitter->sizes();
        input.rightSplitterSizes = QVector<int>(splitterSizes.begin(), splitterSizes.end());
    }
    input.appSettings = m_settings;
    input.cliPaths = m_cliPaths;
    input.saveConfig = config;
    return ProjectPayloadCodec::build(input);
}

QString MainWindow::getAutosaveFilePath() const {
    return AutosaveProjectStore::defaultPath();
}

void MainWindow::autosaveProject() {
    if (m_currentFolder.isEmpty() || m_isLoading) {
        return;
    }

    QString error;
    if (AutosaveProjectStore::save(getAutosaveFilePath(), buildProjectPayload({}), error)) {
        m_statusLabel->setText(tr("Autosaved project"));
    } else {
        m_statusLabel->setText(error);
    }
}

void MainWindow::loadProject(const QString& path, bool confirmReplace) {
    if (confirmReplace && !confirmLayoutReplacement()) {
        return;
    }
    QJsonObject root;
    QString error;
    if (!ProjectFileLoader::load(path, root, error)) {
        if (path.endsWith(".zip", Qt::CaseInsensitive) && error.contains("project.spart.json")) {
            // Fallback: ZIP can still be useful when it only contains image frames.
            loadImagesFromZip(path, false);
            return;
        }
        QMessageBox::warning(this, tr("Load Failed"), error);
        return;
    }
    m_pendingProjectPayload = root;
    cacheLayoutOutputFromPayload(root);

    QJsonObject layoutOpts = root["layout_options"].toObject();
    if (layoutOpts.contains("profile")) {
        const QString profile = layoutOpts["profile"].toString();
        if (!profile.isEmpty() && m_profileCombo->findText(profile) < 0) {
            m_profileCombo->addItem(profile);
        }
        m_profileCombo->setCurrentText(profile);
        if (m_profileSelectorStack) {
            m_profileSelectorStack->setCurrentIndex(m_profileCombo->count() > 0 ? 0 : 1);
        }
    }
    int sourceResolutionWidth = 0;
    int sourceResolutionHeight = 0;
    const bool hasSourceResolution = parseResolution(layoutOpts["source_resolution"].toString(), sourceResolutionWidth, sourceResolutionHeight);
    const double layoutOptionScale = sanitizeLayoutOptionScale(layoutOpts["scale"].toDouble(1.0));
    syncSourceResolutionPresetSelection(
        m_sourceResolutionCombo,
        hasSourceResolution ? sourceResolutionWidth : 1024,
        hasSourceResolution ? sourceResolutionHeight : 1024);
    if (m_layoutScaleSpin) {
        const bool blocked = m_layoutScaleSpin->blockSignals(true);
        m_layoutScaleSpin->setValue(layoutOptionScale);
        m_layoutScaleSpin->blockSignals(blocked);
    }
    if (m_sourceResolutionCombo) {
        m_sourceResolutionCombo->setEnabled(true);
    }
    QJsonObject layoutInfo = root["layout"].toObject();
    QString folder = layoutInfo["folder"].toString();
    const QString sourceMode = layoutInfo["source_mode"].toString();
    QStringList framePaths;
    for (const auto& frameVal : layoutInfo["frame_paths"].toArray()) {
        const QString framePath = frameVal.toString().trimmed();
        if (!framePath.isEmpty()) {
            framePaths.append(framePath);
        }
    }
    if (!folder.isEmpty()) {
        m_currentFolder = folder;
        if (sourceMode == "list" && !framePaths.isEmpty()) {
            m_activeFramePaths = framePaths;
            if (!ensureFrameListInput()) {
                QMessageBox::warning(this, tr("Load Failed"), tr("Could not restore saved frame list; falling back to folder source."));
                m_layoutSourcePath = QDir(folder).absolutePath();
                m_layoutSourceIsList = false;
                if (!m_frameListPath.isEmpty()) {
                    QFile::remove(m_frameListPath);
                    m_frameListPath.clear();
                }
                m_folderLabel->setText(tr("Folder: ") + folder);
            }
        } else {
            m_layoutSourcePath = QDir(folder).absolutePath();
            m_layoutSourceIsList = false;
            if (!m_frameListPath.isEmpty()) {
                QFile::remove(m_frameListPath);
                m_frameListPath.clear();
            }
            m_folderLabel->setText(tr("Folder: ") + folder);
        }
        onRunLayout();
    }
}

void MainWindow::clearZipTempDir() {
    if (m_zipTempDir) {
        delete m_zipTempDir;
        m_zipTempDir = nullptr;
    }
}

bool MainWindow::loadImagesFromZip(const QString& zipPath, bool confirmReplace) {
    if (confirmReplace && !confirmLayoutReplacement()) {
        return false;
    }
    if (!m_cliReady || m_isLoading) {
        return false;
    }

    QString unzipBin = QStandardPaths::findExecutable("unzip");
    if (unzipBin.isEmpty()) {
        QMessageBox::warning(this, "Missing Tool", "Please install the 'unzip' utility to load ZIP archives.");
        return false;
    }

    clearZipTempDir();
    m_zipTempDir = new QTemporaryDir();
    if (!m_zipTempDir->isValid()) {
        delete m_zipTempDir;
        m_zipTempDir = nullptr;
        QMessageBox::warning(this, "Load Failed", "Unable to create temporary directory for ZIP extraction.");
        return false;
    }

    QProcess unzip;
    unzip.start(unzipBin, QStringList() << "-qq" << "-o" << zipPath << "-d" << m_zipTempDir->path());
    unzip.waitForFinished();
    if (unzip.exitCode() != 0) {
        clearZipTempDir();
        QMessageBox::warning(this, "Load Failed", "Could not extract ZIP archive.");
        return false;
    }

    QStringList selections;
    bool selectionCanceled = false;
    if (ImageFolderSelectionDialog::pickMultipleFoldersWithImages(this, m_zipTempDir->path(), selections, &selectionCanceled)) {
        const QStringList absolutePaths = ImageDiscoveryService::collectImagesRecursive(selections);
        if (absolutePaths.isEmpty()) {
            clearZipTempDir();
            QMessageBox::warning(this, tr("Load Failed"), tr("No images found in selected folders."));
            return false;
        }
        m_loadingUiMessage = tr("Loading images...");
        setLoading(true);
        if (!m_frameListPath.isEmpty()) {
            QFile::remove(m_frameListPath);
            m_frameListPath.clear();
        }
        m_activeFramePaths = absolutePaths;
        if (!ensureFrameListInput()) {
            setLoading(false);
            clearZipTempDir();
            QMessageBox::warning(this, tr("Load Failed"), tr("Could not create temporary frame list from ZIP selection."));
            return false;
        }
        updateManualFrameLabel();
        m_statusLabel->setText(QString(tr("Loaded %1 image frame(s) from ZIP")).arg(absolutePaths.size()));
        onRunLayout();
        return true;
    }
    if (selectionCanceled) {
        clearZipTempDir();
        m_statusLabel->setText("Load canceled");
        return false;
    }
    if (ImageDiscoveryService::hasImageFiles(m_zipTempDir->path())) {
        loadFolder(m_zipTempDir->path(), confirmReplace);
        return true;
    }
    clearZipTempDir();
    QMessageBox::warning(this, "Load Failed", "ZIP archive does not contain recognizable image folders.");
    return false;
}

void MainWindow::applyProjectPayload() {
    QJsonObject root = m_pendingProjectPayload;
    m_pendingProjectPayload = QJsonObject();
    ProjectPayloadApplyResult applied = ProjectPayloadCodec::applyToLayout(root, m_currentFolder, m_layoutModel);

    if (m_layoutZoomSpin) {
        m_layoutZoomSpin->setValue(applied.layoutZoom);
    }
    if (m_previewZoomSpin) {
        m_previewZoomSpin->setValue(applied.previewZoom);
    }
    if (m_animZoomSpin) {
        m_animZoomSpin->setValue(applied.animationZoom);
    }
    if (m_layoutScaleSpin) {
        const bool blocked = m_layoutScaleSpin->blockSignals(true);
        m_layoutScaleSpin->setValue(sanitizeLayoutOptionScale(applied.layoutOptionScale));
        m_layoutScaleSpin->blockSignals(blocked);
    }
    syncSourceResolutionPresetSelection(
        m_sourceResolutionCombo,
        applied.sourceResolutionWidth > 0 ? applied.sourceResolutionWidth : 1024,
        applied.sourceResolutionHeight > 0 ? applied.sourceResolutionHeight : 1024);
    if (m_sourceResolutionCombo) {
        m_sourceResolutionCombo->setEnabled(true);
    }
    if (m_leftSplitter && !applied.leftSplitterSizes.isEmpty()) {
        m_leftSplitter->setSizes(QList<int>(applied.leftSplitterSizes.begin(), applied.leftSplitterSizes.end()));
    }
    if (m_rightSplitter && !applied.rightSplitterSizes.isEmpty()) {
        m_rightSplitter->setSizes(QList<int>(applied.rightSplitterSizes.begin(), applied.rightSplitterSizes.end()));
    }

    m_settings = applied.appSettings;
    applySettings();
    if (!applied.cliPaths.layoutBinary.isEmpty() ||
        !applied.cliPaths.packBinary.isEmpty() ||
        !applied.cliPaths.convertBinary.isEmpty()) {
        const QString configuredBinDir = CliToolsConfig::loadBinDir();
        m_spratLayoutBin = CliToolsConfig::resolveBinary("spratlayout", applied.cliPaths.layoutBinary, configuredBinDir);
        m_spratPackBin = CliToolsConfig::resolveBinary("spratpack", applied.cliPaths.packBinary, configuredBinDir);
        m_spratConvertBin = CliToolsConfig::resolveBinary("spratconvert", applied.cliPaths.convertBinary, configuredBinDir);
        m_cliPaths.layoutBinary = applied.cliPaths.layoutBinary.isEmpty() ? m_spratLayoutBin : applied.cliPaths.layoutBinary;
        m_cliPaths.packBinary = applied.cliPaths.packBinary.isEmpty() ? m_spratPackBin : applied.cliPaths.packBinary;
        m_cliPaths.convertBinary = applied.cliPaths.convertBinary.isEmpty() ? m_spratConvertBin : applied.cliPaths.convertBinary;
        m_cliReady = !m_spratLayoutBin.isEmpty() && !m_spratPackBin.isEmpty();
        m_statusLabel->setText(m_cliReady ? tr("CLI ready") : tr("CLI missing"));
        updateUiState();
    }

    m_timelines = applied.timelines;
    refreshTimelineList();
    if (applied.selectedTimelineIndex >= 0 && applied.selectedTimelineIndex < m_timelines.size()) {
        m_timelineList->setCurrentRow(applied.selectedTimelineIndex);
    } else if (!m_timelines.isEmpty()) {
        m_timelineList->setCurrentRow(0);
    }

    if (m_selectedTimelineIndex >= 0 && m_selectedTimelineIndex < m_timelines.size() && !applied.selectedTimelineFrameRows.isEmpty()) {
        for (int row : applied.selectedTimelineFrameRows) {
            if (row >= 0 && row < m_timelineFramesList->count()) {
                if (QListWidgetItem* item = m_timelineFramesList->item(row)) {
                    item->setSelected(true);
                }
            }
        }
    }

    m_selectedPointName = applied.selectedMarkerName;
    QStringList selectedPaths = applied.selectedSpritePaths;
    if (selectedPaths.isEmpty() && !applied.selectedSpritePath.isEmpty()) {
        selectedPaths.append(applied.selectedSpritePath);
    }
    QString primaryPath = applied.primarySelectedSpritePath;
    if (primaryPath.isEmpty()) {
        primaryPath = applied.selectedSpritePath;
    }
    if (!selectedPaths.isEmpty()) {
        m_canvas->selectSpritesByPaths(selectedPaths, primaryPath);
    }

    m_animFrameIndex = qMax(0, applied.animationFrameIndex);
    if (m_selectedTimelineIndex >= 0 && m_selectedTimelineIndex < m_timelines.size()) {
        const int frameCount = m_timelines[m_selectedTimelineIndex].frames.size();
        if (frameCount > 0) {
            m_animFrameIndex = qBound(0, m_animFrameIndex, frameCount - 1);
        } else {
            m_animFrameIndex = 0;
        }
    }
    if (applied.animationPlaying && m_selectedTimelineIndex >= 0 && m_selectedTimelineIndex < m_timelines.size()) {
        onAnimPlayPauseClicked();
    } else if (m_animPlaying) {
        onAnimPlayPauseClicked();
    }
    refreshAnimationTest();
}

void MainWindow::onAutosaveTimer() {
    autosaveProject();
}
