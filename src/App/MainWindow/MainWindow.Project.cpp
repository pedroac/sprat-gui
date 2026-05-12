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
#include "ImportPathSupport.h"

#include <QComboBox>
#include <QBuffer>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDir>
#include <QPushButton>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDoubleSpinBox>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
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
QString extensionFromContentType(const QString& contentType) {
    const QString lower = contentType.toLower();
    if (lower.contains("png")) return ".png";
    if (lower.contains("jpeg") || lower.contains("jpg")) return ".jpg";
    if (lower.contains("bmp")) return ".bmp";
    if (lower.contains("gif")) return ".gif";
    if (lower.contains("webp")) return ".webp";
    if (lower.contains("tga")) return ".tga";
    if (lower.contains("dds")) return ".dds";
    if (lower.contains("json")) return ".json";
    if (lower.contains("zip")) return ".zip";
    if (lower.contains("x-tar") || lower == "application/tar") return ".tar";
    if (lower.contains("gzip")) return ".tar.gz";
    if (lower.contains("bzip2")) return ".tar.bz2";
    if (lower.contains("xz")) return ".tar.xz";
    return QString();
}

QString sanitizedImportName(QString name) {
    name = QFileInfo(name).fileName().trimmed();
    if (name.isEmpty()) {
        return QString();
    }
    for (QChar& ch : name) {
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
            ch = '_';
        }
    }
    return name;
}

QString suggestedNameFromReply(const QUrl& url, QNetworkReply* reply) {
    QString fileName;
    const QVariant disposition = reply->header(QNetworkRequest::ContentDispositionHeader);
    if (disposition.isValid()) {
        const QString dispositionText = disposition.toString();
        const QStringList parts = dispositionText.split(';');
        for (const QString& part : parts) {
            const QString trimmed = part.trimmed();
            if (trimmed.startsWith("filename=", Qt::CaseInsensitive)) {
                fileName = trimmed.mid(QStringLiteral("filename=").size()).trimmed();
                if (fileName.startsWith('"') && fileName.endsWith('"') && fileName.size() >= 2) {
                    fileName = fileName.mid(1, fileName.size() - 2);
                }
                break;
            }
        }
    }

    if (fileName.isEmpty()) {
        fileName = QFileInfo(url.path()).fileName();
    }
    if (fileName.isEmpty()) {
        fileName = "download";
    }

    fileName = sanitizedImportName(fileName);
    if (fileName.isEmpty()) {
        fileName = "download";
    }

    if (!fileName.contains('.')) {
        const QString suffix = extensionFromContentType(reply->header(QNetworkRequest::ContentTypeHeader).toString());
        if (!suffix.isEmpty()) {
            fileName += suffix;
        }
    }

    return fileName;
}

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
    // Clear selection state when loading autosaved project to avoid stale pointers
    m_session->selectedSprite.reset();
    m_session->selectedSprites.clear();
    m_session->selectedPointName.clear();
    // Update UI to clear sprite selection display
    onSpriteSelected(SpritePtr());
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

void MainWindow::onLoadFromUrl() {
    if (m_isLoading) {
        return;
    }

    bool accepted = false;
    const QString input = QInputDialog::getText(
        this,
        tr("Load From URL"),
        tr("Enter an image, project, or archive URL:"),
        QLineEdit::Normal,
        QString(),
        &accepted);
    if (!accepted) {
        return;
    }

    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    const QUrl url = QUrl::fromUserInput(trimmed);
    if (!url.isValid()) {
        QMessageBox::warning(this, tr("Invalid URL"), tr("The provided URL is not valid."));
        return;
    }

    DropAction action = confirmDropAction(trimmed);
    if (action == DropAction::Cancel) {
        return;
    }
    tryHandleRemoteUrl(url, action);
}

void MainWindow::onPasteImport() {
    if (m_isLoading) {
        return;
    }

    const QMimeData* mimeData = QApplication::clipboard()->mimeData();
    if (!mimeData) {
        return;
    }

    if (!tryImportClipboard(mimeData, DropAction::Cancel)) {
        QMessageBox::information(
            this,
            tr("Clipboard Not Supported"),
            tr("Clipboard data must be an image, file, or supported URL."));
        return;
    }

    DropAction action = confirmDropAction(tr("clipboard"));
    if (action == DropAction::Cancel) {
        return;
    }

    tryImportClipboard(mimeData, action);
}

QString MainWindow::createManagedImportFile(const QString& suggestedName, const QByteArray& data, QString& error) {
    auto tempDir = std::make_unique<QTemporaryDir>();
    if (!tempDir->isValid()) {
        error = tr("Unable to create a temporary directory for imported data.");
        return QString();
    }

    QString fileName = sanitizedImportName(suggestedName);
    if (fileName.isEmpty()) {
        fileName = "imported-data";
    }
    const QString filePath = QDir(tempDir->path()).filePath(fileName);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        error = tr("Unable to write imported data to a temporary file.");
        return QString();
    }
    if (file.write(data) != data.size()) {
        error = tr("Failed to write all imported data to a temporary file.");
        return QString();
    }
    file.close();

    m_importTempDirs.push_back(std::move(tempDir));
    return filePath;
}

