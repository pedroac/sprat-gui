#include "MainWindow.h"
#include "AnimationCanvas.h"

#include "AutosaveProjectStore.h"
#include "ImageDiscoveryService.h"
#include "ImageFolderSelectionDialog.h"
#include "ProjectFileLoader.h"
#include "ProjectPayloadCodec.h"
#include "ProjectSaveService.h"
#include "CliToolsConfig.h"
#include "ResolutionUtils.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QApplication>
#include <QListWidgetItem>
#include <QStringList>
#include <QtGlobal>
#include <algorithm>

namespace {
void syncSourceResolutionPresetSelection(QComboBox* combo, int width, int height) {
    if (!combo) {
        return;
    }
    const int presetIndex = combo->findText(formatResolutionText(width, height), Qt::MatchFixedString);
    const bool blocked = combo->blockSignals(true);
    combo->setCurrentIndex(presetIndex >= 0 ? presetIndex : 0);
    combo->blockSignals(blocked);
}
}
void MainWindow::cacheLayoutOutputFromPayload(const QJsonObject& payload) {
    QJsonObject layoutInfo = payload["layout"].toObject();
    m_session->cachedLayoutOutput = layoutInfo["output"].toString();
    m_session->cachedLayoutScale = layoutInfo["scale"].toDouble(1.0);
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
    const bool hasSourceResolution = parseResolutionText(layoutOpts["source_resolution"].toString(), sourceResolutionWidth, sourceResolutionHeight);
    syncSourceResolutionPresetSelection(
        m_sourceResolutionCombo,
        hasSourceResolution ? sourceResolutionWidth : 1024,
        hasSourceResolution ? sourceResolutionHeight : 1024);
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
    m_session->pendingProjectPayload = root;
    m_session->currentFolder = folder;
    const QString sourceMode = layoutInfo["source_mode"].toString();
    QStringList framePaths;
    for (const auto& frameVal : layoutInfo["frame_paths"].toArray()) {
        const QString framePath = frameVal.toString().trimmed();
        if (!framePath.isEmpty()) {
            framePaths.append(framePath);
        }
    }
    if (sourceMode == "list" && !framePaths.isEmpty()) {
        m_session->activeFramePaths = framePaths;
        if (!ensureFrameListInput()) {
            QMessageBox::warning(this, tr("Load Failed"), tr("Could not restore autosaved frame list; falling back to folder source."));
            m_session->layoutSourcePath = QDir(folder).absolutePath();
            m_session->layoutSourceIsList = false;
            if (!m_session->frameListPath.isEmpty()) {
                QFile::remove(m_session->frameListPath);
                m_session->frameListPath.clear();
            }
            m_folderLabel->setText(tr("Folder: ") + folder);
        }
    } else {
        m_session->layoutSourcePath = QDir(folder).absolutePath();
        m_session->layoutSourceIsList = false;
        if (!m_session->frameListPath.isEmpty()) {
            QFile::remove(m_session->frameListPath);
            m_session->frameListPath.clear();
        }
        m_folderLabel->setText(tr("Folder: ") + folder);
    }
    onRunLayout();
}

void MainWindow::onLoadProject() {
    QString filter = tr("All Supported Files (*.json *.zip *.tar *.tar.gz *.tar.bz2 *.tar.xz *.png *.jpg *.jpeg *.bmp *.gif *.webp *.tga *.dds);;"
                        "Project Files (*.json);;"
                        "Archives (*.zip *.tar *.tar.gz *.tar.bz2 *.tar.xz);;"
                        "Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tga *.dds)");
    QString file = QFileDialog::getOpenFileName(this, tr("Load File"), "", filter);
    if (!file.isEmpty()) {
        loadProject(file);
    }
}

void MainWindow::onSaveClicked() {
    QString defaultPath = QDir::currentPath() + "/export";
    if (!m_session->currentFolder.isEmpty()) {
        defaultPath = QFileInfo(m_session->currentFolder).dir().filePath("export");
    }

    SaveDialog dlg(defaultPath, configuredProfiles(), m_profileCombo ? m_profileCombo->currentText().trimmed() : QString(), m_lastSaveConfig, this);
    const int result = dlg.exec();
    m_lastSaveConfig = dlg.getConfig();
    if (result != QDialog::Accepted) {
        return;
    }

    saveProjectWithConfig(m_lastSaveConfig);
}

