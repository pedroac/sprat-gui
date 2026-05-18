#include "WasmFolderBrowserDialog.h"

#ifdef Q_OS_WASM
#include "WasmFileDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QPushButton>
#include <QMessageBox>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QMap>

WasmFolderBrowserDialog::WasmFolderBrowserDialog(const QString& folderPath, QWidget* parent)
    : QDialog(parent)
    , m_folderPath(folderPath)
{
    setWindowTitle(tr("Browse Sprites Folder"));
    setupUi();
    populateTree();
}

void WasmFolderBrowserDialog::setupUi()
{
    auto* layout = new QVBoxLayout(this);

    m_pathLabel = new QLabel(tr("Folder: %1").arg(m_folderPath), this);
    m_pathLabel->setWordWrap(true);
    layout->addWidget(m_pathLabel);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Name"), tr("Size")});
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setAlternatingRowColors(true);
    layout->addWidget(m_tree);

    auto* btnRow = new QHBoxLayout;
    m_downloadBtn = new QPushButton(tr("Download Selected"), this);
    m_deleteBtn   = new QPushButton(tr("Delete Selected"), this);
    auto* closeBtn = new QPushButton(tr("Close"), this);

    btnRow->addWidget(m_downloadBtn);
    btnRow->addWidget(m_deleteBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    resize(640, 480);

    connect(m_downloadBtn, &QPushButton::clicked, this, &WasmFolderBrowserDialog::onDownloadSelected);
    connect(m_deleteBtn,   &QPushButton::clicked, this, &WasmFolderBrowserDialog::onDeleteSelected);
    connect(closeBtn,      &QPushButton::clicked, this, &QDialog::accept);
}

void WasmFolderBrowserDialog::populateTree()
{
    m_tree->clear();

    QMap<QString, QTreeWidgetItem*> dirItems;
    QDir baseDir(m_folderPath);

    QDirIterator it(m_folderPath,
                    QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString absPath = it.next();
        const QString relPath = baseDir.relativeFilePath(absPath);
        const QStringList segments = relPath.split('/');

        // Walk/create folder items for all but last segment (the filename)
        QTreeWidgetItem* parent = nullptr;
        QString builtPath;
        for (int i = 0; i < segments.size() - 1; ++i) {
            builtPath += (builtPath.isEmpty() ? QString() : QStringLiteral("/")) + segments[i];
            if (!dirItems.contains(builtPath)) {
                auto* dirItem = new QTreeWidgetItem(parent ? parent
                                                           : m_tree->invisibleRootItem());
                dirItem->setText(0, segments[i]);
                dirItem->setFlags(dirItem->flags() & ~Qt::ItemIsSelectable);
                dirItems.insert(builtPath, dirItem);
            }
            parent = dirItems.value(builtPath);
        }

        // File item
        QTreeWidgetItem* fileItem = new QTreeWidgetItem(parent ? parent
                                                                : m_tree->invisibleRootItem());
        fileItem->setText(0, segments.last());
        fileItem->setData(0, Qt::UserRole, absPath);

        // Human-readable size
        const qint64 bytes = QFileInfo(absPath).size();
        QString sizeStr;
        if (bytes < 1024) {
            sizeStr = QString::number(bytes) + " B";
        } else if (bytes < 1024 * 1024) {
            sizeStr = QString::number(bytes / 1024.0, 'f', 1) + " KB";
        } else {
            sizeStr = QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
        }
        fileItem->setText(1, sizeStr);
    }

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
    for (QTreeWidgetItem* item : selected) {
        const QString path = item->data(0, Qt::UserRole).toString();
        if (!path.isEmpty()) {
            paths.append(path);
        }
    }
    if (paths.isEmpty()) return;

    const auto reply = QMessageBox::warning(
        this,
        tr("Delete Files"),
        tr("Delete %1 file(s)? This cannot be undone.").arg(paths.size()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    QStringList deleted;
    for (const QString& path : paths) {
        if (QFile::remove(path)) {
            deleted.append(path);
        }
    }

    if (!deleted.isEmpty()) {
        emit filesDeleted(deleted);
    }

    populateTree();
}
#endif
