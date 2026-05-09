#include "MainWindow.h"
#include "AnimationCanvas.h"

#include "ArchiveExtractor.h"
#include "AutosaveProjectStore.h"
#include "ImageDiscoveryService.h"
#include "ImageFolderSelectionDialog.h"
#include "ProjectFileLoader.h"
#ifdef Q_OS_WASM
#include "WasmFileDialog.h"
#endif
#include "ProjectPayloadCodec.h"
#include "ProjectSaveService.h"
#include "CliToolsConfig.h"
#include "ResolutionUtils.h"
#include "FolderSyncService.h"

#include <QComboBox>
#include <QDialogButtonBox>
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
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <QApplication>
#include <QListWidgetItem>
#include <QStringList>
#include <QtGlobal>
#include <algorithm>

#ifdef Q_OS_WASM
#include <emscripten.h>
#endif

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
#ifdef Q_OS_WASM
    wasmOpenFileDialog(false);
    return;
#endif
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
    QString defaultPath;
#ifdef Q_OS_WASM
    QString baseName = m_session ? QFileInfo(m_session->currentFolder).fileName().trimmed() : QString();
    if (baseName.isEmpty()) {
        baseName = "sprat-export";
    }
    defaultPath = QDir::temp().filePath(baseName + ".zip");
    SaveConfig dialogConfig = m_lastSaveConfig;
    dialogConfig.destination = defaultPath;
    SaveDialog dlg(defaultPath,
                   configuredProfiles(),
                   m_profileCombo ? m_profileCombo->currentText().trimmed() : QString(),
                   dialogConfig,
                   this,
                   false);
#else
    defaultPath = QDir::currentPath() + "/export";
    if (!m_session->currentFolder.isEmpty()) {
        defaultPath = QFileInfo(m_session->currentFolder).dir().filePath("export");
    }
    SaveDialog dlg(defaultPath, configuredProfiles(), m_profileCombo ? m_profileCombo->currentText().trimmed() : QString(), m_lastSaveConfig, this);
#endif
    const int result = dlg.exec();
    m_lastSaveConfig = dlg.getConfig();
#ifdef Q_OS_WASM
    m_lastSaveConfig.destination = defaultPath;
#endif
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

    if (m_projectSaveWatcher.isRunning()) {
        return false;
    }

    m_isCanceled = false;
    m_loadingUiMessage = tr("Saving...");
    m_statusLabel->setText(tr("Saving..."));
    setLoading(true);

    auto setStatus = [this](const QString& status) {
#ifdef Q_OS_WASM
        m_loadingUiMessage = status;
        m_statusLabel->setText(status);
        if (m_cliInstallOverlayLabel) {
            m_cliInstallOverlayLabel->setText(status);
        }
        if (m_welcomeLabel && (m_session->layoutModels.isEmpty() || m_session->layoutModels.first().sprites.isEmpty())) {
            m_welcomeLabel->setText(status);
        }
        QApplication::processEvents();
#else
        QMetaObject::invokeMethod(this, [this, status]() {
            m_loadingUiMessage = status;
            m_statusLabel->setText(status);
            if (m_cliInstallOverlayLabel) {
                m_cliInstallOverlayLabel->setText(status);
            }
            if (m_welcomeLabel && (m_session->layoutModels.isEmpty() || m_session->layoutModels.first().sprites.isEmpty())) {
                m_welcomeLabel->setText(status);
            }
        }, Qt::QueuedConnection);
#endif
    };

    auto shouldCancel = [this]() {
#ifdef Q_OS_WASM
        QApplication::processEvents();
#endif
        return m_isCanceled.load();
    };

    auto saveTask = [this, config, setStatus, shouldCancel]() {
        ProjectSaveResult result;
        
        auto runToolBound = [this](const QString& tool, const QStringList& args, const QString& step, const QByteArray* input, QByteArray* output) {
            // Error output is discarded here but we could pass another QByteArray to runTool if needed
            return this->runTool(tool, args, input, output, nullptr);
        };

        result.success = ProjectSaveService::save(
            config,
            m_session->layoutSourcePath,
            m_session->activeFramePaths,
            configuredProfiles(),
            m_profileCombo ? m_profileCombo->currentText().trimmed() : QString(),
            m_spratLayoutBin,
            m_spratPackBin,
            m_spratConvertBin,
            buildProjectPayload(config, m_session),
            result.savedDestination,
            result.error,
            m_settings.deduplicateMode,
            nullptr, // setLoading handled by finished slot
            setStatus,
            shouldCancel,
            runToolBound
        );
        if (!result.success && shouldCancel()) {
            result.canceled = true;
        }
        return result;
    };

    #ifdef Q_OS_WASM
    QTimer::singleShot(0, this, [this, saveTask]() {
        ProjectSaveResult result = saveTask();
        handleProjectSaveResult(result);
    });
    #else
    m_projectSaveWatcher.setFuture(QtConcurrent::run(saveTask));
    #endif
    return true;
}