bool MainWindow::saveProjectWithConfig(SaveConfig config) {
    if (m_spratLayoutBin.isEmpty() || m_spratPackBin.isEmpty()) {
        QMessageBox::critical(this, tr("Error"), tr("Missing spratlayout or spratpack binaries."));
        return false;
    }
    if (m_session->layoutSourcePath.isEmpty()) {
        QMessageBox::critical(this, tr("Error"), tr("No layout source selected."));
        return false;
    }

    m_loadingUiMessage = tr("Saving...");
    m_statusLabel->setText(tr("Saving..."));
    QApplication::processEvents();
    QString savedDestination;
    bool ok = ProjectSaveService::save(
        this,
        config,
        m_session->layoutSourcePath,
        m_session->activeFramePaths,
        configuredProfiles(),
        m_profileCombo ? m_profileCombo->currentText().trimmed() : QString(),
        m_spratLayoutBin,
        m_spratPackBin,
        m_spratConvertBin,
        buildProjectPayload(config, m_session),
        savedDestination,
        [this](bool loading) { setLoading(loading); },
        [this](const QString& status) { m_statusLabel->setText(status); }
    );
    if (ok) {
        m_statusLabel->setText(tr("Saved to ") + savedDestination);
        QMetaObject::invokeMethod(this, [this, savedDestination]() {
            QMessageBox::information(this, tr("Saved"), tr("Project saved successfully to:\n") + savedDestination);
        });
    }
    return ok;
}

QJsonObject MainWindow::buildProjectPayload(SaveConfig config, ProjectSession* session) {
    ProjectPayloadBuildInput input;
    input.currentFolder = session->currentFolder;
    input.activeFramePaths = session->activeFramePaths;
    input.layoutSourceIsList = session->layoutSourceIsList;
    input.timelines = session->timelines;
    input.selectedTimelineIndex = session->selectedTimelineIndex;
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
    input.selectedSprite = session->selectedSprite;
    for (const auto& sprite : session->selectedSprites) {
        if (sprite) {
            input.selectedSpritePaths.append(sprite->path);
        }
    }
    input.primarySelectedSpritePath = session->selectedSprite ? session->selectedSprite->path : QString();
    input.selectedPointName = session->selectedPointName;
    input.layoutModels = session->layoutModels;
    input.layoutOutput = session->cachedLayoutOutput;
    input.layoutScale = session->cachedLayoutScale;
    input.profile = m_profileCombo->currentText();
    SpratProfile selectedProfile;
    const bool hasSelectedProfile = selectedProfileDefinition(selectedProfile);
    input.padding = hasSelectedProfile ? selectedProfile.padding : 0;
    input.trimTransparent = hasSelectedProfile ? selectedProfile.trimTransparent : false;
    int sourceResolutionWidth = 0;
    int sourceResolutionHeight = 0;
    if (m_sourceResolutionCombo && parseResolutionText(m_sourceResolutionCombo->currentText(), sourceResolutionWidth, sourceResolutionHeight)) {
        input.sourceResolutionWidth = sourceResolutionWidth;
        input.sourceResolutionHeight = sourceResolutionHeight;
    }
    input.layoutZoom = m_layoutZoomSpin->value() / 100.0;
    input.previewZoom = m_previewZoomSpin->value() / 100.0;
    input.animationZoom = m_animZoomSpin->value() / 100.0;
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
    if (m_session->currentFolder.isEmpty() || m_isLoading) {
        return;
    }

    QString error;
    if (AutosaveProjectStore::save(getAutosaveFilePath(), buildProjectPayload(m_lastSaveConfig, m_session), error)) {
        m_statusLabel->setText(tr("Autosaved project"));
    } else {
        m_statusLabel->setText(error);
    }
}

