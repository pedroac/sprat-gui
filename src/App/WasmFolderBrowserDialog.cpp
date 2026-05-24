#include "WasmFolderBrowserDialog.h"

#ifdef Q_OS_WASM
#include "WasmFileDialog.h"
#include "ImageDiscoveryService.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QPushButton>
#include <QMessageBox>
#include <QInputDialog>
#include <QKeySequence>
#include <QShortcut>
#include <QMimeData>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QPixmap>
#include <QSet>
#include <QStyle>
#include "MessageDialog.h"
#include <QDrag> // Added for QDrag
#include <QDragEnterEvent> // Added for QDragEnterEvent
#include <QDragMoveEvent> // Added for QDragMoveEvent
#include <QDropEvent> // Added for QDropEvent

namespace {
constexpr int kThumbnailSize = 40;
const char kFolderBrowserMimeType[] = "application/x-sprat-folder-browser-paths";

QString humanReadableSize(qint64 bytes) {
    if (bytes < 1024) {
        return QString::number(bytes) + " B";
    }
    if (bytes < 1024 * 1024) {
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    }
    return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
}

bool isImagePath(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    for (const QString& filter : ImageDiscoveryService::supportedImageFilters()) {
        if (filter.mid(2).compare(suffix, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

class FolderTreeWidget : public QTreeWidget {
public:
    using DropHandler = std::function<void(const QStringList&, const QString&)>;

    explicit FolderTreeWidget(QWidget* parent = nullptr)
        : QTreeWidget(parent) {}

    DropHandler handleFileDrop;

protected:
    void startDrag(Qt::DropActions supportedActions) override {
        Q_UNUSED(supportedActions);

        QStringList filePaths;
        const auto items = selectedItems();
        for (QTreeWidgetItem* item : items) {
            if (item->data(0, Qt::UserRole + 1).toBool()) {
                continue;
            }
            const QString path = item->data(0, Qt::UserRole).toString();
            if (!path.isEmpty() && !filePaths.contains(path)) {
                filePaths.append(path);
            }
        }
        if (filePaths.isEmpty()) {
            return;
        }

        auto* mimeData = new QMimeData;
        mimeData->setData(kFolderBrowserMimeType, filePaths.join('\n').toUtf8());

        QDrag drag(this);
        drag.setMimeData(mimeData);
        drag.exec(Qt::MoveAction);
    }

    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->source() == this && event->mimeData()->hasFormat(kFolderBrowserMimeType)) {
            event->acceptProposedAction();
            return;
        }
        QTreeWidget::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        if (event->source() == this && event->mimeData()->hasFormat(kFolderBrowserMimeType)) {
            event->acceptProposedAction();
            return;
        }
        QTreeWidget::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent* event) override {
        if (event->source() != this || !event->mimeData()->hasFormat(kFolderBrowserMimeType)) {
            QTreeWidget::dropEvent(event);
            return;
        }

        const QStringList paths = QString::fromUtf8(event->mimeData()->data(kFolderBrowserMimeType))
                                      .split('\n', Qt::SkipEmptyParts);
        if (paths.isEmpty()) {
            event->ignore();
            return;
        }

        QString targetDirectory;
        if (QTreeWidgetItem* item = itemAt(event->position().toPoint())) {
            const QString itemPath = item->data(0, Qt::UserRole).toString();
            if (item->data(0, Qt::UserRole + 1).toBool()) {
                targetDirectory = itemPath;
            } else {
                targetDirectory = QFileInfo(itemPath).absolutePath();
            }
        }

        if (targetDirectory.isEmpty()) {
            if (auto* topItem = topLevelItem(0)) {
                targetDirectory = topItem->data(0, Qt::UserRole).toString();
            }
        }

        if (!targetDirectory.isEmpty() && handleFileDrop) {
            handleFileDrop(paths, targetDirectory);
            event->acceptProposedAction();
            return;
        }

        event->ignore();
    }
};
}

WasmFolderBrowserDialog::WasmFolderBrowserDialog(const QString& folderPath, QWidget* parent)
    : QDialog(parent)
    , m_folderPath(folderPath)
{
    setWindowTitle(tr("Browse Sprites Folder"));
    connect(this, &QObject::destroyed, []() { wasmClearFilePickedHandler(); });
    setupUi();
    populateTree();
}

void WasmFolderBrowserDialog::setupUi()
{
    auto* layout = new QVBoxLayout(this);

    m_pathLabel = new QLabel(tr("Folder: %1").arg(m_folderPath), this);
    m_pathLabel->setWordWrap(true);
    layout->addWidget(m_pathLabel);

    auto* tree = new FolderTreeWidget(this);
    tree->handleFileDrop = [this](const QStringList& paths, const QString& targetDirectory) {
        moveDroppedFiles(paths, targetDirectory);
    };
    m_tree = tree;
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Name"), tr("Size")});
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setAlternatingRowColors(true);
    m_tree->setIconSize(QSize(kThumbnailSize, kThumbnailSize));
    m_tree->setUniformRowHeights(false);
    m_tree->setRootIsDecorated(true);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->viewport()->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setDragDropMode(QAbstractItemView::DragDrop);
    layout->addWidget(m_tree);

    auto* btnRow = new QHBoxLayout;
    m_importBtn = new QPushButton(tr("Add Files"), this);
    m_newFolderBtn = new QPushButton(tr("New Folder"), this);
    m_downloadBtn = new QPushButton(tr("Download Selected"), this);
    m_deleteBtn   = new QPushButton(tr("Delete Selected"), this);
    auto* closeBtn = new QPushButton(tr("Close"), this);

    btnRow->addWidget(m_importBtn);
    btnRow->addWidget(m_newFolderBtn);
    btnRow->addWidget(m_downloadBtn);
    btnRow->addWidget(m_deleteBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    resize(640, 480);

    auto* deleteShortcut = new QShortcut(QKeySequence::Delete, m_tree);
    connect(deleteShortcut, &QShortcut::activated, this, &WasmFolderBrowserDialog::onDeleteSelected);
    connect(m_importBtn,    &QPushButton::clicked, this, &WasmFolderBrowserDialog::onImportFiles);
    connect(m_newFolderBtn, &QPushButton::clicked, this, &WasmFolderBrowserDialog::onCreateFolder);
    connect(m_downloadBtn,  &QPushButton::clicked, this, &WasmFolderBrowserDialog::onDownloadSelected);
    connect(m_deleteBtn,    &QPushButton::clicked, this, &WasmFolderBrowserDialog::onDeleteSelected);
    connect(closeBtn,       &QPushButton::clicked, this, &QDialog::accept);
}

QTreeWidgetItem* WasmFolderBrowserDialog::ensureDirectoryItem(const QString& relativeDirPath) {
    QTreeWidgetItem* root = nullptr;
    if (m_tree->topLevelItemCount() == 0) {
        static const QIcon folderIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
        root = new QTreeWidgetItem(m_tree);
        root->setText(0, QFileInfo(m_folderPath).fileName().isEmpty()
                             ? m_folderPath
                             : QFileInfo(m_folderPath).fileName());
        root->setIcon(0, folderIcon);
        root->setData(0, Qt::UserRole, m_folderPath);
        root->setData(0, Qt::UserRole + 1, true);
        root->setExpanded(true);
    } else {
        root = m_tree->topLevelItem(0);
    }

    if (relativeDirPath.isEmpty() || relativeDirPath == ".") {
        return root;
    }

    static const QIcon folderIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    QDir baseDir(m_folderPath);
    QString normalizedPath = QDir::cleanPath(relativeDirPath);
    QStringList segments = normalizedPath.split('/', Qt::SkipEmptyParts);

    QTreeWidgetItem* parent = root;
    QString builtPath;
    for (const QString& segment : segments) {
        builtPath += (builtPath.isEmpty() ? QString() : QStringLiteral("/")) + segment;

        QTreeWidgetItem* match = nullptr;
        for (int i = 0; i < parent->childCount(); ++i) {
            QTreeWidgetItem* child = parent->child(i);
            if (child->data(0, Qt::UserRole + 1).toBool()
                && child->data(0, Qt::UserRole).toString() == baseDir.filePath(builtPath)) {
                match = child;
                break;
            }
        }

        if (!match) {
            match = new QTreeWidgetItem(parent);
            match->setText(0, segment);
            match->setIcon(0, folderIcon);
            match->setData(0, Qt::UserRole, baseDir.filePath(builtPath));
            match->setData(0, Qt::UserRole + 1, true);
        }

        parent = match;
    }

    return parent;
}

QString WasmFolderBrowserDialog::selectedDirectoryPath() const {
    const auto selected = m_tree->selectedItems();
    if (selected.isEmpty()) {
        return m_folderPath;
    }

    for (QTreeWidgetItem* item : selected) {
        const QString itemPath = item->data(0, Qt::UserRole).toString();
        if (itemPath.isEmpty()) {
            continue;
        }
        if (item->data(0, Qt::UserRole + 1).toBool()) {
            return itemPath;
        }
        return QFileInfo(itemPath).absolutePath();
    }

    return m_folderPath;
}

QString WasmFolderBrowserDialog::resolveDestinationPath(const QString& sourcePath,
                                                        const QString& targetDirectory) const {
    QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        return QString();
    }

    QDir targetDir(targetDirectory);
    QString destinationPath = targetDir.filePath(sourceInfo.fileName());
    QFileInfo destinationInfo(destinationPath);
    if (!destinationInfo.exists()) {
        return destinationPath;
    }

    const QString baseName = destinationInfo.completeBaseName();
    const QString suffix = destinationInfo.suffix();
    for (int i = 1; i <= 99; ++i) {
        const QString candidateName = suffix.isEmpty()
            ? QStringLiteral("%1_%2").arg(baseName).arg(i)
            : QStringLiteral("%1_%2.%3").arg(baseName).arg(i).arg(suffix);
        const QString candidatePath = targetDir.filePath(candidateName);
        if (!QFileInfo::exists(candidatePath)) {
            return candidatePath;
        }
    }

    return QString();
}

bool WasmFolderBrowserDialog::copyImportedFile(const QString& sourcePath,
                                               const QString& targetDirectory,
                                               QString* finalPath) {
    QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        return false;
    }

    QDir targetDir(targetDirectory);
    if (!targetDir.exists() && !QDir().mkpath(targetDirectory)) {
        return false;
    }

    const QString destinationPath = resolveDestinationPath(sourcePath, targetDirectory);
    if (destinationPath.isEmpty()) {
        return false;
    }

    if (!QFile::copy(sourcePath, destinationPath)) {
        return false;
    }

    if (finalPath) {
        *finalPath = destinationPath;
    }
    return true;
}

void WasmFolderBrowserDialog::moveDroppedFiles(const QStringList& sourcePaths,
                                               const QString& targetDirectory)
{
    if (sourcePaths.isEmpty() || targetDirectory.isEmpty()) {
        return;
    }

    QStringList movedPaths;
    QStringList deletedPaths;

    for (const QString& sourcePath : sourcePaths) {
        QFileInfo sourceInfo(sourcePath);
        if (!sourceInfo.exists() || !sourceInfo.isFile()) {
            continue;
        }

        if (sourceInfo.absolutePath() == QDir(targetDirectory).absolutePath()) {
            continue;
        }

        QDir().mkpath(targetDirectory);
        const QString destinationPath = resolveDestinationPath(sourcePath, targetDirectory);
        if (destinationPath.isEmpty()) {
            continue;
        }

        if (QFile::rename(sourcePath, destinationPath)) {
            movedPaths.append(destinationPath);
            deletedPaths.append(sourcePath);
            continue;
        }

        if (QFile::copy(sourcePath, destinationPath) && QFile::remove(sourcePath)) {
            movedPaths.append(destinationPath);
            deletedPaths.append(sourcePath);
        }
    }

    if (movedPaths.isEmpty()) {
        return;
    }

    emit filesDeleted(deletedPaths);
    emit filesAdded(movedPaths);
    populateTree();
}

void WasmFolderBrowserDialog::populateTree()
{
    m_tree->clear();

    QDir baseDir(m_folderPath);
    ensureDirectoryItem(QString());

    QDirIterator dirIt(m_folderPath, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (dirIt.hasNext()) {
        const QString dirPath = dirIt.next();
        if (dirPath.contains("/.sprat-trash") || dirPath.endsWith("/.sprat-trash")) {
            continue;
        }
        ensureDirectoryItem(baseDir.relativeFilePath(dirPath));
    }

    static const QIcon fileIcon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
    QDirIterator fileIt(m_folderPath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (fileIt.hasNext()) {
        const QString absPath = fileIt.next();
        if (absPath.contains("/.sprat-trash/")) {
            continue;
        }

        const QString relPath = baseDir.relativeFilePath(absPath);
        const QString relativeDir = QFileInfo(relPath).path();
        QTreeWidgetItem* parent = ensureDirectoryItem(relativeDir);
        QTreeWidgetItem* fileItem = nullptr;
        if (parent == m_tree->invisibleRootItem()) {
            fileItem = new QTreeWidgetItem(m_tree);
        } else {
            fileItem = new QTreeWidgetItem(parent);
        }
        fileItem->setText(0, QFileInfo(absPath).fileName());
        fileItem->setText(1, humanReadableSize(QFileInfo(absPath).size()));
        fileItem->setData(0, Qt::UserRole, absPath);
        fileItem->setData(0, Qt::UserRole + 1, false);
        fileItem->setToolTip(0, relPath);

        if (isImagePath(absPath)) {
            QPixmap pixmap(absPath);
            if (!pixmap.isNull()) {
                fileItem->setIcon(0, QIcon(pixmap.scaled(kThumbnailSize, kThumbnailSize,
                                                         Qt::KeepAspectRatio,
                                                         Qt::SmoothTransformation)));
            } else {
                fileItem->setIcon(0, fileIcon);
            }
        } else {
            fileItem->setIcon(0, fileIcon);
        }
    }

    m_tree->sortItems(0, Qt::AscendingOrder);
    m_tree->expandAll();
}

void WasmFolderBrowserDialog::onDownloadSelected()
{
    const auto selected = m_tree->selectedItems();
    for (QTreeWidgetItem* item : selected) {
        const QString path = item->data(0, Qt::UserRole).toString();
        if (!path.isEmpty()) {
            wasmDownloadFile(path);
        }
    }
}

void WasmFolderBrowserDialog::onDeleteSelected()
{
    const auto selected = m_tree->selectedItems();
    QStringList paths;
    QSet<QString> uniquePaths;
    for (QTreeWidgetItem* item : selected) {
        const QString path = item->data(0, Qt::UserRole).toString();
        if (!path.isEmpty() && !uniquePaths.contains(path)) {
            uniquePaths.insert(path);
            paths.append(path);
        }
    }
    if (paths.isEmpty()) return;

    const int reply = MessageDialog::confirmWarning(
        this,
        tr("Delete Files"),
        tr("Delete %1 file(s)? This cannot be undone.").arg(paths.size()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    QStringList deleted;
    for (const QString& path : paths) {
        QFileInfo info(path);
        if (info.isDir()) {
            QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                deleted.append(it.next());
            }
            QDir(path).removeRecursively();
        } else if (QFile::remove(path)) {
            deleted.append(path);
        }
    }

    if (!deleted.isEmpty()) {
        emit filesDeleted(deleted);
    }

    populateTree();
}

void WasmFolderBrowserDialog::onImportFiles()
{
    wasmSetFilePickedHandler([this](const QString& path, int mode) {
        importPickedFiles(path, mode);
        wasmClearFilePickedHandler();
    });
    wasmOpenFileDialogMode(2);
}

void WasmFolderBrowserDialog::onCreateFolder()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        tr("Create Folder"),
        tr("Folder name:"),
        QLineEdit::Normal,
        QString(),
        &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }

    const QString parentPath = selectedDirectoryPath();
    const QString fullPath = QDir(parentPath).filePath(name);
    if (QFileInfo::exists(fullPath)) {
        MessageDialog::information(this, tr("Create Folder"), tr("A file or folder with that name already exists."));
        return;
    }
    if (!QDir().mkpath(fullPath)) {
        MessageDialog::warning(this, tr("Create Folder"), tr("Could not create folder at: %1").arg(fullPath));
        return;
    }

    populateTree();
}

void WasmFolderBrowserDialog::importPickedFiles(const QString& pickedPath, int mode)
{
    Q_UNUSED(mode);

    QFileInfo pickedInfo(pickedPath);
    if (!pickedInfo.exists()) {
        return;
    }

    const QString targetDirectory = selectedDirectoryPath();
    QStringList addedFiles;

    if (pickedInfo.isDir()) {
        QDirIterator it(pickedPath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString sourcePath = it.next();
            QString copiedPath;
            if (copyImportedFile(sourcePath, targetDirectory, &copiedPath)) {
                addedFiles.append(copiedPath);
            }
        }
    } else {
        QString copiedPath;
        if (copyImportedFile(pickedPath, targetDirectory, &copiedPath)) {
            addedFiles.append(copiedPath);
        }
    }

    if (addedFiles.isEmpty()) {
        MessageDialog::warning(this, tr("Add Files"), tr("No files were imported."));
        return;
    }

    emit filesAdded(addedFiles);
    populateTree();
}
#endif
