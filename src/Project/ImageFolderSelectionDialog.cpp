#include "ImageFolderSelectionDialog.h"

#include "ImageDiscoveryService.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QInputDialog>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

bool ImageFolderSelectionDialog::pickSingleFolderWithImages(QWidget* parent, const QString& root, QString& selection, bool* canceled) {
    if (canceled) {
        *canceled = false;
    }

    const QStringList directories = ImageDiscoveryService::imageDirectoriesOneLevel(root);
    if (directories.isEmpty()) {
        return false;
    }
    if (directories.size() == 1) {
        selection = directories.first();
        return true;
    }

    QDir base(root);
    QStringList labels;
    labels.reserve(directories.size());
    for (const QString& dirPath : directories) {
        labels.append(base.relativeFilePath(dirPath));
    }

    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        parent,
        tr("Select frame folder"),
        tr("Folders with images:"),
        labels,
        0,
        false,
        &ok);
    if (!ok) {
        if (canceled) {
            *canceled = true;
        }
        return false;
    }

    const int index = labels.indexOf(chosen);
    if (index < 0 || index >= directories.size()) {
        return false;
    }
    selection = directories[index];
    return true;
}

bool ImageFolderSelectionDialog::pickMultipleFoldersWithImages(QWidget* parent, const QString& root, QStringList& selections, bool* canceled) {
    if (canceled) {
        *canceled = false;
    }
    selections.clear();

    const QStringList imageDirectories = ImageDiscoveryService::imageDirectoriesRecursive(root);
    if (imageDirectories.isEmpty()) {
        return false;
    }
    if (imageDirectories.size() == 1) {
        selections = imageDirectories;
        return true;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(tr("Select frame folders"));
    dialog.setModal(true);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Choose one or more folders with images. Selecting a parent includes its subfolders."), &dialog));

    QTreeWidget* tree = new QTreeWidget(&dialog);
    tree->setHeaderLabel(tr("Folders"));
    tree->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(tree, 1);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QHash<QString, QTreeWidgetItem*> itemsByRelativePath;
    const QDir base(root);
    const QString rootLabel = QFileInfo(base.absolutePath()).fileName().isEmpty()
                                  ? base.absolutePath()
                                  : QFileInfo(base.absolutePath()).fileName();
    QTreeWidgetItem* rootItem = new QTreeWidgetItem(tree);
    rootItem->setText(0, rootLabel);
    rootItem->setData(0, Qt::UserRole, QString());
    rootItem->setFlags(rootItem->flags() | Qt::ItemIsUserCheckable);
    rootItem->setCheckState(0, Qt::Unchecked);
    itemsByRelativePath.insert(QString(), rootItem);

    auto ensureItem = [&](const QString& relativePath) {
        if (itemsByRelativePath.contains(relativePath)) {
            return itemsByRelativePath.value(relativePath);
        }

        QString normalizedPath = relativePath;
        normalizedPath = normalizedPath.replace('\\', '/').trimmed();
        const QStringList parts = normalizedPath.split('/', Qt::SkipEmptyParts);
        QString currentPath;
        QTreeWidgetItem* parentItem = rootItem;

        for (const QString& part : parts) {
            currentPath = currentPath.isEmpty() ? part : currentPath + "/" + part;
            if (!itemsByRelativePath.contains(currentPath)) {
                QTreeWidgetItem* item = new QTreeWidgetItem(parentItem);
                item->setText(0, part);
                item->setData(0, Qt::UserRole, currentPath);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(0, Qt::Unchecked);
                itemsByRelativePath.insert(currentPath, item);
            }
            parentItem = itemsByRelativePath.value(currentPath);
        }
        return parentItem;
    };

    for (const QString& absolutePath : imageDirectories) {
        const QString relativePath = base.relativeFilePath(absolutePath).replace('\\', '/');
        QTreeWidgetItem* item = ensureItem(relativePath);
        item->setData(0, Qt::UserRole + 1, true);
    }

    bool updatingChecks = false;
    QObject::connect(tree, &QTreeWidget::itemChanged, tree, [&](QTreeWidgetItem* changedItem, int column) {
        if (column != 0 || updatingChecks || !changedItem) {
            return;
        }

        updatingChecks = true;
        const Qt::CheckState state = changedItem->checkState(0);

        std::function<void(QTreeWidgetItem*)> applyToChildren = [&](QTreeWidgetItem* item) {
            for (int i = 0; i < item->childCount(); ++i) {
                QTreeWidgetItem* child = item->child(i);
                child->setCheckState(0, state);
                applyToChildren(child);
            }
        };
        applyToChildren(changedItem);

        QTreeWidgetItem* parentItem = changedItem->parent();
        while (parentItem) {
            int checkedCount = 0;
            int uncheckedCount = 0;
            int partialCount = 0;
            for (int i = 0; i < parentItem->childCount(); ++i) {
                const Qt::CheckState childState = parentItem->child(i)->checkState(0);
                if (childState == Qt::Checked) {
                    ++checkedCount;
                } else if (childState == Qt::Unchecked) {
                    ++uncheckedCount;
                } else {
                    ++partialCount;
                }
            }

            if (partialCount > 0 || (checkedCount > 0 && uncheckedCount > 0)) {
                parentItem->setCheckState(0, Qt::PartiallyChecked);
            } else if (checkedCount == parentItem->childCount()) {
                parentItem->setCheckState(0, Qt::Checked);
            } else {
                parentItem->setCheckState(0, Qt::Unchecked);
            }
            parentItem = parentItem->parent();
        }
        updatingChecks = false;
    });

    tree->expandToDepth(1);

    if (dialog.exec() != QDialog::Accepted) {
        if (canceled) {
            *canceled = true;
        }
        return false;
    }

    std::function<void(QTreeWidgetItem*, bool)> collectChecked = [&](QTreeWidgetItem* item, bool ancestorSelected) {
        const bool selected = item->checkState(0) == Qt::Checked;
        const QString relativePath = item->data(0, Qt::UserRole).toString();
        if (selected && !ancestorSelected) {
            const QString absolutePath = relativePath.isEmpty() ? base.absolutePath() : base.absoluteFilePath(relativePath);
            selections.append(QDir(absolutePath).absolutePath());
            ancestorSelected = true;
        }
        for (int i = 0; i < item->childCount(); ++i) {
            collectChecked(item->child(i), ancestorSelected);
        }
    };
    collectChecked(rootItem, false);

    selections.removeDuplicates();
    std::sort(selections.begin(), selections.end());
    return !selections.isEmpty();
}