QString MainWindow::createManagedImportImageFile(const QImage& image, QString& error) {
    if (image.isNull()) {
        error = tr("Clipboard image data is empty.");
        return QString();
    }

    QByteArray pngData;
    QBuffer buffer(&pngData);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        error = tr("Unable to encode clipboard image data as PNG.");
        return QString();
    }

    return createManagedImportFile("clipboard-image.png", pngData, error);
}

void MainWindow::finishImportedPath(const QString& path, DropAction action) {
    if (path.isEmpty()) {
        return;
    }
    if (!isSupportedDropPath(path)) {
        QMessageBox::warning(
            this,
            tr("Unsupported Import"),
            tr("The imported data is not a supported image, project, or archive."));
        return;
    }
    tryHandleDroppedPath(path, action);
}

bool MainWindow::tryImportClipboard(const QMimeData* mimeData, DropAction action) {
    if (!mimeData) {
        return false;
    }
    const bool validateOnly = (action == DropAction::Cancel);

    if (mimeData->hasUrls()) {
        const QList<QUrl> urls = mimeData->urls();
        if (urls.size() != 1) {
            return false;
        }
        const QUrl url = urls.first();
        if (url.isLocalFile()) {
            return validateOnly ? isSupportedDropPath(url.toLocalFile())
                                : tryHandleDroppedPath(url.toLocalFile(), action);
        }
        return tryHandleRemoteUrl(url, action);
    }

    if (mimeData->hasImage()) {
        if (validateOnly) {
            return true;
        }
        QImage image = qvariant_cast<QImage>(mimeData->imageData());
        if (image.isNull()) {
            const QPixmap pixmap = qvariant_cast<QPixmap>(mimeData->imageData());
            image = pixmap.toImage();
        }
        QString error;
        const QString path = createManagedImportImageFile(image, error);
        if (path.isEmpty()) {
            QMessageBox::warning(this, tr("Import Failed"), error);
            return true;
        }
        finishImportedPath(path, action);
        return true;
    }

    const QStringList zipMimeTypes = {
        "application/zip",
        "application/x-zip-compressed"
    };
    for (const QString& mimeType : zipMimeTypes) {
        if (mimeData->hasFormat(mimeType)) {
            if (validateOnly) {
                return true;
            }
            QString error;
            const QString path = createManagedImportFile("clipboard.zip", mimeData->data(mimeType), error);
            if (path.isEmpty()) {
                QMessageBox::warning(this, tr("Import Failed"), error);
            } else {
                finishImportedPath(path, action);
            }
            return true;
        }
    }

    if (mimeData->hasText()) {
        const QString text = mimeData->text().trimmed();
        if (text.isEmpty()) {
            return false;
        }

        const QUrl url = QUrl::fromUserInput(text);
        if (url.isValid() && !url.scheme().isEmpty()) {
            if (url.isLocalFile()) {
                return validateOnly ? isSupportedDropPath(url.toLocalFile())
                                    : tryHandleDroppedPath(url.toLocalFile(), action);
            }
            return tryHandleRemoteUrl(url, action);
        }

        if (QFileInfo::exists(text)) {
            const QString localPath = QFileInfo(text).absoluteFilePath();
            return validateOnly ? isSupportedDropPath(localPath)
                                : tryHandleDroppedPath(localPath, action);
        }
    }

    return false;
}

