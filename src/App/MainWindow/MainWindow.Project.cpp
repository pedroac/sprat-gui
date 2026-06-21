#include "MainWindow.h"
#include "AnimationPreviewPanel.h"
#include "TimelineEditorPanel.h"
#include "TimelineListWidget.h"
#include "LayoutCanvas.h"
#include "LayoutOrchestrator.h"
#include "CliSetupController.h"
#include "AnimationCanvas.h"
#include "PackedAtlasView.h"
#include "SpriteEditorPanel.h"
#include "PreviewCanvas.h"
#include "NavigatorPanel.h"
#include "NavigatorTreeWidget.h"
#include "MarkerRepository.h"
#include "AtlasesManagementWorkspace.h"
#include "ExportWorkspace.h"
#include "FrameAnimationWorkspace.h"

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
#include "MessageDialog.h"
#include "ExportDiffDialog.h"

#include <QDockWidget>
#include "ResolutionUtils.h"
#include "ImportPathSupport.h"

#include <QCheckBox>
#include <QDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QMenu>
#include <QComboBox>
#include <QSizePolicy>
#include <QStyle>
#include <QDirIterator>
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
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
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

void MainWindow::onOpenRecentProjectRequested() {
    if (m_recentProjects.isEmpty()) {
        onLoadProject();
        return;
    }

    if (m_recentProjectsMenu) {
        // Show the recent projects menu at the button's position
        m_recentProjectsMenu->exec(m_recentProjectBtn->mapToGlobal(QPoint(0, m_recentProjectBtn->height())));
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
        m_atlasWorkspace->sourceResolutionCombo(),
        hasSourceResolution ? sourceResolutionWidth : 1024,
        hasSourceResolution ? sourceResolutionHeight : 1024);
    if (m_atlasWorkspace->sourceResolutionCombo()) {
        m_atlasWorkspace->sourceResolutionCombo()->setEnabled(true);
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
        QString framePath = frameVal.toString().trimmed();
        if (framePath.isEmpty()) continue;
        if (QDir::isRelativePath(framePath) && !folder.isEmpty()) {
            framePath = QDir(folder).filePath(framePath);
        }
        framePaths.append(framePath);
    }
    if (sourceMode == "list" && !framePaths.isEmpty()) {
        m_session->activeFramePaths = framePaths;
        if (!ensureFrameListInput()) {
            MessageDialog::warning(this, tr("Load Failed"), tr("Could not restore autosaved frame list; falling back to folder source."));
            m_session->layoutSourcePath = QDir(folder).absolutePath();
            m_session->layoutSourceIsList = false;
            if (!m_session->frameListPath.isEmpty()) {
                QFile::remove(m_session->frameListPath);
                m_session->frameListPath.clear();
            }
            updateFolderLabel(folder);
        }
    } else {
        m_session->layoutSourcePath = QDir(folder).absolutePath();
        m_session->layoutSourceIsList = false;
        if (!m_session->frameListPath.isEmpty()) {
            QFile::remove(m_session->frameListPath);
            m_session->frameListPath.clear();
        }
        updateFolderLabel(folder);
    }
    scheduleLayoutRebuild(true);
}

void MainWindow::onLoadProject() {
#ifdef Q_OS_WASM
    wasmOpenFileDialog(false);
    return;
#endif
    QMessageBox picker(this);
    picker.setWindowTitle(tr("Load"));
    picker.setIcon(QMessageBox::Question);
    picker.setText(tr("Load a project file or a project folder?"));

    QPushButton* projectFileButton = picker.addButton(tr("Project File..."), QMessageBox::AcceptRole);
    QPushButton* projectFolderButton = picker.addButton(tr("Project Folder..."), QMessageBox::AcceptRole);
    picker.addButton(QMessageBox::Cancel);
    picker.exec();

    if (picker.clickedButton() == projectFileButton) {
        const QString filter = tr("Project Files (project.spart.json *.json);;"
                                  "ZIP Projects (*.zip);;"
                                  "All Supported Files (*.json *.zip)");
        const QString file = QFileDialog::getOpenFileName(this, tr("Load Project File"), "", filter);
        if (!file.isEmpty()) {
            loadProject(file);
        }
        return;
    }

    if (picker.clickedButton() == projectFolderButton) {
        const QString folder = QFileDialog::getExistingDirectory(this, tr("Select Project Folder"));
        if (folder.isEmpty()) {
            return;
        }
        const QString projectFile = QDir(folder).filePath("project.spart.json");
        if (!QFileInfo::exists(projectFile)) {
            MessageDialog::warning(
                this,
                tr("Load Failed"),
                tr("The selected folder does not contain a project.spart.json file."));
            return;
        }
        loadProject(projectFile);
    }
}