void MainWindow::onProjectSaveFinished() {
    handleProjectSaveResult(m_projectSaveWatcher.result());
}

void MainWindow::handleProjectSaveResult(const ProjectSaveResult& result) {
    setLoading(false);

    if (result.canceled) {
        m_statusLabel->setText(tr("Save canceled"));
        return;
    }

    if (result.success) {
#ifdef Q_OS_WASM
        m_statusLabel->setText(tr("Preparing download..."));
#else
        m_statusLabel->setText(tr("Saved to ") + result.savedDestination);
#endif
#ifdef Q_OS_WASM
        // Trigger browser download for the saved file
        QFile file(result.savedDestination);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray content = file.readAll();
            QString fileName = QFileInfo(result.savedDestination).fileName();
            
            EM_ASM({
                var content = HEAPU8.slice($0, $0 + $1);
                var fileName = UTF8ToString($2);
                var blob = new Blob([content], {type: "application/octet-stream"});
                var url = window.URL.createObjectURL(blob);
                var a = document.createElement("a");
                a.href = url;
                a.download = fileName;
                document.body.appendChild(a);
                a.click();
                window.URL.revokeObjectURL(url);
                document.body.removeChild(a);
            }, content.constData(), content.size(), fileName.toUtf8().constData());
            
            file.close();
        }
        if (!result.savedDestination.isEmpty()) {
            QFile::remove(result.savedDestination);
        }
        m_statusLabel->setText(tr("Download started"));
#endif
#ifdef Q_OS_WASM
        QMessageBox::information(this, tr("Saved"), tr("Download started."));
#else
        QMessageBox::information(this, tr("Saved"), tr("Project saved successfully to:\n") + result.savedDestination);
#endif
    } else {
        m_statusLabel->setText(tr("Save failed"));
        QMessageBox::critical(this, tr("Save Failed"), result.error.isEmpty() ? tr("An unknown error occurred during save.") : result.error);
    }
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
    if (ext == "zip") {
        loadImagesFromZip(path, action);
        return;
    }
    
    if (ext == "tar" || ext == "gz" || ext == "bz2" || ext == "xz") {
        loadTarFile(path, action);
        return;
    }
    
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "gif" || ext == "webp" || ext == "tga" || ext == "dds") {
        loadImageWithFrameDetection(path, action);
        return;
    }

    if (m_projectLoadWatcher.isRunning()) {
        return;
    }

    m_loadingUiMessage = tr("Loading project...");
    setLoading(true);

    auto loadTask = [path, action]() {
        ProjectLoadResult result;
        result.path = path;
        result.action = action;
        result.success = ProjectFileLoader::load(path, result.root, result.error);
        return result;
    };

    m_projectLoadWatcher.setFuture(QtConcurrent::run(loadTask));
}

void MainWindow::onProjectLoadFinished() {
    ProjectLoadResult result = m_projectLoadWatcher.result();
    const QString path = result.path;
    const DropAction action = result.action;

    if (!result.success) {
        if (path.endsWith(".zip", Qt::CaseInsensitive)) {
            loadImagesFromZip(path, action);
            return;
        }
        setLoading(false);
        QMessageBox::warning(this, tr("Load Failed"), result.error);
        return;
    }

    // Add to recent projects
    addToRecentProjects(path);

    QJsonObject root = result.root;
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
        setLoading(false);
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
    } else {
        setLoading(false);
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

    if (m_zipDiscoveryWatcher.isRunning()) {
        return false;
    }

    m_loadingUiMessage = tr("Extracting ZIP...");
    m_isCanceled = false;
    setLoading(true);

    if (action == DropAction::Replace) {
        m_session->clearTempDirs();
    }
    auto tempDir = std::make_unique<QTemporaryDir>();
    if (!tempDir->isValid()) {
        setLoading(false);
        QMessageBox::warning(this, tr("Load Failed"), tr("Unable to create temporary directory for ZIP extraction."));
        return false;
    }
    QString tempPath = tempDir->path();
    m_session->addTempDir(std::move(tempDir));

    auto zipTask = [this, zipPath, tempPath, action]() {
        ZipDiscoveryResult result;
        result.tempPath = tempPath;
        result.zipPath = zipPath;
        result.action = action;
        result.canceled = false;

        QString error;
        if (ArchiveExtractor::extractToDirectory(zipPath, tempPath, error, &m_isCanceled)) {
            if (m_isCanceled) {
                result.canceled = true;
            } else {
                result.selections = ImageDiscoveryService::imageDirectoriesRecursive(tempPath);
            }
        } else {
            if (m_isCanceled) {
                result.canceled = true;
            } else {
                result.error = error;
                qWarning() << "ArchiveExtractor error:" << error;
            }
        }
        return result;
    };

#ifdef Q_OS_WASM
    // QtConcurrent can be unreliable without pthreads/COOP+COEP; run synchronously.
    processZipDiscoveryResult(zipTask());
#else
    m_zipDiscoveryWatcher.setFuture(QtConcurrent::run(zipTask));
#endif
    return true;
}

