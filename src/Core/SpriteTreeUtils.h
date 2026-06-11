#pragma once
#include <functional>
#include <QIcon>
#include <QPair>
#include <QString>
#include <QVector>

class QTreeWidget;
class QTreeWidgetItem;

/**
 * @brief Shared utilities for building sprite navigation trees.
 */
namespace SpriteTreeUtils {

/**
 * Populates sprite-tree items under @p root for the given @p entries.
 *
 * Groups items into folder nodes based on '/' separators in the localName component.
 * Within each folder, animation sequences are detected via TimelineBuilder and
 * rendered as virtual animation group nodes with @p animGroupIcon.
 *
 * Folder nodes are expanded by default.
 *
 * @param tree          QTreeWidget for top-level item creation when root is nullptr.
 * @param root          Parent node, or nullptr to insert at the top level.
 * @param entries       (absolutePath, localName) pairs; localName drives the tree structure.
 * @param folderIcon    Icon for filesystem folder nodes.
 * @param animGroupIcon Icon for virtual animation-sequence group nodes.
 * @param checkable     When true, folder and group nodes get Qt::ItemIsUserCheckable.
 * @param makeLeaf      Callback invoked for each leaf item.
 *                      Receives (parentItem, absolutePath, leafDisplayName).
 *                      Must create the item as a child of parentItem.
 */
void buildSubTree(
    QTreeWidget* tree,
    QTreeWidgetItem* root,
    const QVector<QPair<QString, QString>>& entries,
    const QIcon& folderIcon,
    const QIcon& animGroupIcon,
    bool checkable,
    const std::function<void(QTreeWidgetItem*, const QString&, const QString&)>& makeLeaf
);

} // namespace SpriteTreeUtils