void MainWindow::onLoadFromUrl() {
    if (m_isLoading) {
        return;
    }

    bool accepted = false;
    const QString input = QInputDialog::getText(
        this,
        tr("Add Source URL"),
        tr("Enter an image or archive URL:"),
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
        MessageDialog::warning(this, tr("Invalid URL"), tr("The provided URL is not valid."));
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
        MessageDialog::information(
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

    if (m_projectController) m_projectController->addImportTempDir(std::move(tempDir));
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
        MessageDialog::warning(
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
            MessageDialog::warning(this, tr("Import Failed"), error);
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
                MessageDialog::warning(this, tr("Import Failed"), error);
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
    if (m_isLoading || (m_projectController && m_projectController->activeImportReply())) {
        return false;
    }

    m_loadingUiMessage = tr("Downloading from URL...");
    m_isCanceled = false;
    setLoading(true);

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_projectController->importNetworkManager()->get(request);
    m_projectController->setActiveImportReply(reply);

    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total > 0) {
            m_statusLabel->setText(tr("Downloading... %1%").arg((received * 100) / total));
        } else {
            m_statusLabel->setText(tr("Downloading..."));
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, url, action]() {
        if (m_projectController) m_projectController->clearActiveImportReply();
        const QByteArray data = reply->readAll();
        const QString fileName = suggestedNameFromReply(url, reply);
        const bool success = (reply->error() == QNetworkReply::NoError);
        const QString errorText = reply->errorString();
        const bool canceled = (reply->error() == QNetworkReply::OperationCanceledError) || m_isCanceled;
        reply->deleteLater();

        setLoading(false);

        if (!success) {
            if (!canceled) {
#ifdef Q_OS_WASM
                const bool alreadyProxied = url.host() == QStringLiteral("corsproxy.io");
                QString message = errorText;
                if (!data.isEmpty()) {
                    const QJsonDocument doc = QJsonDocument::fromJson(data);
                    if (doc.isObject()) {
                        const QString serverError = doc.object().value("error").toString();
                        if (!serverError.isEmpty()) {
                            message += QStringLiteral("\n\n") + tr("Server response: %1").arg(serverError);
                        }
                    }
                }
                message += QStringLiteral("\n\n")
                    + tr("In the web version, downloads from external servers may be blocked "
                         "by the browser's cross-origin (CORS) security policy. "
                         "You can download the file manually and drop it here, or use Load.");
                auto* msgBox = new QMessageBox(this);
                msgBox->setAttribute(Qt::WA_DeleteOnClose);
                msgBox->setWindowTitle(tr("Download Failed"));
                msgBox->setText(message);
                msgBox->setIcon(QMessageBox::Warning);
                QPushButton* retryBtn = nullptr;
                if (!alreadyProxied) {
                    retryBtn = msgBox->addButton(tr("Retry via CORS Proxy"), QMessageBox::AcceptRole);
                    retryBtn->setToolTip(tr("Routes the download through corsproxy.io, a third-party service. "
                                            "The URL and file contents will be visible to the proxy operator."));
                }
                msgBox->addButton(QMessageBox::Ok);
                connect(msgBox, &QDialog::finished, this, [this, msgBox, retryBtn, url, action]() {
                    if (retryBtn && msgBox->clickedButton() == retryBtn) {
                        const QByteArray encoded = "https://corsproxy.io/?url="
                            + QUrl::toPercentEncoding(url.toString());
                        const QUrl proxiedUrl = QUrl::fromEncoded(encoded);
                        QTimer::singleShot(0, this, [this, proxiedUrl, action]() {
                            tryHandleRemoteUrl(proxiedUrl, action);
                        });
                    }
                });
                msgBox->open();
#else
                MessageDialog::warning(this, tr("Download Failed"), errorText);
#endif
            }
            return;
        }

        QString error;
        const QString path = createManagedImportFile(fileName, data, error);
        if (path.isEmpty()) {
            MessageDialog::warning(this, tr("Import Failed"), error);
            return;
        }

        if (m_projectController) m_projectController->setPendingImportUrl(url.toString());
        finishImportedPath(path, action);
    });

    return true;
}

void MainWindow::onSaveClicked() {
#ifdef Q_OS_WASM
    return;
#else
    const QString projectFilePath = m_projectController ? m_projectController->projectFilePath() : QString();
    if (projectFilePath.isEmpty()) {
        onSaveAsClicked();
        return;
    }
    const QString projectDir = QFileInfo(projectFilePath).absolutePath();
    QString error;
    if (!ProjectSaveService::writeProjectJson(projectDir, buildProjectPayload(m_lastSaveConfig, m_session, true), error)) {
        m_statusLabel->setText(tr("Save failed: ") + error);
        MessageDialog::critical(this, tr("Save Failed"), error);
        return;
    }
    if (m_lastSaveConfig.syncSprites) {
        syncNewSpritesToProjectFolder(projectDir);
    }
    m_statusLabel->setText(tr("Saved to: %1").arg(projectDir));
    if (m_undoStack) m_undoStack->setClean();
    updateUiState();
#endif
}

#ifndef Q_OS_WASM
void MainWindow::onSaveAsClicked() {
    // Start browser in the parent of the current project folder, or the last
    // used parent, so the user naturally creates sibling project folders.
    const QString projectFilePath2 = m_projectController ? m_projectController->projectFilePath() : QString();
    QString startDir;
    if (!projectFilePath2.isEmpty()) {
        startDir = QFileInfo(QFileInfo(projectFilePath2).absolutePath()).absolutePath();
    } else if (!m_settings.defaultProjectsFolder.isEmpty()
               && QDir(m_settings.defaultProjectsFolder).exists()) {
        startDir = m_settings.defaultProjectsFolder;
    } else {
        startDir = QDir(QStandardPaths::writableLocation(
            QStandardPaths::DocumentsLocation)).filePath("Sprat Projects");
    }

    const QString projectDir = QFileDialog::getSaveFileName(
        this, tr("Save Project As"), startDir,
        QString(), nullptr, QFileDialog::DontConfirmOverwrite);
    if (projectDir.isEmpty()) return;

    // Remember the parent so next Save As opens there.
    m_settings.defaultProjectsFolder = QFileInfo(projectDir).absolutePath();
    CliToolsConfig::saveAppSettings(m_settings, m_cliPaths);

    QDir().mkpath(projectDir);

    {
        const QString folderName = QFileInfo(projectDir).fileName();

        QDialog dlg(this);
        dlg.setWindowTitle(tr("Save Project"));
        dlg.setWindowModality(Qt::WindowModal);
        dlg.setMinimumWidth(420);

        auto* iconLabel = new QLabel(&dlg);
        iconLabel->setPixmap(QApplication::style()->standardIcon(QStyle::SP_MessageBoxQuestion).pixmap(64, 64));
        iconLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        iconLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        auto* headlineLabel = new QLabel(tr("Copy images into \"%1\"?").arg(folderName), &dlg);
        headlineLabel->setWordWrap(true);
        QFont headlineFont = headlineLabel->font();
        headlineFont.setBold(true);
        headlineFont.setPointSize(headlineFont.pointSize() + 2);
        headlineLabel->setFont(headlineFont);

        auto* bodyLabel = new QLabel(
            tr("Source images will be saved into <b>sprites/</b> inside the project folder. "
               "The project will be self-contained and portable — open it on any machine. "
               "Uncheck to store the project file only."),
            &dlg);
        bodyLabel->setWordWrap(true);
        bodyLabel->setTextFormat(Qt::RichText);

        auto* cb = new QCheckBox(tr("Copy images into project folder"), &dlg);
        cb->setChecked(true);

        auto* textLayout = new QVBoxLayout();
        textLayout->setSpacing(8);
        textLayout->addWidget(headlineLabel);
        textLayout->addWidget(bodyLabel);
        textLayout->addSpacing(4);
        textLayout->addWidget(cb);
        textLayout->addStretch();

        auto* contentLayout = new QHBoxLayout();
        contentLayout->setSpacing(20);
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->addWidget(iconLabel);
        contentLayout->addLayout(textLayout, 1);

        auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
        buttonBox->button(QDialogButtonBox::Save)->setDefault(true);
        connect(buttonBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

        auto* mainLayout = new QVBoxLayout(&dlg);
        mainLayout->setContentsMargins(24, 24, 24, 16);
        mainLayout->setSpacing(24);
        mainLayout->addLayout(contentLayout);
        mainLayout->addWidget(buttonBox);

        if (dlg.exec() != QDialog::Accepted) return;
        m_lastSaveConfig.syncSprites = cb->isChecked();
    }

    const QString newProjectFilePath = QDir(projectDir).filePath("project.spart.json");
    if (m_projectController) m_projectController->setProjectFilePath(newProjectFilePath);
    m_lastSaveConfig.destination = projectDir;

    if (m_lastSaveConfig.syncSprites) {
        copySpriteToProjectFolder(projectDir);
    }

    const QString folderName = QFileInfo(projectDir).fileName();
    setWindowTitle(tr("%1 — %2[*]").arg(folderName, projectDir));

    QString error;
    if (!ProjectSaveService::writeProjectJson(projectDir, buildProjectPayload(m_lastSaveConfig, m_session, true), error)) {
        m_statusLabel->setText(tr("Save failed: ") + error);
        MessageDialog::critical(this, tr("Save Failed"), error);
        return;
    }
    m_statusLabel->setText(tr("Saved to: %1").arg(projectDir));
    if (m_undoStack) m_undoStack->setClean();
    addToRecentProjects(newProjectFilePath);
    updateUiState();
}
#endif

void MainWindow::onExportClicked() {
    if (m_lastSaveConfig.outputPath.isEmpty()) {
        onExportAsClicked();
        return;
    }
    const QVector<SpratProfile> profs = configuredProfiles();
    if (!ExportDiffDialog::show(this, m_lastSaveConfig,
                                m_session ? m_session->atlases : QVector<AtlasEntry>{},
                                profs))
        return;
    runExport(m_lastSaveConfig);
}

void MainWindow::onExportAsClicked() {
    // Build the config for populate(), applying settings-based defaults for empty fields.
    SaveConfig configForPopulate = m_lastSaveConfig;
    if (configForPopulate.outputPath.isEmpty() && !m_settings.exportDefaultOutputFolder.isEmpty())
        configForPopulate.outputPath = m_settings.exportDefaultOutputFolder;
    if (configForPopulate.transform.isEmpty())
        configForPopulate.transform = m_settings.exportDefaultFormat;
    if (configForPopulate.scaleFilter.isEmpty())
        configForPopulate.scaleFilter = m_settings.exportDefaultScaleFilter;

    QString startDir;
    if (!configForPopulate.outputPath.isEmpty()) {
        startDir = configForPopulate.outputPath;
    } else if (!m_session->currentFolder.isEmpty()) {
        startDir = QFileInfo(m_session->currentFolder).dir().filePath("export");
    } else {
        startDir = QDir::currentPath() + "/export";
    }

    const QVector<SpratProfile> allProfiles = configuredProfiles();
    const QStringList enabled = m_atlasesManagementWorkspace
        ? m_atlasesManagementWorkspace->enabledProfiles() : QStringList();
    QVector<SpratProfile> toExport;
    for (const SpratProfile& p : allProfiles) {
        if (enabled.isEmpty() || enabled.contains(p.name.trimmed()))
            toExport << p;
    }
    m_exportWorkspace->populate(toExport,
                                m_atlasWorkspace->profileCombo() ? m_atlasWorkspace->profileCombo()->currentData().toString() : QString(),
                                configForPopulate,
                                startDir);

    m_exportWorkspace->setPresets(m_exportPresets);

    if (m_session) {
        QList<QPair<int,AtlasExportConfig>> atlasConfigs;
        QStringList atlasNames;
        for (int i = 0; i < m_session->atlases.size(); ++i) {
            const auto& a = m_session->atlases[i];
            if (a.isExcluded || a.spritePaths.isEmpty()) continue;
            atlasConfigs.append({i, a.exportConfig});
            atlasNames.append(a.name.isEmpty() ? tr("Atlas %1").arg(i) : a.name);
        }
        m_exportWorkspace->setAtlasExportConfigs(atlasConfigs, atlasNames);
    }

    switchWorkspace(m_exportWorkspace);
}

void MainWindow::refreshPreview(const QString& profileName, const QString& scaleFilter) {
    if (m_exportCoordinator) m_exportCoordinator->refreshPreview(profileName, scaleFilter);
}

void MainWindow::schedulePreviewPack(const QString& profileName, const QString& scaleFilter) {
    if (m_exportCoordinator) m_exportCoordinator->schedulePreviewPack(profileName, scaleFilter);
}


void MainWindow::onExportWorkspaceRequested(SaveConfig config) {
#ifdef Q_OS_WASM
    QString baseName = m_session ? QFileInfo(m_session->currentFolder).fileName().trimmed() : QString();
    if (baseName.isEmpty()) {
        baseName = "sprat-export";
    }
    config.outputPath = QDir::temp().filePath(baseName + ".zip");
#endif

    const QVector<SpratProfile> profs = configuredProfiles();
    if (!ExportDiffDialog::show(this, config,
                                m_session ? m_session->atlases : QVector<AtlasEntry>{},
                                profs))
        return;

    m_lastSaveConfig = config;
    switchWorkspace(m_atlasWorkspace);
    updateUiState();  // enables Export action now that outputPath is set
    if (m_exportCoordinator) m_exportCoordinator->runExport(m_lastSaveConfig);
}

bool MainWindow::runExport(SaveConfig config) {
    return m_exportCoordinator ? m_exportCoordinator->runExport(std::move(config)) : false;
}

QJsonObject MainWindow::buildProjectPayload(SaveConfig config, ProjectSession* session, bool portable) {
    ProjectPayloadBuildInput input;
    input.currentFolder = session->currentFolder;
    input.sourceFolder = session->sourceFolder;
    input.sources = session->sources;
    input.orphanedSpritePaths = session->orphanedSpritePaths;
    const QString pFilePath = m_projectController ? m_projectController->projectFilePath() : QString();
    input.projectDir = pFilePath.isEmpty()
        ? QString()
        : QFileInfo(pFilePath).absolutePath();
    input.activeFramePaths = session->activeFramePaths;
    input.layoutSourceIsList = session->layoutSourceIsList;
    input.atlases = session->atlases;
    input.activeAtlasIndex = session->activeAtlasIndex;
    input.timelines = session->activeAtlas().timelines;
    input.selectedTimelineIndex = session->selectedTimelineIndex;
    QVector<int> selectedTimelineRows;
    {
        auto* tp = m_frameAnimWorkspace ? m_frameAnimWorkspace->timelinePanel() : nullptr;
        auto* framesList = tp ? tp->timelineFramesList() : nullptr;
        const QList<QListWidgetItem*> selectedFrameItems = framesList ? framesList->selectedItems() : QList<QListWidgetItem*>();
        selectedTimelineRows.reserve(selectedFrameItems.size());
        for (QListWidgetItem* item : selectedFrameItems) {
            selectedTimelineRows.append(framesList->row(item));
        }
        std::sort(selectedTimelineRows.begin(), selectedTimelineRows.end());
    }
    input.selectedTimelineFrameRows = selectedTimelineRows;
    input.animationFrameIndex = m_frameAnimWorkspace ? m_frameAnimWorkspace->animFrameIndex() : 0;
    input.animationPlaying = m_frameAnimWorkspace ? m_frameAnimWorkspace->isAnimPlaying() : false;
    input.selectedSprite = session->selectedSprite;
    for (const auto& sprite : session->selectedSprites) {
        if (sprite) {
            input.selectedSpritePaths.append(sprite->path);
        }
    }
    input.primarySelectedSpritePath = session->selectedSprite ? session->selectedSprite->path : QString();
    input.selectedPointName = session->selectedPointName;
    input.layoutModels = session->activeAtlas().layoutModels;
    input.layoutOutput = session->cachedLayoutOutput;
    input.layoutScale = session->cachedLayoutScale;
    input.profile = m_atlasWorkspace->profileCombo() ? m_atlasWorkspace->profileCombo()->currentData().toString() : QString();
    SpratProfile selectedProfile;
    const bool hasSelectedProfile = selectedProfileDefinition(selectedProfile);
    input.padding = hasSelectedProfile ? selectedProfile.padding : 0;
    input.trimTransparent = hasSelectedProfile ? selectedProfile.trimTransparent : false;
    int sourceResolutionWidth = 0;
    int sourceResolutionHeight = 0;
    auto* sourceResolutionCombo = m_atlasWorkspace->sourceResolutionCombo();
    if (sourceResolutionCombo && parseResolutionText(sourceResolutionCombo->currentText(), sourceResolutionWidth, sourceResolutionHeight)) {
        input.sourceResolutionWidth = sourceResolutionWidth;
        input.sourceResolutionHeight = sourceResolutionHeight;
    }
    input.layoutZoom = m_layoutZoom / 100.0;
    auto* spriteEditorPanel = m_atlasWorkspace->spriteEditorPanel();
    input.previewZoom = spriteEditorPanel ? spriteEditorPanel->previewZoomSpin()->value() / 100.0 : 1.0;
    auto* ap = m_frameAnimWorkspace ? m_frameAnimWorkspace->animPanel() : nullptr;
    auto* animZoomSpin = ap ? ap->zoomSpin() : nullptr;
    input.animationZoom = animZoomSpin ? animZoomSpin->value() / 100.0 : 1.0;
    // Export canvas zoom: use live canvas zoom when in export workspace, otherwise the last saved value.
    input.exportZoom = (m_currentWorkspace == m_exportWorkspace && m_exportLayoutCanvas && m_exportLayoutCanvas->zoom() > 0.0)
        ? m_exportLayoutCanvas->zoom()
        : (m_exportWorkspace ? m_exportWorkspace->savedZoom() : 0.0);
    input.dockState = saveState();
    input.appSettings = m_settings;
    input.cliPaths = m_cliPaths;
    input.saveConfig = config;
    input.portablePaths = portable;
    input.exportPresets   = m_exportPresets;
    input.markerTemplates = m_atlasWorkspace ? m_atlasWorkspace->markerRepository()->markerTemplates() : QVector<MarkerTemplate>{};
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
    if (AutosaveProjectStore::save(getAutosaveFilePath(), buildProjectPayload(m_lastSaveConfig, m_session, /*portable=*/true), error)) {
        m_statusLabel->setText(tr("Autosaved project"));
    } else {
        m_statusLabel->setText(error);
    }
}

void MainWindow::loadProject(const QString& path, DropAction action) {
    if (action == DropAction::Cancel) {
        return;
    }
    auto* ap0 = m_frameAnimWorkspace ? m_frameAnimWorkspace->animPanel() : nullptr;
    if (ap0 && ap0->animCanvas())
        ap0->animCanvas()->setZoomManual(false);
    if (m_atlasWorkspace->canvas()) m_atlasWorkspace->canvas()->setZoomManual(false);
    auto* sprEd = m_atlasWorkspace->spriteEditorPanel();
    if (sprEd && sprEd->previewCanvas()) sprEd->previewCanvas()->setZoomManual(false);

    const QString lowerPath = path.toLower();

    // ZIP files: try loading as a project first (checks for project.spart.json).
    // If that fails, onProjectLoadFinished falls back to loadImagesFromZip.
    // Other archive formats and images are delegated directly.
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

    if (m_projectController->isProjectLoadRunning()) {
        return;
    }

    m_loadingUiMessage = tr("Loading project...");
    setLoading(true);

#ifdef Q_OS_WASM
    // QtConcurrent can be unreliable without pthreads/COOP+COEP; run synchronously
    // via a deferred timer so the loading overlay paints before we block.
    QTimer::singleShot(0, this, [this, path, action]() {
        ProjectController::ProjectLoadResult result;
        result.path = path;
        result.action = action;
        result.success = ProjectFileLoader::load(path, result.root, result.error);
        processProjectLoadResult(result);
    });
#else
    m_projectController->launchProjectLoad(path, action);
#endif
}

void MainWindow::onProjectLoadFinished() {
    processProjectLoadResult(m_projectController->projectLoadResult());
}

void MainWindow::processProjectLoadResult(const ProjectController::ProjectLoadResult& result) {
    const QString path = result.path;
    const DropAction action = result.action;

    if (!result.success) {
        if (path.endsWith(".zip", Qt::CaseInsensitive)) {
            // Clear loading state before fallback — loadImagesFromZip
            // checks m_isLoading and would reject the call otherwise.
            setLoading(false);
            if (!loadImagesFromZip(path, action)) {
                MessageDialog::warning(this, tr("Load Failed"), tr("Could not load ZIP as a project or as an image archive."));
            }
            return;
            }
            setLoading(false);
            MessageDialog::warning(this, tr("Load Failed"), result.error);
            return;    }

    // Add to recent projects
    addToRecentProjects(path);

    // Track project file path and prefer sprites subfolder
    if (m_projectController) m_projectController->setProjectFilePath(path);
    if (m_projectController) m_projectController->setSourceFolderIsTemp(false);

    QJsonObject root = result.root;
    const int projectVersion = root["version"].toInt(1);
    const bool isZipProject = path.endsWith(".zip", Qt::CaseInsensitive);

    // For ZIP projects with embedded sprites (v2+), extract the entire archive
    // to a temp dir so sprites become accessible on disk.
    QString projectDir = QFileInfo(path).absolutePath();
    if (isZipProject && projectVersion >= 2) {
        auto tempDir = std::make_unique<QTemporaryDir>();
        if (!tempDir->isValid()) {
            setLoading(false);
            MessageDialog::warning(this, tr("Load Failed"), tr("Could not create temporary directory for ZIP extraction."));
            return;
        }
        const QString tempPath = tempDir->path();
        QString extractError;
        if (!ArchiveExtractor::extractToDirectory(path, tempPath, extractError)) {
            setLoading(false);
            MessageDialog::warning(this, tr("Load Failed"), tr("Could not extract ZIP: %1").arg(extractError));
            return;
        }
        projectDir = tempPath;
        const QString spritesDir = QDir(tempPath).filePath("sprites");
        if (QDir(spritesDir).exists()) {
            m_session->sourceFolder = spritesDir;
        }
        if (m_projectController) m_projectController->setSourceFolderIsTemp(true);
        m_projectController->addTempDir(std::move(tempDir));
    } else {
        // Prefer a pre-existing sprites subfolder next to the project file
        const QString spritesDir = QDir(projectDir).filePath("sprites");
        if (QDir(spritesDir).exists()) {
            m_session->sourceFolder = spritesDir;
        }
    }
    // If no sprites dir found, applyProjectPayload will set sourceFolder from layoutSourcePath

    QJsonObject layoutInfo = root["layout"].toObject();
    QString folder = layoutInfo["folder"].toString();

    // Resolve relative folder paths (version 2+) against the project directory.
    // If the resolved subfolder doesn't exist (e.g. older saves that stored
    // "sprites" but never copied files there), fall back to the project root so
    // frame paths are at least resolved against a real directory.
    if (projectVersion >= 2 || QDir::isRelativePath(folder)) {
        const QString resolved = QDir(projectDir).filePath(folder);
        folder = QDir(resolved).exists() ? resolved : projectDir;
    }

    QStringList framePaths;
    for (const auto& frameVal : layoutInfo["frame_paths"].toArray()) {
        QString framePath = frameVal.toString().trimmed();
        if (framePath.isEmpty()) {
            continue;
        }
        // Resolve relative frame paths against the resolved folder
        if (projectVersion >= 2 || QDir::isRelativePath(framePath)) {
            framePath = QDir(folder).filePath(framePath);
        }
        framePaths.append(framePath);
    }

    if (action == DropAction::Merge) {
        setLoading(false);
        if (!framePaths.isEmpty()) {
            m_session->activeFramePaths.append(framePaths);
            scheduleLayoutRebuild(true);
        }
        return;
    }

    // Clear undo history when loading a new project
    if (m_undoStack) m_undoStack->clear();

    // Clear selection state when loading a new project to avoid stale pointers
    m_session->selectedSprite.reset();
    m_session->selectedSprites.clear();
    m_session->selectedPointName.clear();
    // Update UI to clear sprite selection display
    onSpriteSelected(SpritePtr());

    m_session->pendingProjectPayload = root;
    cacheLayoutOutputFromPayload(root);

    QJsonObject layoutOpts = root["layout_options"].toObject();
    auto* profileCombo = m_atlasWorkspace->profileCombo();
    if (layoutOpts.contains("profile")) {
        const QString profile = layoutOpts["profile"].toString();
        if (!profile.isEmpty() && profileCombo->findData(profile) < 0) {
            profileCombo->addItem(profile, profile);
        }
        profileCombo->setCurrentIndex(profileCombo->findData(profile));
        if (m_profileSelectorStack) {
            m_profileSelectorStack->setCurrentIndex(profileCombo->count() > 0 ? 0 : 1);
        }
    }
    m_currentProfile = profileCombo ? profileCombo->currentData().toString() : QString();

    int sourceResolutionWidth = 0;
    int sourceResolutionHeight = 0;
    const bool hasSourceResolution = parseResolutionText(layoutOpts["source_resolution"].toString(), sourceResolutionWidth, sourceResolutionHeight);
    syncSourceResolutionPresetSelection(
        m_atlasWorkspace->sourceResolutionCombo(),
        hasSourceResolution ? sourceResolutionWidth : 1024,
        hasSourceResolution ? sourceResolutionHeight : 1024);
    if (m_atlasWorkspace->sourceResolutionCombo()) {
        m_atlasWorkspace->sourceResolutionCombo()->setEnabled(true);
        m_currentResolution = m_atlasWorkspace->sourceResolutionCombo()->currentText();
    }
    
    const QString sourceMode = layoutInfo["source_mode"].toString();
    if (!folder.isEmpty()) {
        m_session->currentFolder = folder;
        if (!QDir(folder).exists()) {
            // Source folder missing: restore project settings but skip layout rebuild.
            // A folder will be assigned when sprites are loaded.
            setLoading(false);
            applyProjectPayload();
        } else {
            if (sourceMode == "list" && !framePaths.isEmpty()) {
                m_session->activeFramePaths = framePaths;
                if (!ensureFrameListInput()) {
                    MessageDialog::warning(this, tr("Load Failed"), tr("Could not restore saved frame list; falling back to folder source."));
                    m_session->layoutSourcePath = QDir(folder).absolutePath();
                    m_session->layoutSourceIsList = false;
                    if (!m_session->frameListPath.isEmpty()) {
                        QFile::remove(m_session->frameListPath);
                        m_session->frameListPath.clear();
                    }
                    updateFolderLabel(folder);
                }
            } else {
                m_session->layoutSourcePath = QDir(folder).absolutePath();
                m_session->layoutSourceIsList = false;
                if (!m_session->frameListPath.isEmpty()) {
                    QFile::remove(m_session->frameListPath);
                    m_session->frameListPath.clear();
                }
                updateFolderLabel(folder);
            }
            scheduleLayoutRebuild(true);
        }
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

    if (m_projectController->isZipDiscoveryRunning()) {
        return false;
    }

    // For Merge, skip extraction entirely if this ZIP is already a loaded source.
    if (action == DropAction::Merge && m_session) {
        for (const ProjectSource& s : m_session->sources) {
            if (s.originalPath == zipPath) {
                onRunLayout(true);
                return true;
            }
        }
    }

    m_loadingUiMessage = tr("Extracting ZIP...");
    m_isCanceled = false;
    setLoading(true);

    if (action == DropAction::Replace) {
        m_projectController->clearTempDirs();
    }
    auto tempDir = std::make_unique<QTemporaryDir>();
    if (!tempDir->isValid()) {
        setLoading(false);
        MessageDialog::warning(this, tr("Load Failed"), tr("Unable to create temporary directory for ZIP extraction."));
        return false;
    }
    QString tempPath = tempDir->path();
    m_projectController->addTempDir(std::move(tempDir));

#ifdef Q_OS_WASM
    auto zipTaskWasm = [this, zipPath, tempPath, action]() {
        ProjectController::ZipDiscoveryResult result;
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
    // QtConcurrent can be unreliable without pthreads/COOP+COEP; run synchronously.
    processZipDiscoveryResult(zipTaskWasm());
#else
    m_projectController->launchZipDiscovery(zipPath, action, tempPath);
#endif
    return true;
}

void MainWindow::onZipDiscoveryFinished() {
    processZipDiscoveryResult(m_projectController->zipDiscoveryResult());
}

void MainWindow::processZipDiscoveryResult(const ProjectController::ZipDiscoveryResult& result) {
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
            MessageDialog::warning(this, tr("Load Failed"), tr("Could not extract ZIP: %1").arg(result.error));
        } else {
            MessageDialog::warning(this, tr("Load Failed"), tr("Could not extract ZIP or no image folders found."));
        }
        return;
    }

    // Hide loading overlay before showing the folder selection dialog
    setLoading(false);

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
        QPushButton* okButton = buttons->button(QDialogButtonBox::Ok);
        okButton->setEnabled(false);
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

        connect(tree, &QTreeWidget::itemChanged, tree, [&]() {
            bool anyChecked = false;
            for (auto it = items.cbegin(); it != items.cend(); ++it) {
                if (it.value()->checkState(0) == Qt::Checked) {
                    anyChecked = true;
                    break;
                }
            }
            okButton->setEnabled(anyChecked);
        });

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
            MessageDialog::warning(this, tr("Load Failed"), tr("No folders selected."));
            return;
        }
    }

    const QStringList absolutePaths = ImageDiscoveryService::collectImagesRecursive(selectedFolders);
    if (absolutePaths.isEmpty()) {
        setLoading(false);
        MessageDialog::warning(this, tr("Load Failed"), tr("No images found in selected folders."));
        return;
    }

    m_loadingUiMessage = tr("Loading images...");
    setLoading(true);
    
    // Determine the common root of extracted images for subfolder preservation.
    // Use the first selected folder if single, or tempPath for multiple.
    const QString extractionRoot = (selectedFolders.size() == 1)
        ? selectedFolders.first()
        : tempPath;

    if (action == DropAction::Merge) {
        // Set currentFolder before copying so relative structure is preserved
        m_session->currentFolder = extractionRoot;
        const QString subName = m_projectController->computeSourceSubfolderName(zipPath);
        const QString subfolderPath = QDir(m_session->sourceFolder).filePath(subName);
        const QStringList copied = m_projectController->copyFramesToSourceSubfolder(
            absolutePaths, subfolderPath, m_mergeReplaceAllDuplicates);
        m_session->activeFramePaths.append(copied);
        m_projectController->registerLoadedSource(zipPath, action, subfolderPath);
    } else {
        // On Replace, delete all contents from sprites folder (including subdirectories)
        if (!m_session->sourceFolder.isEmpty()) {
            QDir dir(m_session->sourceFolder);
            dir.removeRecursively();
            QDir().mkpath(m_session->sourceFolder);
        }

        if (m_projectController && m_projectController->isSourceFolderTemp()) {
            m_projectController->setProjectFilePath(QString());
        }
        if (m_projectController) m_projectController->setSourceFolderIsTemp(false);
        if (!m_session->frameListPath.isEmpty()) {
            QFile::remove(m_session->frameListPath);
            m_session->frameListPath.clear();
        }
        m_session->sourceFolder.clear();
        m_projectController->clearSourceFolderTempDir();
        ensureSourceFolder();
        m_session->activeAtlas().timelines.clear();
        m_session->selectedTimelineIndex = -1;
        // Clear selection state when loading new images to avoid stale pointers
        m_session->selectedSprite.reset();
        m_session->selectedSprites.clear();
        m_session->selectedPointName.clear();
        // Update UI to clear sprite selection display
        onSpriteSelected(SpritePtr());
        refreshTimelineList();
        refreshAnimationTest();
        // Set currentFolder so relative subfolder structure is preserved
        m_session->currentFolder = extractionRoot;
        // Copy extracted images into a dedicated subfolder on Replace
        const QString subName = m_projectController->computeSourceSubfolderName(zipPath);
        const QString subfolderPath = QDir(m_session->sourceFolder).filePath(subName);
        m_session->activeFramePaths = m_projectController->copyFramesToSourceSubfolder(absolutePaths, subfolderPath);
        m_projectController->registerLoadedSource(zipPath, action, subfolderPath);
    }

    if (!ensureFrameListInput()) {
        setLoading(false);
        MessageDialog::warning(this, tr("Load Failed"), tr("Could not create temporary frame list from ZIP selection."));
        return;
    }
    updateManualFrameLabel();
    m_statusLabel->setText(QString(tr("Loaded %1 image frame(s) from ZIP")).arg(absolutePaths.size()));
    scheduleLayoutRebuild(true);
}

