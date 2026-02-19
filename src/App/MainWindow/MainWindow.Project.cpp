#include "MainWindow.h"

#include "AutosaveProjectStore.h"
#include "ProjectFileLoader.h"
#include "ProjectPayloadCodec.h"
#include "ProjectSaveService.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QApplication>
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
    const QString folder = root["layout"].toObject()["folder"].toString();
    if (folder.isEmpty()) {
        m_statusLabel->setText(tr("Autosave missing folder."));
        return;
    }

    m_statusLabel->setText(tr("Loading autosaved project"));
    cacheLayoutOutputFromPayload(root);
    m_pendingProjectPayload = root;
    m_currentFolder = folder;
    m_folderLabel->setText(tr("Folder: ") + folder);
    m_layoutSourcePath = QDir(folder).absolutePath();
    m_layoutSourceIsList = false;
    if (!m_frameListPath.isEmpty()) {
        QFile::remove(m_frameListPath);
        m_frameListPath.clear();
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

    SaveDialog dlg(defaultPath, this);
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

    SpratProfile selectedProfile;
    const bool hasSelectedProfile = selectedProfileDefinition(selectedProfile);
    const int profilePadding = hasSelectedProfile ? selectedProfile.padding : 0;
    const bool profileTrimTransparent = hasSelectedProfile ? selectedProfile.trimTransparent : false;

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
        m_profileCombo->currentText(),
        profilePadding,
        profileTrimTransparent,
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
    input.timelines = m_timelines;
    input.selectedTimelineIndex = m_selectedTimelineIndex;
    input.selectedSprite = m_selectedSprite;
    input.selectedPointName = m_selectedPointName;
    input.layoutModel = m_layoutModel;
    input.layoutOutput = m_cachedLayoutOutput;
    input.layoutScale = m_cachedLayoutScale;
    input.profile = m_profileCombo->currentText();
    SpratProfile selectedProfile;
    const bool hasSelectedProfile = selectedProfileDefinition(selectedProfile);
    input.padding = hasSelectedProfile ? selectedProfile.padding : 0;
    input.trimTransparent = hasSelectedProfile ? selectedProfile.trimTransparent : false;
    input.layoutZoom = m_layoutZoomSpin->value();
    input.previewZoom = m_previewZoomSpin->value();
    input.animationZoom = m_animZoomSpin->value();
    input.animationFps = m_fpsSpin->value();
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
    QJsonObject layoutInfo = root["layout"].toObject();
    QString folder = layoutInfo["folder"].toString();
    if (!folder.isEmpty()) {
        m_currentFolder = folder;
        m_folderLabel->setText(tr("Folder: ") + folder);
        m_layoutSourcePath = QDir(folder).absolutePath();
        m_layoutSourceIsList = false;
        if (!m_frameListPath.isEmpty()) {
            QFile::remove(m_frameListPath);
            m_frameListPath.clear();
        }
        onRunLayout();
    }
}

bool MainWindow::pickImageSubdirectory(const QString& root, QString& selection, bool* canceled) const {
    if (canceled) {
        *canceled = false;
    }
    QVector<QString> dirs;
    if (hasImageFiles(root)) {
        dirs.append(root);
    }
    QDir base(root);
    for (const QString& entry : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString candidate = base.filePath(entry);
        if (hasImageFiles(candidate)) {
            dirs.append(candidate);
        }
    }
    if (dirs.isEmpty()) {
        return false;
    }
    if (dirs.size() == 1) {
        selection = dirs.first();
        return true;
    }
    QStringList labels;
    labels.reserve(dirs.size());
    for (const QString& dirPath : dirs) {
        labels.append(base.relativeFilePath(dirPath));
    }
    bool ok = false;
    QString chosen = QInputDialog::getItem(const_cast<MainWindow*>(this), tr("Select frame folder"), tr("Folders with images:"), labels, 0, false, &ok);
    if (!ok) {
        if (canceled) {
            *canceled = true;
        }
        return false;
    }
    int index = labels.indexOf(chosen);
    if (index >= 0 && index < dirs.size()) {
        selection = dirs[index];
        return true;
    }
    return false;
}

bool MainWindow::hasImageFiles(const QString& path) const {
    QDir dir(path);
    const QStringList filters = {"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.gif", "*.webp", "*.tga", "*.dds"};
    return !dir.entryList(filters, QDir::Files).isEmpty();
}

void MainWindow::clearZipTempDir() {
    if (m_zipTempDir) {
        delete m_zipTempDir;
        m_zipTempDir = nullptr;
    }
}

bool MainWindow::loadImagesFromZip(const QString& zipPath, bool confirmReplace) {
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

    QString selection;
    bool selectionCanceled = false;
    if (pickImageSubdirectory(m_zipTempDir->path(), selection, &selectionCanceled)) {
        loadFolder(selection, confirmReplace);
        return true;
    }
    if (selectionCanceled) {
        clearZipTempDir();
        m_statusLabel->setText("Load canceled");
        return false;
    }
    if (hasImageFiles(m_zipTempDir->path())) {
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
    m_fpsSpin->setValue(applied.animationFps);
    m_timelines = applied.timelines;
    refreshTimelineList();
}

void MainWindow::onAutosaveTimer() {
    autosaveProject();
}