void MainWindow::loadProject(const QString& path, DropAction action) {
    if (action == DropAction::Replace && !confirmLayoutReplacement()) {
        return;
    }
    if (action == DropAction::Cancel) {
        return;
    }
    if (m_animCanvas) m_animCanvas->setZoomManual(false);
    if (m_canvas) m_canvas->setZoomManual(false);
    if (m_previewView) m_previewView->setZoomManual(false);

    QFileInfo info(path);
    const QString ext = info.suffix().toLower();
    
    // Delegate to specific loaders based on extension
    if (ext == "tar" || ext == "gz" || ext == "bz2" || ext == "xz") {
        loadTarFile(path, action);
        return;
    }
    
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "gif" || ext == "webp" || ext == "tga" || ext == "dds") {
        loadImageWithFrameDetection(path, action);
        return;
    }

    QJsonObject root;
    QString error;
    if (!ProjectFileLoader::load(path, root, error)) {
        if (path.endsWith(".zip", Qt::CaseInsensitive)) {
            loadImagesFromZip(path, action);
            return;
        }
        QMessageBox::warning(this, tr("Load Failed"), error);
        return;
    }
    
    QJsonObject layoutInfo = root["layout"].toObject();
    QString folder = layoutInfo["folder"].toString();
    QStringList framePaths;
    for (const auto& frameVal : layoutInfo["frame_paths"].toArray()) {
        const QString framePath = frameVal.toString().trimmed();
        if (!framePath.isEmpty()) {
            framePaths.append(framePath);
        }
    }

    if (action == DropAction::Merge) {
        if (!framePaths.isEmpty()) {
            m_session->activeFramePaths.append(framePaths);
            onRunLayout();
        }
        return;
    }

    m_session->pendingProjectPayload = root;
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
    const bool hasSourceResolution = parseResolutionText(layoutOpts["source_resolution"].toString(), sourceResolutionWidth, sourceResolutionHeight);
    syncSourceResolutionPresetSelection(
        m_sourceResolutionCombo,
        hasSourceResolution ? sourceResolutionWidth : 1024,
        hasSourceResolution ? sourceResolutionHeight : 1024);
    if (m_sourceResolutionCombo) {
        m_sourceResolutionCombo->setEnabled(true);
    }
    
    const QString sourceMode = layoutInfo["source_mode"].toString();
    if (!folder.isEmpty()) {
        m_session->currentFolder = folder;
        if (sourceMode == "list" && !framePaths.isEmpty()) {
            m_session->activeFramePaths = framePaths;
            if (!ensureFrameListInput()) {
                QMessageBox::warning(this, tr("Load Failed"), tr("Could not restore saved frame list; falling back to folder source."));
                m_session->layoutSourcePath = QDir(folder).absolutePath();
                m_session->layoutSourceIsList = false;
                if (!m_session->frameListPath.isEmpty()) {
                    QFile::remove(m_session->frameListPath);
                    m_session->frameListPath.clear();
                }
                m_folderLabel->setText(tr("Folder: ") + folder);
            }
        } else {
            m_session->layoutSourcePath = QDir(folder).absolutePath();
            m_session->layoutSourceIsList = false;
            if (!m_session->frameListPath.isEmpty()) {
                QFile::remove(m_session->frameListPath);
                m_session->frameListPath.clear();
            }
            m_folderLabel->setText(tr("Folder: ") + folder);
        }
        onRunLayout();
    }
}

bool MainWindow::loadImagesFromZip(const QString& zipPath, DropAction action) {

    if (action == DropAction::Replace && !confirmLayoutReplacement()) {
        return false;
    }
    if (action == DropAction::Cancel) {
        return false;
    }
    if (!m_cliReady || m_isLoading) {
        return false;
    }

    QString unzipBin = QStandardPaths::findExecutable("unzip");
    bool usePowerShell = false;
    if (unzipBin.isEmpty()) {
#ifdef Q_OS_WIN
        unzipBin = QStandardPaths::findExecutable("powershell");
        if (!unzipBin.isEmpty()) {
            usePowerShell = true;
        } else {
            QMessageBox::warning(this, tr("Missing Tool"), tr("Neither 'unzip' nor 'powershell' was found. Cannot load ZIP archives."));
            return false;
        }
#else
        QMessageBox::warning(this, tr("Missing Tool"), tr("Please install the 'unzip' utility to load ZIP archives."));
        return false;
#endif
    }

    if (action == DropAction::Replace) {
        m_session->clearTempDirs();
    }
    auto tempDir = std::make_unique<QTemporaryDir>();
    if (!tempDir->isValid()) {
        QMessageBox::warning(this, tr("Load Failed"), tr("Unable to create temporary directory for ZIP extraction."));
        return false;
    }
    QString tempPath = tempDir->path();
    m_session->addTempDir(std::move(tempDir));

    QProcess unzip;
    if (usePowerShell) {
        QString script = QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force").arg(zipPath, tempPath);
        unzip.start(unzipBin, QStringList() << "-Command" << script);
    } else {
        unzip.start(unzipBin, QStringList() << "-qq" << "-o" << zipPath << "-d" << tempPath);
    }
    unzip.waitForFinished();
    if (unzip.exitStatus() != QProcess::NormalExit || unzip.exitCode() != 0) {
        QMessageBox::warning(this, tr("Load Failed"), tr("Could not extract ZIP archive."));
        return false;
    }

    QStringList selections;
    bool selectionCanceled = false;
    if (ImageFolderSelectionDialog::pickMultipleFoldersWithImages(this, tempPath, selections, &selectionCanceled)) {
        const QStringList absolutePaths = ImageDiscoveryService::collectImagesRecursive(selections);
        if (absolutePaths.isEmpty()) {
            QMessageBox::warning(this, tr("Load Failed"), tr("No images found in selected folders."));
            return false;
        }
        m_loadingUiMessage = tr("Loading images...");
        setLoading(true);
        
        if (action == DropAction::Merge) {
            m_session->activeFramePaths.append(absolutePaths);
        } else {
            if (!m_session->frameListPath.isEmpty()) {
                QFile::remove(m_session->frameListPath);
                m_session->frameListPath.clear();
            }
            m_session->activeFramePaths = absolutePaths;
        }

        if (!ensureFrameListInput()) {
            setLoading(false);
            QMessageBox::warning(this, tr("Load Failed"), tr("Could not create temporary frame list from ZIP selection."));
            return false;
        }
        updateManualFrameLabel();
        m_statusLabel->setText(QString(tr("Loaded %1 image frame(s) from ZIP")).arg(absolutePaths.size()));
        onRunLayout();
        return true;
    }
    if (selectionCanceled) {
        m_statusLabel->setText(tr("Load canceled"));
        return false;
    }
    if (ImageDiscoveryService::hasImageFiles(tempPath)) {
        loadFolder(tempPath, action);
        return true;
    }
    QMessageBox::warning(this, tr("Load Failed"), tr("ZIP archive does not contain recognizable image folders."));
    return false;
}