bool MainWindow::tryHandleRemoteUrl(const QUrl& url, DropAction action) {
    if (!ImportPathSupport::isSupportedRemoteImportUrl(url)) {
        return false;
    }

    if (action == DropAction::Cancel) {
        return true;
    }
    if (m_isLoading || m_activeImportReply) {
        return false;
    }

    if (!m_importNetworkManager) {
        m_importNetworkManager = new QNetworkAccessManager(this);
    }

    m_loadingUiMessage = tr("Downloading from URL...");
    m_isCanceled = false;
    setLoading(true);

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_importNetworkManager->get(request);
    m_activeImportReply = reply;

    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total > 0) {
            m_statusLabel->setText(tr("Downloading... %1%").arg((received * 100) / total));
        } else {
            m_statusLabel->setText(tr("Downloading..."));
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, url, action]() {
        m_activeImportReply.clear();
        const QByteArray data = reply->readAll();
        const QString fileName = suggestedNameFromReply(url, reply);
        const bool success = (reply->error() == QNetworkReply::NoError);
        const QString errorText = reply->errorString();
        const bool canceled = (reply->error() == QNetworkReply::OperationCanceledError) || m_isCanceled;
        reply->deleteLater();

        setLoading(false);

        if (!success) {
            if (!canceled) {
                QMessageBox::warning(this, tr("Download Failed"), errorText);
            }
            return;
        }

        QString error;
        const QString path = createManagedImportFile(fileName, data, error);
        if (path.isEmpty()) {
            QMessageBox::warning(this, tr("Import Failed"), error);
            return;
        }

        finishImportedPath(path, action);
    });

    return true;
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
        promoteSourceFolderAfterSave(result.savedDestination);

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
    if (action == DropAction::Cancel) {
        return;
    }
    if (m_animCanvas) m_animCanvas->setZoomManual(false);
    if (m_canvas) m_canvas->setZoomManual(false);
    if (m_previewView) m_previewView->setZoomManual(false);

    const QString lowerPath = path.toLower();
    
    // Delegate to specific loaders based on extension
    if (lowerPath.endsWith(".zip")) {
        loadImagesFromZip(path, action);
        return;
    }
    
    if (lowerPath.endsWith(".tar") || lowerPath.endsWith(".tar.gz") ||
        lowerPath.endsWith(".tar.bz2") || lowerPath.endsWith(".tar.xz")) {
        loadTarFile(path, action);
        return;
    }
    
    if (lowerPath.endsWith(".png") || lowerPath.endsWith(".jpg") ||
        lowerPath.endsWith(".jpeg") || lowerPath.endsWith(".bmp") ||
        lowerPath.endsWith(".gif") || lowerPath.endsWith(".webp") ||
        lowerPath.endsWith(".tga") || lowerPath.endsWith(".dds")) {
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

    // Track project file path and prefer sprites subfolder
    m_projectFilePath = path;
    m_sourceFolderIsTemp = false;

    // Prefer a pre-existing sprites subfolder next to the project file
    const QString projectDir = QFileInfo(path).absolutePath();
    const QString spritesDir = QDir(projectDir).filePath("sprites");
    if (QDir(spritesDir).exists()) {
        m_session->sourceFolder = spritesDir;
    }
    // If no sprites dir found, applyProjectPayload will set sourceFolder from layoutSourcePath

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

    // Clear selection state when loading a new project to avoid stale pointers
    m_session->selectedSprite.reset();
    m_session->selectedSprites.clear();
    m_session->selectedPointName.clear();
    // Update UI to clear sprite selection display
    onSpriteSelected(SpritePtr());

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
        // Ask if files with same names should be replaced
        QMessageBox msg(this);
        msg.setWindowTitle(tr("Merge with duplicates"));
        msg.setText(tr("When merging, what should happen to files with the same name?"));
        QAbstractButton* replaceBtn = msg.addButton(tr("Replace"), QMessageBox::AcceptRole);
        msg.addButton(tr("Rename"), QMessageBox::AcceptRole);
        msg.exec();
        m_mergeReplaceAllDuplicates = (msg.clickedButton() == replaceBtn);

        m_session->activeFramePaths.append(absolutePaths);
        // Copy extracted images to source folder so they persist after temp dir cleanup
        copyActiveFramesToSourceFolder(m_mergeReplaceAllDuplicates);
    } else {
        // On Replace, delete all files from sprites folder
        if (!m_session->sourceFolder.isEmpty()) {
            QDir dir(m_session->sourceFolder);
            for (const QString& file : dir.entryList(QDir::Files)) {
                dir.remove(file);
            }
        }

        m_projectFilePath.clear();
        m_sourceFolderIsTemp = false;
        if (!m_session->frameListPath.isEmpty()) {
            QFile::remove(m_session->frameListPath);
            m_session->frameListPath.clear();
        }
        m_session->sourceFolder.clear();
        m_session->clearSourceFolderTempDir();
        ensureSourceFolder();
        m_session->activeFramePaths = absolutePaths;
        m_session->timelines.clear();
        m_session->selectedTimelineIndex = -1;
        // Clear selection state when loading new images to avoid stale pointers
        m_session->selectedSprite.reset();
        m_session->selectedSprites.clear();
        m_session->selectedPointName.clear();
        // Update UI to clear sprite selection display
        onSpriteSelected(SpritePtr());
        refreshTimelineList();
        refreshAnimationTest();
        // Copy extracted images to source folder on Replace
        copyActiveFramesToSourceFolder();
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

void MainWindow::promoteSourceFolderAfterSave(const QString& saveDestination) {
    QFileInfo destInfo(saveDestination);
    const QString projectDir = destInfo.isDir()
        ? saveDestination
        : destInfo.absolutePath();
    const QString newSpritesPath = QDir(projectDir).filePath("sprites");

    if (m_session->sourceFolder == newSpritesPath) return; // already correct

    if (m_sourceFolderIsTemp && !m_session->sourceFolder.isEmpty()) {
        QDir newDir;
        if (newDir.mkpath(newSpritesPath)) {
            const QStringList files = QDir(m_session->sourceFolder)
                .entryList(QDir::Files | QDir::NoDotAndDotDot);
            for (const QString& file : files) {
                const QString src = QDir(m_session->sourceFolder).filePath(file);
                const QString dst = QDir(newSpritesPath).filePath(file);
                QFile::rename(src, dst);
            }
        }
    }

    m_session->sourceFolder = newSpritesPath;
    m_sourceFolderIsTemp = false;

    if (m_folderWatcher && m_settings.syncMode == SyncMode::Watch) {
        m_folderWatcher->stopWatching();
        m_folderWatcher->watchFolder(newSpritesPath);
    }

    updateOpenSourceFolderAction();
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
