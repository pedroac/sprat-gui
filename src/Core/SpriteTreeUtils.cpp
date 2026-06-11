#include "SpriteTreeUtils.h"
#include "models.h"
#include "TimelineBuilder.h"

#include <QMap>
#include <QSet>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace SpriteTreeUtils {

void buildSubTree(
    QTreeWidget* tree,
    QTreeWidgetItem* root,
    const QVector<QPair<QString, QString>>& entries,
    const QIcon& folderIcon,
    const QIcon& animGroupIcon,
    bool checkable,
    const std::function<void(QTreeWidgetItem*, const QString&, const QString&)>& makeLeaf)
{
    using Entry = QPair<QString, QString>;

    auto makeGroupNode = [&](QTreeWidgetItem* parent, const QString& text) -> QTreeWidgetItem* {
        auto* node = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
        node->setText(0, text);
        node->setIcon(0, folderIcon);
        if (checkable) {
            node->setFlags(node->flags() | Qt::ItemIsUserCheckable);
            node->setCheckState(0, Qt::Unchecked);
        }
        return node;
    };

    auto findOrCreateFolderPath = [&](QTreeWidgetItem* parent, const QStringList& parts) -> QTreeWidgetItem* {
        QTreeWidgetItem* current = parent;
        for (const QString& part : parts) {
            QTreeWidgetItem* found = nullptr;
            const int count = current ? current->childCount() : tree->topLevelItemCount();
            for (int i = 0; i < count; ++i) {
                QTreeWidgetItem* child = current ? current->child(i) : tree->topLevelItem(i);
                if (child->text(0) == part && !child->data(0, Qt::UserRole).isValid()) {
                    found = child;
                    break;
                }
            }
            if (!found) found = makeGroupNode(current, part);
            current = found;
        }
        return current;
    };

    // Group entries by folder (everything before the last '/').
    QMap<QString, QVector<Entry>> folderGroups;
    for (const auto& entry : entries) {
        const int lastSlash = entry.second.lastIndexOf('/');
        const QString folder = (lastSlash >= 0) ? entry.second.left(lastSlash) : QString();
        folderGroups[folder].append(entry);
    }

    for (auto it = folderGroups.constBegin(); it != folderGroups.constEnd(); ++it) {
        const QString& folderPath   = it.key();
        const QVector<Entry>& items = it.value();

        QTreeWidgetItem* folderNode = root;
        if (!folderPath.isEmpty())
            folderNode = findOrCreateFolderPath(root, folderPath.split('/'));

        // Build temporary sprites for TimelineBuilder animation grouping.
        QVector<SpritePtr> tempSprites;
        tempSprites.reserve(items.size());
        for (const auto& entry : items) {
            const int ls = entry.second.lastIndexOf('/');
            auto tmp     = std::make_shared<Sprite>();
            tmp->path    = entry.first;
            tmp->name    = (ls >= 0) ? entry.second.mid(ls + 1) : entry.second;
            tempSprites.append(tmp);
        }

        const QVector<TimelineSeed> seeds = TimelineBuilder::buildFromSprites(tempSprites);

        // Skip creating a group node when only one multi-frame animation is in this folder.
        int groupCount = 0;
        for (const auto& seed : seeds)
            if (seed.frames.size() > 1) ++groupCount;
        const bool skipSingleGroup = (groupCount == 1);

        // Map path → leaf entry (path, leafName) for TimelineSeed lookup.
        QMap<QString, Entry> pathToEntry;
        for (const auto& entry : items) {
            const int ls = entry.second.lastIndexOf('/');
            pathToEntry[entry.first] = {entry.first, (ls >= 0) ? entry.second.mid(ls + 1) : entry.second};
        }

        QSet<QString> groupedPaths;
        QMap<QString, QVector<Entry>> groupMembers; // seed name → frame-ordered entries
        if (!skipSingleGroup) {
            for (const auto& seed : seeds) {
                if (seed.frames.size() <= 1) continue;
                QVector<Entry> members;
                for (const QString& p : seed.frames) {
                    if (pathToEntry.contains(p)) members.append(pathToEntry[p]);
                    groupedPaths.insert(p);
                }
                if (!members.isEmpty()) groupMembers[seed.name] = members;
            }
        }

        // Unified sorted render map: group name → members (>1 entry = anim group node).
        QMap<QString, QVector<Entry>> renderMap = groupMembers;
        for (const auto& entry : items) {
            if (groupedPaths.contains(entry.first)) continue;
            const int ls           = entry.second.lastIndexOf('/');
            const QString leafName = (ls >= 0) ? entry.second.mid(ls + 1) : entry.second;
            renderMap[leafName].append({entry.first, leafName});
        }

        for (auto rit = renderMap.constBegin(); rit != renderMap.constEnd(); ++rit) {
            const QVector<Entry>& members = rit.value();
            if (members.size() > 1) {
                auto* gNode = makeGroupNode(folderNode, rit.key());
                gNode->setIcon(0, animGroupIcon);
                for (const auto& m : members)
                    makeLeaf(gNode, m.first, m.second);
            } else {
                makeLeaf(folderNode, members.first().first, members.first().second);
            }
        }

        if (folderNode != root) folderNode->setExpanded(true);
    }
}

} // namespace SpriteTreeUtils