void MainWindow::applyProjectPayload() {
    m_isRestoringProject = true;
    QJsonObject root = m_session->pendingProjectPayload;
    m_session->pendingProjectPayload = QJsonObject();
    ProjectPayloadApplyResult applied = ProjectPayloadCodec::applyToLayout(root, m_session->currentFolder, m_session->layoutModels);

    if (m_layoutZoomSpin) {
        m_layoutZoomSpin->setValue(applied.layoutZoom * 100.0);
    }
    if (m_previewZoomSpin) {
        m_previewZoomSpin->setValue(applied.previewZoom * 100.0);
    }
    if (m_animZoomSpin) {
        m_animZoomSpin->setValue(applied.animationZoom * 100.0);
    }
    if (m_animCanvas) m_animCanvas->setZoomManual(false);
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
    m_lastSaveConfig = applied.saveConfig;
    applySettings();
    
    QStringList missing;
    resolveCliBinaries(missing);
    m_cliReady = !m_spratLayoutBin.isEmpty() && !m_spratPackBin.isEmpty();
    m_statusLabel->setText(m_cliReady ? tr("CLI ready") : tr("CLI missing"));
    updateUiState();

    m_session->timelines = applied.timelines;

    refreshTimelineList();
    if (applied.selectedTimelineIndex >= 0 && applied.selectedTimelineIndex < m_session->timelines.size()) {
        m_timelineList->setCurrentRow(applied.selectedTimelineIndex);
    } else if (!m_session->timelines.isEmpty()) {
        m_timelineList->setCurrentRow(0);
    }

    if (m_session->selectedTimelineIndex >= 0 && m_session->selectedTimelineIndex < m_session->timelines.size() && !applied.selectedTimelineFrameRows.isEmpty()) {
        for (int row : applied.selectedTimelineFrameRows) {
            if (row >= 0 && row < m_timelineFramesList->count()) {
                if (QListWidgetItem* item = m_timelineFramesList->item(row)) {
                    item->setSelected(true);
                }
            }
        }
    }

    m_session->selectedPointName = applied.selectedMarkerName;
    QStringList selectedPaths = applied.selectedSpritePaths;
    if (selectedPaths.isEmpty() && !applied.selectedSpritePath.isEmpty()) {
        selectedPaths.append(applied.selectedSpritePath);
    }
    QString primaryPath = applied.primarySelectedSpritePath;
    if (primaryPath.isEmpty()) {
        primaryPath = applied.selectedSpritePath;
    }
    if (!selectedPaths.isEmpty()) {
        if (m_canvas) {
            m_canvas->selectSpritesByPaths(selectedPaths, primaryPath);
        }
    }

    m_animFrameIndex = qMax(0, applied.animationFrameIndex);
    if (m_session->selectedTimelineIndex >= 0 && m_session->selectedTimelineIndex < m_session->timelines.size()) {
        const int frameCount = m_session->timelines[m_session->selectedTimelineIndex].frames.size();
        if (frameCount > 0) {
            m_animFrameIndex = qBound(0, m_animFrameIndex, frameCount - 1);
        } else {
            m_animFrameIndex = 0;
        }
    }
    if (applied.animationPlaying && m_session->selectedTimelineIndex >= 0 && m_session->selectedTimelineIndex < m_session->timelines.size()) {
        onAnimPlayPauseClicked();
    } else if (m_animPlaying) {
        onAnimPlayPauseClicked();
    }
    refreshAnimationTest();
    m_isRestoringProject = false;
}

void MainWindow::onAutosaveTimer() {
    autosaveProject();
}