void MainWindow::onZipDiscoveryFinished() {
    processZipDiscoveryResult(m_zipDiscoveryWatcher.result());
}

void MainWindow::processZipDiscoveryResult(const ZipDiscoveryResult& result) {
    if (result.canceled) {
        setLoading(false);
        m_statusLabel->setText(tr("ZIP extraction canceled"));
        return;
    }
    const QString tempPath = result.tempPath;
    const QString zipPath = result.zipPath;
    const DropAction action = result.action;
    QStringList imageDirectories = result.selections;

    if (imageDirectories.isEmpty()) {
        setLoading(false);
        if (!result.error.isEmpty()) {
            QMessageBox::warning(this, tr("Load Failed"), tr("Could not extract ZIP: %1").arg(result.error));
        } else {
            QMessageBox::warning(this, tr("Load Failed"), tr("Could not extract ZIP or no image folders found."));
        }
        return;
    }

    QStringList selectedFolders;
    bool selectionCanceled = false;
    
    if (imageDirectories.size() == 1) {
        selectedFolders = imageDirectories;
    } else {
        // Show selection dialog (blocking, but scan was async)
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Select frame folders"));
        dialog.setModal(true);
        QVBoxLayout* layout = new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(tr("Choose one or more folders with images:"), &dialog));
        QTreeWidget* tree = new QTreeWidget(&dialog);
        tree->setHeaderLabel(tr("Folders"));
        tree->setSelectionMode(QAbstractItemView::NoSelection);
        layout->addWidget(tree, 1);
        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        const QDir base(tempPath);
        QHash<QString, QTreeWidgetItem*> items;
        for (const QString& absPath : imageDirectories) {
            QString relPath = base.relativeFilePath(absPath).replace('\\', '/');
            QStringList parts = relPath.split('/', Qt::SkipEmptyParts);
            QTreeWidgetItem* parent = nullptr;
            QString currentPath;
            for (const QString& part : parts) {
                currentPath = currentPath.isEmpty() ? part : currentPath + "/" + part;
                if (!items.contains(currentPath)) {
                    QTreeWidgetItem* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
                    item->setText(0, part);
                    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                    item->setCheckState(0, Qt::Unchecked);
                    item->setData(0, Qt::UserRole, currentPath);
                    items.insert(currentPath, item);
                }
                parent = items.value(currentPath);
            }
        }
        tree->expandAll();

        if (dialog.exec() == QDialog::Accepted) {
            for (auto it = items.begin(); it != items.end(); ++it) {
                if (it.value()->checkState(0) == Qt::Checked) {
                    QString relPath = it.key();
                    selectedFolders.append(base.absoluteFilePath(relPath));
                }
            }
        } else {
            selectionCanceled = true;
        }
    }

    if (selectionCanceled) {
        m_statusLabel->setText(tr("Load canceled"));
        setLoading(false);
        return;
    }

    if (selectedFolders.isEmpty()) {
        // Fallback to checking root if nothing selected or just one folder wasn't enough
        if (ImageDiscoveryService::hasImageFiles(tempPath)) {
            selectedFolders << tempPath;
        } else {
            setLoading(false);
            QMessageBox::warning(this, tr("Load Failed"), tr("No folders selected."));
            return;
        }
    }

    const QStringList absolutePaths = ImageDiscoveryService::collectImagesRecursive(selectedFolders);
    if (absolutePaths.isEmpty()) {
        setLoading(false);
        QMessageBox::warning(this, tr("Load Failed"), tr("No images found in selected folders."));
        return;
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
        return;
    }
    updateManualFrameLabel();
    m_statusLabel->setText(QString(tr("Loaded %1 image frame(s) from ZIP")).arg(absolutePaths.size()));
    onRunLayout();
}

void MainWindow::applyProjectPayload() {
    m_isRestoringProject = true;

    // Clean up previous source folder watcher before loading new project
    cleanupSourceFolderWatcher();

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

    // Initialize source folder for sync if layoutSourcePath points to a folder
    if (!m_session->layoutSourcePath.isEmpty()) {
        QFileInfo fi(m_session->layoutSourcePath);
        if (fi.isDir()) {
            m_session->sourceFolder = fi.absoluteFilePath();
            initializeSourceFolderWatcher();
        }
    }

    QStringList missing;
    resolveCliBinaries(missing);
    QString storedVersion = CliToolsConfig::loadInstalledCliVersion();
    m_cliReady = missing.isEmpty()
                 && !storedVersion.isEmpty()
                 && storedVersion == QString(SPRAT_CLI_VERSION);
    m_statusLabel->setText(m_cliReady ? tr("CLI ready") : tr("CLI tools not ready"));
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