void MainWindow::promoteSourceFolderAfterSave(const QString& saveDestination) {
    QFileInfo destInfo(saveDestination);
    const QString projectDir = destInfo.isDir()
        ? saveDestination
        : destInfo.absolutePath();
    const QString newSpritesPath = QDir(projectDir).filePath("sprites");

    if (m_session->sourceFolder == newSpritesPath) return; // already correct

    // Only promote if the sprites directory actually exists on disk.
    // For ZIP saves, sprites are written to a temp dir that gets zipped and deleted,
    // so the newSpritesPath won't exist — keep current paths intact.
    if (!QDir(newSpritesPath).exists()) return;

    // The save service already copied sprites into newSpritesPath.
    // Update the session to point there and rebuild frame paths accordingly.
    const QString oldFolder = m_session->sourceFolder;
    m_session->sourceFolder = newSpritesPath;
    m_session->currentFolder = newSpritesPath;
    if (m_projectController) m_projectController->setSourceFolderIsTemp(false);

    // Rewrite activeFramePaths to reference the new sprites location
    if (!oldFolder.isEmpty() && !m_session->activeFramePaths.isEmpty()) {
        QDir oldDir(oldFolder);
        QStringList updated;
        updated.reserve(m_session->activeFramePaths.size());
        for (const QString& fp : m_session->activeFramePaths) {
            QString rel = oldDir.relativeFilePath(fp);
            if (rel.startsWith("..")) {
                rel = QFileInfo(fp).fileName();
            }
            updated.append(QDir(newSpritesPath).filePath(rel));
        }
        m_session->activeFramePaths = updated;
    }

    // Rebuild frame list input if in list mode
    if (m_session->layoutSourceIsList) {
        ensureFrameListInput();
    } else {
        m_session->layoutSourcePath = newSpritesPath;
    }

    // Release old temp dir if applicable
    m_projectController->clearSourceFolderTempDir();

    // The cached layout may reference paths inside the now-deleted temp dir.
    // Clear it so the next save runs spratlayout fresh against the promoted paths.
    m_session->cachedLayoutOutput.clear();
    m_session->cachedLayoutScale = 1.0;

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
    ProjectPayloadApplyResult applied = ProjectPayloadCodec::applyToLayout(root, m_session->currentFolder, m_session->activeAtlas().layoutModels);

    m_layoutZoom = applied.layoutZoom * 100.0;
    onLayoutZoomChanged(m_layoutZoom);
    auto* spriteEditorPanel2 = m_atlasWorkspace->spriteEditorPanel();
    if (spriteEditorPanel2 && spriteEditorPanel2->previewZoomSpin()) {
        spriteEditorPanel2->previewZoomSpin()->setValue(applied.previewZoom * 100.0);
    }
    // Store animation zoom for deferred restore when FrameAnimation workspace is entered.
    if (m_frameAnimWorkspace)
        m_frameAnimWorkspace->setRestoreZoom(applied.animationZoom);
    auto* ap2 = m_frameAnimWorkspace ? m_frameAnimWorkspace->animPanel() : nullptr;
    auto* animZoomSpin2 = ap2 ? ap2->zoomSpin() : nullptr;
    if (animZoomSpin2) {
        animZoomSpin2->blockSignals(true);
        animZoomSpin2->setValue(applied.animationZoom * 100.0);
        animZoomSpin2->blockSignals(false);
    }
    if (ap2 && ap2->animCanvas())
        ap2->animCanvas()->setZoomManual(false);
    if (m_exportWorkspace) m_exportWorkspace->setSavedZoom(applied.exportZoom);
    syncSourceResolutionPresetSelection(
        m_atlasWorkspace->sourceResolutionCombo(),
        applied.sourceResolutionWidth > 0 ? applied.sourceResolutionWidth : 1024,
        applied.sourceResolutionHeight > 0 ? applied.sourceResolutionHeight : 1024);
    if (m_atlasWorkspace->sourceResolutionCombo()) {
        m_atlasWorkspace->sourceResolutionCombo()->setEnabled(true);
    }
    if (!applied.dockState.isEmpty()) {
        restoreState(applied.dockState);
    }

    m_settings = applied.appSettings;
    m_lastSaveConfig = applied.saveConfig;
    if (m_atlasesManagementWorkspace)
        m_atlasesManagementWorkspace->setProfilesGlobal(m_lastSaveConfig.profilesGlobal);
    m_exportPresets   = applied.exportPresets;
    if (m_atlasWorkspace)
        m_atlasWorkspace->markerRepository()->setMarkerTemplates(applied.markerTemplates);
    applySettings();

    // Restore sources
    if (!applied.sources.isEmpty()) {
        m_session->sources = applied.sources;
        // Keep sourceFolder in sync with the first source path if it's a folder
        if (m_session->sourceFolder.isEmpty() && m_session->sources.first().type == SourceType::Folder) {
            m_session->sourceFolder = m_session->sources.first().originalPath;
        }
    }
    m_session->orphanedSpritePaths = applied.orphanedSpritePaths;

    // Initialize source folder for sync if layoutSourcePath points to a folder
    if (!m_session->layoutSourcePath.isEmpty()) {
        QFileInfo fi(m_session->layoutSourcePath);
        if (fi.isDir()) {
            m_session->sourceFolder = fi.absoluteFilePath();
            // Ensure the first source mirrors the source folder
            if (m_session->sources.isEmpty()) {
                ProjectSource src;
                src.name = fi.fileName();
                src.type = SourceType::Folder;
                src.originalPath = m_session->sourceFolder;
                m_session->sources.append(src);
            } else if (m_session->sources.first().type == SourceType::Folder) {
                m_session->sources.first().originalPath = m_session->sourceFolder;
            }
            initializeSourceFolderWatcher();
        }
    }

    if (m_cliSetup) {
        bool allFound = m_cliSetup->resolveQuietly();
        // Update local mirror from controller
        m_cliPaths = m_cliSetup->paths();
        m_spratLayoutBin  = m_cliPaths.layoutBinary;
        m_spratPackBin    = m_cliPaths.packBinary;
        m_spratConvertBin = m_cliPaths.convertBinary;
        m_spratFramesBin  = m_cliPaths.framesBinary;
        m_spratUnpackBin  = m_cliPaths.unpackBinary;
        if (m_layoutOrchestrator) m_layoutOrchestrator->updateLayoutBinary(m_spratLayoutBin);
        QString storedVersion = CliToolsConfig::loadInstalledCliVersion();
        m_cliReady = allFound
                     && !storedVersion.isEmpty()
                     && storedVersion == QString(SPRAT_CLI_VERSION);
    }
    m_statusLabel->setText(m_cliReady ? tr("CLI ready") : tr("CLI tools not ready"));
    updateUiState();

    // Restore atlas structure
    if (!applied.atlases.isEmpty()) {
        // Preserve runtime layoutModels generated by the layout tool for the active atlas
        const QVector<LayoutModel> runtimeModels = m_session->activeAtlas().layoutModels;
        m_session->atlases = applied.atlases;
        m_session->activeAtlasIndex = qBound(0, applied.activeAtlasIndex,
                                              m_session->atlases.size() - 1);
        // Re-attach runtime layout models
        m_session->activeAtlas().layoutModels = runtimeModels;
        // For v1-3 migration: neutral atlas spritePaths was left empty; populate from activeFramePaths
        {
            const int neutralIdx = m_session->neutralAtlasIndex();
            AtlasEntry& neutral = m_session->atlases[neutralIdx];
            if (neutral.spritePaths.isEmpty() && !m_session->activeFramePaths.isEmpty()) {
                neutral.spritePaths = m_session->activeFramePaths;
            }
        }
        emit m_session->atlasesChanged();
    }
    // Always restore the active atlas's timelines (already populated for both v4 and v1-3)
    m_session->activeAtlas().timelines = applied.timelines;
    m_session->selectedTimelineIndex = (applied.selectedTimelineIndex >= 0
        && applied.selectedTimelineIndex < m_session->activeAtlas().timelines.size())
        ? applied.selectedTimelineIndex
        : (m_session->activeAtlas().timelines.isEmpty() ? -1 : 0);

    auto* tp2 = m_frameAnimWorkspace ? m_frameAnimWorkspace->timelinePanel() : nullptr;
    if (tp2)
        tp2->selectTimeline(m_session->selectedTimelineIndex);
    else
        refreshTimelineList();

    if (m_session->selectedTimelineIndex >= 0 && m_session->selectedTimelineIndex < m_session->activeAtlas().timelines.size() && !applied.selectedTimelineFrameRows.isEmpty()) {
        auto* framesList = tp2 ? tp2->timelineFramesList() : nullptr;
        for (int row : applied.selectedTimelineFrameRows) {
            if (framesList && row >= 0 && row < framesList->count()) {
                if (QListWidgetItem* item = framesList->item(row)) {
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
        if (m_atlasWorkspace->canvas()) {
            m_atlasWorkspace->canvas()->selectSpritesByPaths(selectedPaths, primaryPath);
        }
    }

    if (m_frameAnimWorkspace) {
        int frameIndex = qMax(0, applied.animationFrameIndex);
        if (m_session->selectedTimelineIndex >= 0 && m_session->selectedTimelineIndex < m_session->activeAtlas().timelines.size()) {
            const int frameCount = m_session->activeAtlas().timelines[m_session->selectedTimelineIndex].frames.size();
            frameIndex = frameCount > 0 ? qBound(0, frameIndex, frameCount - 1) : 0;
        }
        m_frameAnimWorkspace->setAnimFrameIndex(frameIndex);
        if (applied.animationPlaying && m_session->selectedTimelineIndex >= 0 && m_session->selectedTimelineIndex < m_session->activeAtlas().timelines.size()) {
            m_frameAnimWorkspace->onAnimPlayPauseClicked();
        } else if (m_frameAnimWorkspace->isAnimPlaying()) {
            m_frameAnimWorkspace->onAnimPlayPauseClicked();
        }
    }
    refreshAnimationTest();
    m_isRestoringProject = false;
}

void MainWindow::onAutosaveTimer() {
    autosaveProject();
}

void MainWindow::copySpriteToProjectFolder(const QString& projectDir) {
    const QString spritesPath = QDir(projectDir).filePath(QStringLiteral("sprites"));
    QDir().mkpath(spritesPath);

    QStringList frames;
    if (m_session->layoutSourceIsList) {
        frames = m_session->activeFramePaths;
    } else if (!m_session->sourceFolder.isEmpty()) {
        frames = ImageDiscoveryService::collectImagesRecursive({m_session->sourceFolder});
    }
    if (frames.isEmpty()) return;

    // Copy frames preserving their path structure relative to sourceFolder.
    // Each source already lives in its own named subfolder of sourceFolder
    // (e.g. <sourceFolder>/archive/anim/run.png), so this mirrors that layout
    // into <projectDir>/sprites/archive/anim/run.png.
    const QDir srcRoot(m_session->sourceFolder);
    const QDir dstRoot(spritesPath);
    for (const QString& p : frames) {
        const QString abs = QFileInfo(p).absoluteFilePath();
        const QString rel = srcRoot.relativeFilePath(abs);
        if (rel.startsWith("..")) continue; // outside sourceFolder — unexpected, skip
        const QString dst = dstRoot.filePath(rel);
        QDir().mkpath(QFileInfo(dst).absolutePath());
        if (!QFileInfo::exists(dst)) {
            if (!QFile::copy(abs, dst))
                qWarning() << "[syncNewSpritesToProjectFolder] Failed to copy" << abs << "to" << dst;
        }
    }

    // Update each source's cachedFolderPath to its new location inside sprites/
    for (ProjectSource& src : m_session->sources) {
        if (src.cachedFolderPath.isEmpty()) continue;
        const QString rel = srcRoot.relativeFilePath(
            QDir(src.cachedFolderPath).canonicalPath());
        if (!rel.startsWith("..")) {
            src.cachedFolderPath = dstRoot.filePath(rel);
        }
    }

    // promoteSourceFolderAfterSave uses sourceFolder as base to remap activeFramePaths —
    // the same base we used above — then updates sourceFolder/currentFolder/layoutSourcePath.
    promoteSourceFolderAfterSave(projectDir);
    if (m_session->layoutSourceIsList) {
        ensureFrameListInput();
    }
}

void MainWindow::syncNewSpritesToProjectFolder(const QString& projectDir) {
    const QString spritesPath = QDir(projectDir).filePath(QStringLiteral("sprites"));
    const QString canonicalSprites = QDir(spritesPath).canonicalPath();

    if (m_session->layoutSourceIsList) {
        // Collect frames that live outside the project sprites folder
        QStringList outside;
        for (const QString& p : m_session->activeFramePaths) {
            const QString abs = QFileInfo(p).absoluteFilePath();
            if (canonicalSprites.isEmpty() || !abs.startsWith(canonicalSprites + QLatin1Char('/')))
                outside.append(p);
        }
        if (outside.isEmpty()) return;

        // Copy each outside frame into its source's named subfolder inside sprites/,
        // preserving the structure within that source's cache directory.
        const QDir dstRoot(spritesPath);
        QHash<QString, QString> remapped;
        for (const QString& p : outside) {
            const QString abs = QFileInfo(p).absoluteFilePath();
            QString rel;
            for (const ProjectSource& src : m_session->sources) {
                if (src.cachedFolderPath.isEmpty()) continue;
                const QString canon = QDir(src.cachedFolderPath).canonicalPath();
                if (!canon.isEmpty() && abs.startsWith(canon + QLatin1Char('/'))) {
                    rel = src.name + QLatin1Char('/') +
                          QDir(src.cachedFolderPath).relativeFilePath(abs);
                    break;
                }
            }
            if (rel.isEmpty() || rel.startsWith(".."))
                rel = QFileInfo(p).fileName(); // fallback: flat in sprites/
            const QString dst = dstRoot.filePath(rel);
            QDir().mkpath(QFileInfo(dst).absolutePath());
            if (!QFileInfo::exists(dst)) {
                if (!QFile::copy(abs, dst)) continue;
            }
            remapped[p] = dst;
        }
        if (remapped.isEmpty()) return;
        for (QString& p : m_session->activeFramePaths)
            if (remapped.contains(p)) p = remapped[p];
        ensureFrameListInput();
    } else {
        // Folder mode: if the source folder is already the project sprites folder, nothing to do
        if (!canonicalSprites.isEmpty() &&
            QDir(m_session->sourceFolder).canonicalPath() == canonicalSprites)
            return;
        // Still pointing to an external folder — copy everything in
        copySpriteToProjectFolder(projectDir);
    }
}

// ---------------------------------------------------------------------------
// Workspace switching — IWorkspace-based lifecycle
// ---------------------------------------------------------------------------

void MainWindow::applyWorkspaceLayout(IWorkspace* ws)
{
    // Capture Frame Animation dock widths before potentially hiding the animation dock.
    // m_animationDock is only visible when Frame Animation is active, so this is
    // a reliable guard against saving state from other workspaces.
    if (m_atlasDock && m_animationDock && m_animationDock->isVisible()) {
        m_savedAtlasDockW = m_atlasDock->width();
        m_savedAnimDockW  = m_animationDock->width();
    }

    auto* atlasSplitter = m_atlasWorkspace ? m_atlasWorkspace->atlasSplitter() : nullptr;
    auto* editorContent = m_atlasWorkspace ? m_atlasWorkspace->spriteEditorPanel() : nullptr;
    if (ws == m_atlasWorkspace) {
        // AtlasWorkspace::leave() already saved splitter H sizes; restore to Horizontal here
        if (atlasSplitter) atlasSplitter->setOrientation(Qt::Horizontal);
        if (m_atlasDock)     m_atlasDock->show();
        if (m_animationDock) m_animationDock->hide();
    } else if (ws == m_frameAnimWorkspace) {
        // AtlasWorkspace::leave() already saved splitter H sizes; flip to Vertical now
        if (atlasSplitter)  atlasSplitter->setOrientation(Qt::Vertical);
        if (editorContent)  editorContent->hide();
        if (m_atlasDock)      m_atlasDock->show();
        if (m_animationDock)  m_animationDock->show();
        // Restore dock widths saved when leaving Frame Animation; prevents the atlas
        // dock from staying expanded after the animation dock was hidden in Sprites.
        if (m_savedAtlasDockW > 0 && m_savedAnimDockW > 0)
            resizeDocks({m_atlasDock, m_animationDock},
                        {m_savedAtlasDockW, m_savedAnimDockW}, Qt::Horizontal);
    } else if (ws == m_exportWorkspace) {
        if (m_atlasDock)     m_atlasDock->hide();
        if (m_animationDock) m_animationDock->hide();
        if (m_debugDock)     m_debugDock->hide();
        if (m_exportWorkspace && m_session) {
            QStringList atlasNames;
            QList<int>  sessionIndices;
            for (int i = 0; i < m_session->atlases.size(); ++i) {
                const auto& a = m_session->atlases[i];
                if (a.isExcluded || a.spritePaths.isEmpty()) continue;
                atlasNames.append(a.name);
                sessionIndices.append(i);
            }
            m_exportWorkspace->setAtlasNames(atlasNames, m_session->activeAtlasIndex, sessionIndices);
        }
        m_mainStack->setCurrentIndex(1);
        m_mainStack->show();
    } else if (ws == m_atlasesManagementWorkspace) {
        if (m_atlasDock)     m_atlasDock->hide();
        if (m_animationDock) m_animationDock->hide();
        if (m_session) {
            m_projectController->syncFramePathsToNeutralAtlas(DropAction::Merge);
            m_atlasesManagementWorkspace->setSources(m_session->sources);
            m_atlasesManagementWorkspace->setAtlases(
                m_session->atlases, m_session->activeAtlasIndex);
        }
        m_mainStack->setCurrentIndex(2);
        m_mainStack->show();
    }
}

void MainWindow::switchWorkspace(IWorkspace* next)
{
    if (m_currentWorkspace == next) return;

    IWorkspace* prev = m_currentWorkspace;

    // Leave current workspace (saves view state via enter/leave contract)
    if (prev) {
        prev->leave();

        // Workspace-specific cleanup not expressible inside the workspace itself
        if (prev == m_exportWorkspace) {
            if (m_exportCoordinator) {
                m_exportCoordinator->setExportWorkspaceActive(false);
                m_exportCoordinator->setExportPreviewAtlasIndex(-1);
                m_exportCoordinator->cancelPreview();
            }
            if (m_packedAtlasView) {
                m_packedAtlasView->setParent(this);
                m_packedAtlasView->hide();
            }
            if (m_exportLayoutCanvas) {
                m_exportLayoutCanvas->setLoadingHint(false);
                m_exportLayoutCanvas->setParent(this);
                m_exportLayoutCanvas->hide();
            }
            m_mainStack->setCurrentIndex(0);
        } else if (prev == m_atlasesManagementWorkspace) {
            if (m_atlasesManagementWorkspace &&
                    m_atlasesManagementWorkspace->viewMode()
                    == AtlasesManagementWorkspace::ViewMode::Layout) {
                auto* canvas = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
                auto* atlasViewStack = m_atlasWorkspace ? m_atlasWorkspace->atlasViewStack() : nullptr;
                if (canvas) canvas->setDimFilter(QString());
                m_atlasesManagementWorkspace->clearCanvasWidget();
                if (canvas && atlasViewStack && atlasViewStack->widget(0)) {
                    QWidget* cc = atlasViewStack->widget(0);
                    canvas->setParent(cc);
                    if (auto* l = cc->layout()) l->addWidget(canvas);
                    canvas->show();
                }
            }
            if (m_session)
                m_session->activeAtlasIndex = m_session->neutralAtlasIndex();
            if (m_layoutOrchestrator)
                m_layoutOrchestrator->stopAndClearPending();
            m_mainStack->setCurrentIndex(0);
        }
    }

    m_currentWorkspace = next;

    // Inform layout orchestrator of the new active workspace (0=Atlas, 1=FrameAnim, 2=Export, 3=AtlasMgt)
    if (m_layoutOrchestrator) {
        const int wsIdx = (next == m_frameAnimWorkspace) ? 1
                        : (next == m_exportWorkspace)    ? 2
                        : (next == m_atlasesManagementWorkspace) ? 3
                        : 0;
        m_layoutOrchestrator->setActiveWorkspace(wsIdx);
    }

    // Update toolbar action checked state
    if (m_atlasWorkspaceAction)
        m_atlasWorkspaceAction->setChecked(next == m_atlasWorkspace);
    if (m_frameAnimWorkspaceAction)
        m_frameAnimWorkspaceAction->setChecked(next == m_frameAnimWorkspace);
    if (m_exportationWorkspaceAction)
        m_exportationWorkspaceAction->setChecked(next == m_exportWorkspace);
    if (m_atlasesManagementWorkspaceAction)
        m_atlasesManagementWorkspaceAction->setChecked(next == m_atlasesManagementWorkspace);

    // Apply dock/stack layout for the incoming workspace
    applyWorkspaceLayout(next);

    // Enter the new workspace (restores view state, configures navigator, etc.)
    next->enter();

    // Post-enter work that requires MainWindow context
    auto* editorContent = m_atlasWorkspace ? m_atlasWorkspace->spriteEditorPanel() : nullptr;
    if (next == m_frameAnimWorkspace) {
        if (editorContent) editorContent->hide();
        updateNavigatorAtlasCombo();
        refreshSpriteTree();
        refreshTimelineList();
        refreshAnimationTest();
        scheduleLayoutRebuild(true);
        if (m_frameAnimWorkspace->firstLoad()) {
            m_frameAnimWorkspace->clearFirstLoad();
            resizeDocks({m_atlasDock, m_animationDock}, {270, 730}, Qt::Horizontal);
        }
    } else if (next == m_exportWorkspace) {
        if (m_exportCoordinator)
            m_exportCoordinator->setExportWorkspaceActive(true);
    } else if (next == m_atlasWorkspace) {
        if (editorContent) editorContent->show();
        refreshSpriteTree();
        updateUiState();
        if (prev == m_atlasesManagementWorkspace)
            scheduleLayoutRebuild(true);
    }
}
