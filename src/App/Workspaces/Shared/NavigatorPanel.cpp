#include "NavigatorPanel.h"
#include "AppIcons.h"
#include "NavigatorTreeWidget.h"
#include "ProjectSession.h"
#include "SpriteTreeUtils.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QPainter>
#include <QRegularExpression>
#include <QDateTime>
#include <QSet>
#include <QSignalBlocker>
#include <functional>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QtConcurrent/QtConcurrentRun>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>

namespace {
/// Sprite rows' thumbnails are this many pixels a side.
constexpr int THUMBNAIL_SIZE = 20;

struct CachedThumbnail {
    QDateTime modified;
    QImage image;  ///< Null for a file that isn't an image (it gets no icon)
};

struct LoadedThumbnail {
    QString path;
    QDateTime modified;
    QImage image;
};

/// Thumbnails by sprite path, shared by every sprite tree (each picker and workspace), so a
/// sprite is decoded once while its file is unchanged. Images, not pixmaps: they outlive
/// QApplication safely.
QHash<QString, CachedThumbnail>& thumbnailCache() {
    static QHash<QString, CachedThumbnail> cache;
    return cache;
}

/// Decodes thumbnails off the UI thread for every sprite tree. Trees rebuild several times
/// during an import: a path already being decoded isn't decoded again, and every tree gets
/// each batch as it is ready (ThumbnailLoader::ready).
class ThumbnailLoader : public QObject {
public:
    static ThumbnailLoader* instance() {
        static ThumbnailLoader* loader = new ThumbnailLoader(qApp);
        return loader;
    }

    void load(const QStringList& paths) {
        QStringList wanted;
        for (const QString& path : paths) {
            if (m_loading.contains(path)) continue;
            m_loading.insert(path);
            wanted.append(path);
        }
        if (wanted.isEmpty()) return;
#ifdef Q_OS_WASM
        // QtConcurrent may not run without pthreads (see LayoutRunner): decode here, once the
        // tree that asked is built.
        QMetaObject::invokeMethod(this, [this, wanted]() { deliver(decode(wanted)); }, Qt::QueuedConnection);
#else
        auto* watcher = new QFutureWatcher<QVector<LoadedThumbnail>>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
            deliver(watcher->result());
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run([wanted]() { return decode(wanted); }));
#endif
    }

    /// Calls @p callback with each batch of thumbnails, until @p owner is destroyed.
    void listen(QObject* owner, std::function<void(const QHash<QString, QImage>&)> callback) {
        m_listeners.append({owner, std::move(callback)});
        connect(owner, &QObject::destroyed, this, [this, owner]() {
            m_listeners.removeIf([owner](const auto& listener) { return listener.first == owner; });
        });
    }

private:
    using QObject::QObject;

    static QVector<LoadedThumbnail> decode(const QStringList& paths) {
        QVector<LoadedThumbnail> loaded;
        loaded.reserve(paths.size());
        for (const QString& path : paths) {
            QImage image = QImageReader(path).read();
            if (!image.isNull())
                image = image.scaled(THUMBNAIL_SIZE, THUMBNAIL_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            loaded.append({path, QFileInfo(path).lastModified(), image});
        }
        return loaded;
    }

    void deliver(const QVector<LoadedThumbnail>& loaded) {
        QHash<QString, QImage> images;
        for (const LoadedThumbnail& thumbnail : loaded) {
            m_loading.remove(thumbnail.path);
            thumbnailCache().insert(thumbnail.path, {thumbnail.modified, thumbnail.image});
            images.insert(thumbnail.path, thumbnail.image);
        }
        for (const auto& [owner, callback] : m_listeners) callback(images);
    }

    QSet<QString> m_loading;
    QVector<QPair<QObject*, std::function<void(const QHash<QString, QImage>&)>>> m_listeners;
};

/// The cached thumbnail of @p path, or nullptr when it isn't cached or its file changed since.
const QImage* cachedThumbnail(const QString& path) {
    const auto it = thumbnailCache().constFind(path);
    if (it == thumbnailCache().constEnd() || it->modified != QFileInfo(path).lastModified()) return nullptr;
    return &it->image;
}
}  // namespace

// ---------------------------------------------------------------------------
// Helper: recursively propagate tristate check state bottom-up for folder nodes.
// ---------------------------------------------------------------------------
static void updateFolderCheckState(QTreeWidgetItem* item)
{
    for (int i = 0; i < item->childCount(); ++i)
        updateFolderCheckState(item->child(i));
    if (item->data(0, Qt::UserRole).isValid())        return; // sprite leaf
    if (item->data(0, Qt::UserRole + 2).toInt() > 0)  return; // hidden/excluded special item
    int checked = 0, unchecked = 0;
    for (int i = 0; i < item->childCount(); ++i) {
        const Qt::CheckState cs = item->child(i)->checkState(0);
        if (cs == Qt::Checked)        ++checked;
        else if (cs == Qt::Unchecked) ++unchecked;
        else { ++checked; ++unchecked; }
    }
    if (checked == 0)        item->setCheckState(0, Qt::Unchecked);
    else if (unchecked == 0) item->setCheckState(0, Qt::Checked);
    else                     item->setCheckState(0, Qt::PartiallyChecked);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Convert a simple glob pattern (supports * and ?) to an anchored regex string.
static QString globToRegex(const QString& glob)
{
    QString rx;
    rx.reserve(glob.size() * 2 + 2);
    for (const QChar& c : glob) {
        switch (c.unicode()) {
        case u'*': rx += QLatin1String(".*");  break;
        case u'?': rx += QLatin1Char('.');      break;
        // Escape every other regex metacharacter
        case u'\\': case u'^': case u'$': case u'.': case u'|':
        case u'+':  case u'(': case u')': case u'[': case u']':
        case u'{':  case u'}':
            rx += QLatin1Char('\\');
            rx += c;
            break;
        default: rx += c; break;
        }
    }
    return QLatin1Char('^') + rx + QLatin1Char('$');
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
NavigatorPanel::NavigatorPanel(QWidget* parent)
    : QWidget(parent)
{
    ThumbnailLoader::instance()->listen(this, [this](const QHash<QString, QImage>& thumbnails) {
        applyLoadedThumbnails(thumbnails);
    });
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    // Atlas selection row — hidden by default, shown in Frame Animation workspace
    m_atlasRow = new QWidget(this);
    m_atlasRow->setVisible(false);
    auto* atlasRowLayout = new QHBoxLayout(m_atlasRow);
    atlasRowLayout->setContentsMargins(0, 0, 0, 0);
    atlasRowLayout->addWidget(new QLabel(tr("Atlas:"), m_atlasRow));
    m_atlasCombo = new QComboBox(m_atlasRow);
    m_atlasCombo->setToolTip(tr("Select atlas — filters sprites and timelines"));
    atlasRowLayout->addWidget(m_atlasCombo, 1);
    layout->addWidget(m_atlasRow);

    connect(m_atlasCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NavigatorPanel::onAtlasComboChanged);

    // Filter row: mode combo + search box + result count + show-hidden checkbox
    auto* filterRow = new QHBoxLayout();
    filterRow->setContentsMargins(0, 0, 0, 0);

    m_filterModeCombo = new QComboBox(this);
    m_filterModeCombo->addItem(tr("Text"),  static_cast<int>(FilterMode::Text));
    m_filterModeCombo->addItem(tr("Glob"),  static_cast<int>(FilterMode::Glob));
    m_filterModeCombo->addItem(tr("Regex"), static_cast<int>(FilterMode::Regex));
    m_filterModeCombo->setToolTip(tr(
        "Filter mode:\n"
        "  Text  — case-insensitive substring (default)\n"
        "  Glob  — wildcards: * matches any run of characters, ? matches one\n"
        "  Regex — full Qt regular expression"));
    m_filterModeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    filterRow->addWidget(m_filterModeCombo);
    connect(m_filterModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { if (m_filterEdit) applyFilter(m_filterEdit->text()); });

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Search sprites..."));
    filterRow->addWidget(m_filterEdit, 1);

    m_filterResult = new QLabel(this);
    m_filterResult->setStyleSheet("color: #888; font-size: 11px;");
    m_filterResult->setVisible(false);
    filterRow->addWidget(m_filterResult);

    m_showHidden = new QCheckBox(tr("Show hidden"), this);
    m_showHidden->setChecked(false);
    m_showHidden->setToolTip(tr("Show hidden and excluded items"));
    filterRow->addWidget(m_showHidden);
    layout->addLayout(filterRow);

    connect(m_filterEdit, &QLineEdit::textChanged, this, &NavigatorPanel::onFilterTextChanged);
    connect(m_showHidden, &QCheckBox::toggled, this, &NavigatorPanel::showHiddenChanged);

    // Sprite tree
    m_spriteTree = new NavigatorTreeWidget(this);
    m_spriteTree->setHeaderLabel(tr("Sprites"));
    m_spriteTree->setIconSize(QSize(20, 20));
    m_spriteTree->setSortingEnabled(true);
    m_spriteTree->sortByColumn(0, Qt::AscendingOrder);
    m_spriteTree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_spriteTree);

    // Add Source button — hidden by default; shown in Sprites workspace
    m_addSourceBtn = new QPushButton(
        AppIcons::icon(QStringLiteral("add")),
        tr("Add Source"), this);
    m_addSourceBtn->setVisible(false);
    layout->addWidget(m_addSourceBtn);

    connect(m_addSourceBtn, &QPushButton::clicked, this, [this]() {
        QMenu* menu = new QMenu(this);
        auto* folderAction  = menu->addAction(AppIcons::icon(QStringLiteral("folder-add")), tr("Folder..."));
        auto* imageAction   = menu->addAction(AppIcons::icon(QStringLiteral("image-add")), tr("Image..."));
        auto* archiveAction = menu->addAction(AppIcons::icon(QStringLiteral("archive")), tr("Archive..."));
        auto* urlAction     = menu->addAction(AppIcons::icon(QStringLiteral("globe-add")), tr("URL..."));
        QAction* chosen = menu->exec(
            m_addSourceBtn->mapToGlobal(QPoint(0, m_addSourceBtn->height())));
        if      (chosen == folderAction)  emit addSourceFolderRequested();
        else if (chosen == imageAction)   emit addSourceImageRequested();
        else if (chosen == archiveAction) emit addSourceArchiveRequested();
        else if (chosen == urlAction)     emit addSourceUrlRequested();
        menu->deleteLater();
    });

    // Forward excludeRequested from the tree
    connect(m_spriteTree, &NavigatorTreeWidget::excludeRequested,
            this, &NavigatorPanel::excludeKeyPressed);

    // Checkbox cascade: checking/unchecking a group propagates to all descendants;
    // checking/unchecking a leaf updates ancestor folder tristate.
    connect(m_spriteTree, &QTreeWidget::itemChanged,
            this, [this](QTreeWidgetItem* item, int) {
        if (!m_checkboxesEnabled) return;
        const Qt::CheckState cs = item->checkState(0);
        if (cs == Qt::PartiallyChecked) return; // tristate update — don't cascade further

        // Propagate down to all descendants.
        if (item->childCount() > 0) {
            m_spriteTree->blockSignals(true);
            std::function<void(QTreeWidgetItem*)> cascade = [&](QTreeWidgetItem* node) {
                for (int i = 0; i < node->childCount(); ++i) {
                    node->child(i)->setCheckState(0, cs);
                    cascade(node->child(i));
                }
            };
            cascade(item);
            m_spriteTree->blockSignals(false);
        }

        // Propagate up: recompute tristate for each ancestor.
        m_spriteTree->blockSignals(true);
        for (QTreeWidgetItem* p = item->parent(); p; p = p->parent())
            updateFolderCheckState(p);
        m_spriteTree->blockSignals(false);
    });
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void NavigatorPanel::configure(const Config& config)
{
    setAtlasComboVisible(config.atlasCombo);
    setShowHiddenVisible(config.showHidden);
    setCheckboxesEnabled(config.checkboxes);
    setSelectionMode(config.selectionMode);
    setAddSourceButtonVisible(config.addSourceButton);
}

void NavigatorPanel::setAtlasComboVisible(bool visible)
{
    if (m_atlasRow) m_atlasRow->setVisible(visible);
}

void NavigatorPanel::setShowHiddenVisible(bool visible)
{
    if (m_showHidden) m_showHidden->setVisible(visible);
}

void NavigatorPanel::setCheckboxesEnabled(bool enabled)
{
    m_checkboxesEnabled = enabled;
}

void NavigatorPanel::setSelectionMode(QAbstractItemView::SelectionMode mode)
{
    if (m_spriteTree) m_spriteTree->setSelectionMode(mode);
}

void NavigatorPanel::setAddSourceButtonVisible(bool visible)
{
    if (m_addSourceBtn) m_addSourceBtn->setVisible(visible);
}

void NavigatorPanel::setGroupSimilar(bool group)
{
    m_groupSimilar = group;
}

void NavigatorPanel::setSpriteBadgeCallback(SpriteBadgeCallback cb)
{
    m_badgeCallback = std::move(cb);
    if (m_spriteTree) {
        m_spriteTree->setIconSize(m_badgeCallback
            ? QSize(12 + 2 + 20, 20)   // badge + gap + thumbnail
            : QSize(20, 20));
    }
}

void NavigatorPanel::setSpriteFilter(SpriteFilter filter)
{
    m_spriteFilter = std::move(filter);
}

QStringList NavigatorPanel::selectedSpritePaths() const
{
    QStringList paths;
    if (!m_spriteTree) return paths;
    const QList<QTreeWidgetItem*> selected = m_spriteTree->selectedItems();
    const QSet<QTreeWidgetItem*> selectedSet(selected.begin(), selected.end());
    auto isUnderSelection = [&](QTreeWidgetItem* item) {
        for (QTreeWidgetItem* node = item; node; node = node->parent()) {
            if (selectedSet.contains(node)) return true;
        }
        return false;
    };
    QTreeWidgetItemIterator it(m_spriteTree);
    while (*it) {
        QTreeWidgetItem* item = *it;
        ++it;
        if (item->isHidden() || !isUnderSelection(item)) continue;
        const auto sprite = item->data(0, Qt::UserRole).value<SpritePtr>();
        if (sprite && !sprite->path.isEmpty() && !paths.contains(sprite->path))
            paths.append(sprite->path);
    }
    return paths;
}

QTreeWidgetItem* NavigatorPanel::findSpriteItem(const QString& path) const
{
    if (!m_spriteTree || path.isEmpty()) return nullptr;
    const QString cleanPath = QDir::cleanPath(path);
    QTreeWidgetItemIterator it(m_spriteTree);
    while (*it) {
        const auto sprite = (*it)->data(0, Qt::UserRole).value<SpritePtr>();
        if (sprite && QDir::cleanPath(sprite->path) == cleanPath) return *it;
        ++it;
    }
    return nullptr;
}

QTreeWidgetItem* NavigatorPanel::firstSpriteItem() const
{
    if (!m_spriteTree) return nullptr;
    QTreeWidgetItemIterator it(m_spriteTree);
    while (*it) {
        const auto sprite = (*it)->data(0, Qt::UserRole).value<SpritePtr>();
        if (sprite && !sprite->path.isEmpty()) return *it;
        ++it;
    }
    return nullptr;
}

void NavigatorPanel::refresh(const ProjectSession* session, bool showHidden, int atlasFilter)
{
    buildTree(session, showHidden, atlasFilter);
    // Re-apply any active filter after rebuild
    if (m_filterEdit && !m_filterEdit->text().isEmpty())
        applyFilter(m_filterEdit->text());
}

void NavigatorPanel::updateAtlasCombo(const QVector<AtlasEntry>& atlases, int activeSessionIndex)
{
    if (!m_atlasCombo) return;
    m_atlasCombo->blockSignals(true);
    m_atlasCombo->clear();
    m_atlasCombo->addItem(tr("All"), -1);
    int selectComboIdx = 0; // Default to "All" if the active atlas is empty/excluded
    for (int i = 0; i < atlases.size(); ++i) {
        const auto& atlas = atlases[i];
        if (atlas.isExcluded) continue;
        if (atlas.spritePaths.isEmpty()) continue; // Hide empty atlases
        if (i == activeSessionIndex)
            selectComboIdx = m_atlasCombo->count(); // count() accounts for "All" at index 0
        m_atlasCombo->addItem(atlas.name, i); // Store real atlas index as item data
    }
    m_atlasCombo->setCurrentIndex(selectComboIdx);
    m_atlasCombo->blockSignals(false);
}

QString NavigatorPanel::filterText() const
{
    return m_filterEdit ? m_filterEdit->text() : QString();
}

void NavigatorPanel::clearFilter()
{
    if (m_filterEdit) {
        m_filterEdit->blockSignals(true);
        m_filterEdit->clear();
        m_filterEdit->blockSignals(false);
    }
    if (m_filterResult) m_filterResult->setVisible(false);
}

QStringList NavigatorPanel::checkedPaths() const
{
    QStringList paths;
    if (!m_spriteTree) return paths;
    std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* item) {
        if (item->childCount() == 0) {
            if (item->checkState(0) == Qt::Checked) {
                auto sprite = item->data(0, Qt::UserRole).value<SpritePtr>();
                if (sprite && !sprite->path.isEmpty())
                    paths.append(sprite->path);
            }
        } else {
            for (int i = 0; i < item->childCount(); ++i)
                walk(item->child(i));
        }
    };
    for (int i = 0; i < m_spriteTree->topLevelItemCount(); ++i)
        walk(m_spriteTree->topLevelItem(i));
    return paths;
}

void NavigatorPanel::applyFilter(const QString& text)
{
    if (!m_spriteTree) return;

    const FilterMode mode = m_filterModeCombo
        ? static_cast<FilterMode>(m_filterModeCombo->currentIndex())
        : FilterMode::Text;

    // Compile the regex/glob pattern once outside the item loop.
    QRegularExpression rx;
    if (!text.isEmpty() && mode != FilterMode::Text) {
        const QString pattern = (mode == FilterMode::Glob) ? globToRegex(text) : text;
        rx = QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
    }

    QTreeWidgetItemIterator it(m_spriteTree);
    QSet<QTreeWidgetItem*> itemsToShow;

    // First pass: find all items matching the search text
    while (*it) {
        QTreeWidgetItem* item = *it;
        bool matches;
        if (text.isEmpty()) {
            matches = true;
        } else if (mode == FilterMode::Text) {
            matches = item->text(0).contains(text, Qt::CaseInsensitive);
        } else {
            matches = rx.isValid() && rx.match(item->text(0)).hasMatch();
        }
        if (matches) {
            itemsToShow.insert(item);
            QTreeWidgetItem* parent = item->parent();
            while (parent) {
                itemsToShow.insert(parent);
                parent = parent->parent();
            }
        }
        ++it;
    }

    // Second pass: apply visibility and count visible leaves
    int visibleLeaves = 0;
    int totalLeaves   = 0;
    QTreeWidgetItemIterator it2(m_spriteTree);
    while (*it2) {
        QTreeWidgetItem* item = *it2;
        const bool visible = itemsToShow.contains(item);
        const bool isLeaf = item->data(0, Qt::UserRole).isValid();
        if (isLeaf) {
            ++totalLeaves;
            if (visible) ++visibleLeaves;
        }
        item->setHidden(!visible);
        ++it2;
    }

    if (m_filterResult) {
        const bool filtering = !text.isEmpty();
        m_filterResult->setVisible(filtering);
        if (filtering) {
            if (visibleLeaves == 0)
                m_filterResult->setText(tr("No results"));
            else
                m_filterResult->setText(tr("%1/%2").arg(visibleLeaves).arg(totalLeaves));
        }
    }
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------
void NavigatorPanel::onAtlasComboChanged(int comboIdx)
{
    if (!m_atlasCombo || comboIdx < 0) return;
    const int atlasIndex = m_atlasCombo->itemData(comboIdx).toInt();
    emit atlasIndexChanged(atlasIndex);
}

void NavigatorPanel::onFilterTextChanged(const QString& text)
{
    applyFilter(text);
}

// ---------------------------------------------------------------------------
// Tree building
// ---------------------------------------------------------------------------
void NavigatorPanel::buildTree(const ProjectSession* session, bool showHidden, int atlasFilter)
{
    if (!m_spriteTree) return;

    // ── Save tree state before the rebuild ──────────────────────────────────
    auto treeItemKey = [](QTreeWidgetItem* node) -> QString {
        QStringList parts;
        while (node) { parts.prepend(node->text(0)); node = node->parent(); }
        return parts.join(QChar(0x1F));
    };
    const bool hadItems = m_spriteTree->invisibleRootItem()->childCount() > 0;
    QSet<QString> collapsedKeys;
    QSet<QString> checkedPaths;
    int scrollPos = 0;
    if (hadItems) {
        QTreeWidgetItemIterator sit(m_spriteTree);
        while (*sit) {
            const QVariant v = (*sit)->data(0, Qt::UserRole);
            if (v.isValid()) {
                if (m_checkboxesEnabled && (*sit)->checkState(0) == Qt::Checked) {
                    const auto sprite = v.value<SpritePtr>();
                    if (sprite) checkedPaths.insert(sprite->path);
                }
            } else {
                if (!(*sit)->isExpanded())
                    collapsedKeys.insert(treeItemKey(*sit));
            }
            ++sit;
        }
        scrollPos = m_spriteTree->verticalScrollBar()->value();
    }

    // Clear filter display but don't touch the filter text
    if (m_filterResult) m_filterResult->setVisible(false);

    m_spriteTree->blockSignals(true);
    m_spriteTree->clear();

    // Check whether there's anything to show
    if (!session) {
        m_spriteTree->blockSignals(false);
        return;
    }

    const bool nothingToShow = (atlasFilter >= 0 && atlasFilter < session->atlases.size())
        ? session->atlases[atlasFilter].spritePaths.isEmpty()
        : session->activeFramePaths.isEmpty();

    if (nothingToShow) {
        m_spriteTree->blockSignals(false);
        return;
    }

    const QIcon folderIcon    = AppIcons::icon(QStringLiteral("folder"));
    const QIcon animGroupIcon = AppIcons::icon(QStringLiteral("folder-virtual"));

    auto makeGroupNode = [&](QTreeWidgetItem* parent, const QString& text) -> QTreeWidgetItem* {
        auto* node = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_spriteTree);
        node->setText(0, text);
        node->setIcon(0, folderIcon);
        if (m_checkboxesEnabled) {
            node->setFlags(node->flags() | Qt::ItemIsUserCheckable);
            node->setCheckState(0, Qt::Unchecked);
        }
        return node;
    };

    using SpriteLeaf = QPair<SpritePtr, QString>;

    // ── Collect sprites ──────────────────────────────────────────────────────
    QVector<SpritePtr> allSprites;

    if (atlasFilter >= 0 && atlasFilter < session->atlases.size()) {
        // atlasFilter mode: show only the sprites listed in spritePaths for the
        // selected atlas. Look up SpritePtr from spriteIndex (populated by
        // rebuildSpriteIndex; covers both pre- and post-layout sprites).
        for (const QString& p : session->atlases[atlasFilter].spritePaths) {
            const QString key = QDir::cleanPath(p);
            auto it = session->spriteIndex.find(key);
            if (it != session->spriteIndex.end())
                allSprites.append(it.value());
        }
    } else {
        // Default: all sprites from all non-excluded atlases via spritePaths +
        // spriteIndex.  Using spritePaths (not layoutModels) ensures that sprites
        // assigned to a named atlas that has never been laid out are still shown —
        // layoutModels is empty for such atlases, which would cause them to vanish
        // from the Sprites workspace whenever the Atlases workspace was used.
        QSet<QString> seen;
        for (const auto& atlas : session->atlases) {
            if (atlas.isExcluded) continue;
            for (const QString& p : atlas.spritePaths) {
                const QString key = QDir::cleanPath(p);
                if (seen.contains(key)) continue;
                seen.insert(key);
                auto it = session->spriteIndex.find(key);
                if (it != session->spriteIndex.end())
                    allSprites.append(it.value());
            }
        }
    }

    if (m_spriteFilter) {
        allSprites.erase(std::remove_if(allSprites.begin(), allSprites.end(),
                                        [this](const SpritePtr& sp) { return !sp || !m_spriteFilter(sp); }),
                         allSprites.end());
    }

    if (allSprites.isEmpty()) {
        m_spriteTree->blockSignals(false);
        return;
    }

    QMap<QString, SpritePtr> spriteByPath;
    for (const SpritePtr& sp : allSprites)
        spriteByPath[sp->path] = sp;
    QStringList pendingThumbnails;  // not cached yet: decoded off the UI thread below
    QSet<QString> pendingSet;

    auto makeLeafCb = [&](QTreeWidgetItem* parent, const QString& path, const QString& leafName) {
        auto* leaf = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_spriteTree);
        leaf->setText(0, leafName);
        if (m_checkboxesEnabled) {
            leaf->setFlags(leaf->flags() | Qt::ItemIsUserCheckable);
            leaf->setCheckState(0, Qt::Unchecked);
        }
        const SpritePtr sp = spriteByPath.value(path);
        leaf->setData(0, Qt::UserRole, QVariant::fromValue(sp));
        if (const QImage* thumbnail = cachedThumbnail(path)) {
            if (!thumbnail->isNull()) applyThumbnail(leaf, QIcon(QPixmap::fromImage(*thumbnail)));
        } else if (!pendingSet.contains(path)) {
            pendingSet.insert(path);
            pendingThumbnails.append(path);
        }
    };

    auto findOrCreateFolderPath = [&](QTreeWidgetItem* root, const QStringList& parts) -> QTreeWidgetItem* {
        QTreeWidgetItem* current = root;
        for (const QString& part : parts) {
            QTreeWidgetItem* found = nullptr;
            int childCount = current ? current->childCount() : m_spriteTree->topLevelItemCount();
            for (int i = 0; i < childCount; ++i) {
                QTreeWidgetItem* child = current ? current->child(i) : m_spriteTree->topLevelItem(i);
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

    auto toEntries = [](const QVector<SpriteLeaf>& leaves) -> QVector<QPair<QString, QString>> {
        QVector<QPair<QString, QString>> result;
        result.reserve(leaves.size());
        for (const auto& [sp, name] : leaves)
            result.append({sp->path, name});
        return result;
    };

    if (!session->sources.isEmpty()) {
        // One or more sources: group sprites under a top-level source node each.
        QVector<QVector<SpriteLeaf>> perSource(session->sources.size());
        QVector<SpriteLeaf> unassigned;

        // Pre-compute cleaned source paths and per-source hidden folder sets so we
        // don't repeat QDir::cleanPath / QSet construction inside the sprite loop.
        QVector<QString> cleanedSourcePaths;
        cleanedSourcePaths.reserve(session->sources.size());
        QVector<QSet<QString>> hiddenSetsPerSource;
        hiddenSetsPerSource.reserve(session->sources.size());
        for (const auto& src : session->sources) {
            cleanedSourcePaths.append(QDir::cleanPath(src.cachedFolderPath));
            hiddenSetsPerSource.append(QSet<QString>(src.hiddenFolders.begin(), src.hiddenFolders.end()));
        }

        for (const SpritePtr& sprite : allSprites) {
            const QString cleanedPath = QDir::cleanPath(sprite->path);
            int bestLen = -1;
            int bestIdx = -1;
            for (int si = 0; si < session->sources.size(); ++si) {
                const QString& cleaned = cleanedSourcePaths[si];
                if (cleaned.isEmpty()) continue;
                if ((cleanedPath.startsWith(cleaned + QLatin1Char('/'))
                        || cleanedPath == cleaned)
                        && cleaned.length() > bestLen) {
                    bestLen = cleaned.length();
                    bestIdx = si;
                }
            }

            if (bestIdx >= 0) {
                const QString& cleanedCache = cleanedSourcePaths[bestIdx];
                QString localName;
                if (cleanedPath.startsWith(cleanedCache + QLatin1Char('/'))) {
                    const QString rel = cleanedPath.mid(cleanedCache.length() + 1);
                    // In atlasFilter mode, sprites explicitly assigned to an atlas are
                    // always shown regardless of excludedFiles.
                    if (atlasFilter < 0 && session->sources[bestIdx].excludedFiles.contains(rel)) continue;
                    const QString dir  = QFileInfo(rel).path();
                    const QString base = QFileInfo(rel).baseName();
                    localName = (dir.isEmpty() || dir == QLatin1String("."))
                                ? base : dir + QLatin1Char('/') + base;
                } else {
                    localName = sprite->name;
                }
                // Strip hidden folder segments (Sprites workspace only; atlas views show full paths)
                if (atlasFilter < 0) {
                    const QSet<QString>& hiddenSet = hiddenSetsPerSource[bestIdx];
                    if (!hiddenSet.isEmpty() && localName.contains('/')) {
                        const QStringList parts = localName.split('/');
                        QStringList resultParts;
                        QString accRelPath;
                        for (int i = 0; i < parts.size() - 1; ++i) {
                            if (!accRelPath.isEmpty()) accRelPath += '/';
                            accRelPath += parts[i];
                            if (!hiddenSet.contains(accRelPath))
                                resultParts.append(parts[i]);
                        }
                        resultParts.append(parts.last());
                        localName = resultParts.join('/');
                    }
                }
                perSource[bestIdx].append({sprite, localName});
            } else {
                unassigned.append({sprite, sprite->name});
            }
        }

        for (int si = 0; si < session->sources.size(); ++si) {
            if (perSource[si].isEmpty() && !session->sources[si].hasError) continue;
            const auto& src = session->sources[si];
            const int hiddenCount = src.hiddenFolders.size();
            const QString nodeText = (!showHidden && hiddenCount > 0)
                ? tr("%1 (%2 hidden)").arg(src.name).arg(hiddenCount)
                : src.name;
            auto* sourceNode = makeGroupNode(nullptr, nodeText);
            QFont f = sourceNode->font(0);
            f.setBold(true);
            sourceNode->setFont(0, f);
            sourceNode->setData(0, Qt::UserRole + 1, si);

            QIcon sourceIcon;
            QString typeLabel;
            switch (src.type) {
            case SourceType::Folder:
                sourceIcon = AppIcons::icon(QStringLiteral("folder"));
                typeLabel = tr("Folder");
                break;
            case SourceType::SingleImage:
                sourceIcon = src.originalPath.endsWith(".gif", Qt::CaseInsensitive)
                    ? AppIcons::icon(QStringLiteral("image-animated"))
                    : AppIcons::icon(QStringLiteral("image"));
                typeLabel = tr("Image");
                break;
            case SourceType::Archive:
                sourceIcon = AppIcons::icon(QStringLiteral("archive"));
                typeLabel = tr("Archive");
                break;
            case SourceType::Url:
                sourceIcon = AppIcons::icon(QStringLiteral("globe"));
                typeLabel = tr("URL");
                break;
            }

            if (src.hasError) {
                sourceNode->setIcon(0, AppIcons::icon(QStringLiteral("error")));
                sourceNode->setToolTip(0, tr("Failed to load — right-click to retry\n%1: %2")
                    .arg(typeLabel, src.originalPath));
                // Add a child item showing the error detail
                const QColor errColor = QApplication::palette().color(QPalette::Disabled, QPalette::Text);
                auto* errItem = new QTreeWidgetItem(sourceNode);
                errItem->setText(0, src.errorMessage.isEmpty()
                    ? tr("Load failed") : src.errorMessage);
                errItem->setForeground(0, errColor);
                {
                    QFont ef = errItem->font(0);
                    ef.setItalic(true);
                    errItem->setFont(0, ef);
                }
                errItem->setFlags(errItem->flags() & ~Qt::ItemIsUserCheckable);
                errItem->setData(0, Qt::UserRole + 2, 4);  // type 4 = error detail item
            } else {
                sourceNode->setIcon(0, sourceIcon);
                sourceNode->setToolTip(0, typeLabel + ": " + src.originalPath);
            }

            if (!perSource[si].isEmpty()) {
                SpriteTreeUtils::buildSubTree(m_spriteTree, sourceNode, toEntries(perSource[si]),
                    folderIcon, animGroupIcon, m_checkboxesEnabled, makeLeafCb, m_groupSimilar);
            }

            // ── Hidden-folder placeholders (Sprites workspace only, showHidden ON) ──
            if (atlasFilter < 0 && showHidden && !src.hiddenFolders.isEmpty()) {
                const QSet<QString> hiddenSet(src.hiddenFolders.begin(), src.hiddenFolders.end());
                const QColor dimColor = QApplication::palette().color(QPalette::Disabled, QPalette::Text);
                for (const QString& relHidden : src.hiddenFolders) {
                    const QString folderName = QFileInfo(relHidden).fileName();
                    const QString parentRel  = QFileInfo(relHidden).path();

                    QString effectiveParentPath;
                    if (parentRel != QLatin1String(".") && !parentRel.isEmpty()) {
                        const QStringList parentParts = parentRel.split('/');
                        QStringList resultParts;
                        QString acc;
                        for (const QString& part : parentParts) {
                            if (!acc.isEmpty()) acc += '/';
                            acc += part;
                            if (!hiddenSet.contains(acc)) resultParts.append(part);
                        }
                        effectiveParentPath = resultParts.join('/');
                    }

                    QTreeWidgetItem* parentNode = sourceNode;
                    if (!effectiveParentPath.isEmpty())
                        parentNode = findOrCreateFolderPath(sourceNode, effectiveParentPath.split('/'));

                    auto* placeholder = new QTreeWidgetItem(parentNode);
                    placeholder->setText(0, folderName);
                    placeholder->setIcon(0, folderIcon);
                    QFont pf = placeholder->font(0);
                    pf.setItalic(true);
                    placeholder->setFont(0, pf);
                    placeholder->setForeground(0, dimColor);
                    placeholder->setToolTip(0, tr("Hidden — right-click to unhide"));
                    placeholder->setData(0, Qt::UserRole + 2, 1);
                    placeholder->setData(0, Qt::UserRole + 3, si);
                    placeholder->setData(0, Qt::UserRole + 4, relHidden);
                    placeholder->setFlags(placeholder->flags() & ~Qt::ItemIsUserCheckable);
                }
            }

            // ── Per-source "Excluded" trash node (Sprites workspace only) ──────
            if (atlasFilter < 0 && !src.excludedFiles.isEmpty()) {
                const QColor dimColor = QApplication::palette().color(QPalette::Disabled, QPalette::Text);
                const int N = src.excludedFiles.size();

                auto* trashNode = new QTreeWidgetItem(sourceNode);
                trashNode->setText(0, tr("Excluded (%1)").arg(N));
                trashNode->setIcon(0, AppIcons::icon(QStringLiteral("delete")));
                {
                    QFont tf = trashNode->font(0);
                    tf.setItalic(true);
                    trashNode->setFont(0, tf);
                }
                trashNode->setForeground(0, dimColor);
                trashNode->setToolTip(0, tr("Sprites excluded from layout — right-click to re-include"));
                trashNode->setData(0, Qt::UserRole + 2, 3);
                trashNode->setData(0, Qt::UserRole + 3, si);
                trashNode->setFlags(trashNode->flags() & ~Qt::ItemIsUserCheckable);
                trashNode->setExpanded(true);

                auto findOrCreateExclFolder = [&](auto& self, QTreeWidgetItem* root, const QStringList& parts) -> QTreeWidgetItem* {
                    Q_UNUSED(self);
                    QTreeWidgetItem* current = root;
                    for (const QString& part : parts) {
                        QTreeWidgetItem* found = nullptr;
                        for (int i = 0; i < current->childCount(); ++i) {
                            QTreeWidgetItem* child = current->child(i);
                            if (child->text(0) == part && !child->data(0, Qt::UserRole).isValid()
                                    && child->data(0, Qt::UserRole + 2).toInt() == 0) {
                                found = child;
                                break;
                            }
                        }
                        if (!found) {
                            found = new QTreeWidgetItem(current);
                            found->setText(0, part);
                            found->setIcon(0, folderIcon);
                            found->setForeground(0, dimColor);
                            found->setFlags(found->flags() & ~Qt::ItemIsUserCheckable);
                            found->setExpanded(true);
                        }
                        current = found;
                    }
                    return current;
                };

                for (const QString& relPath : src.excludedFiles) {
                    const QStringList parts = relPath.split('/');
                    const QString baseName  = QFileInfo(relPath).baseName();

                    QTreeWidgetItem* parent = trashNode;
                    if (parts.size() > 1)
                        parent = findOrCreateExclFolder(findOrCreateExclFolder, trashNode, parts.mid(0, parts.size() - 1));

                    auto* exclItem = new QTreeWidgetItem(parent);
                    exclItem->setText(0, baseName);
                    exclItem->setForeground(0, dimColor);
                    {
                        QFont ef = exclItem->font(0);
                        ef.setItalic(true);
                        exclItem->setFont(0, ef);
                    }
                    exclItem->setToolTip(0, tr("Excluded from layout — right-click to re-include"));
                    exclItem->setData(0, Qt::UserRole + 2, 2);
                    exclItem->setData(0, Qt::UserRole + 3, si);
                    exclItem->setData(0, Qt::UserRole + 4, relPath);
                    exclItem->setFlags(exclItem->flags() & ~Qt::ItemIsUserCheckable);
                }
            }
        }

        if (!unassigned.isEmpty()) {
            auto* otherNode = makeGroupNode(nullptr, tr("Other"));
            SpriteTreeUtils::buildSubTree(m_spriteTree, otherNode, toEntries(unassigned),
                folderIcon, animGroupIcon, m_checkboxesEnabled, makeLeafCb, m_groupSimilar);
        }
    } else {
        // Single source (or no source tracking): flat list from all sprites.
        QVector<QPair<QString, QString>> entries;
        entries.reserve(allSprites.size());
        for (const auto& sprite : allSprites)
            entries.append({sprite->path, sprite->name});
        SpriteTreeUtils::buildSubTree(m_spriteTree, nullptr, entries,
            folderIcon, animGroupIcon, m_checkboxesEnabled, makeLeafCb, m_groupSimilar);
    }

    // ── Restore tree state ───────────────────────────────────────────────────
    {
        QTreeWidgetItemIterator rit(m_spriteTree);
        while (*rit) {
            if ((*rit)->data(0, Qt::UserRole + 2).toInt() > 0) {
                ++rit; continue;
            }
            const QVariant v = (*rit)->data(0, Qt::UserRole);
            if (v.isValid()) {
                if (m_checkboxesEnabled) {
                    const auto sprite = v.value<SpritePtr>();
                    if (sprite && checkedPaths.contains(sprite->path))
                        (*rit)->setCheckState(0, Qt::Checked);
                }
            } else {
                (*rit)->setExpanded(!collapsedKeys.contains(treeItemKey(*rit)));
            }
            ++rit;
        }
        if (m_checkboxesEnabled) {
            for (int i = 0; i < m_spriteTree->topLevelItemCount(); ++i)
                updateFolderCheckState(m_spriteTree->topLevelItem(i));
        }
    }

    m_spriteTree->sortItems(0, Qt::AscendingOrder);

    // ── Pin "Excluded" trash nodes to the top of their source node ───────────
    // sortItems above sorts everything alphabetically; items like "attack" would
    // sort before "Excluded (N)". Disable sorting and manually reposition each
    // trash node (UserRole+2 == 3) to index 0 of its parent source node.
    m_spriteTree->setSortingEnabled(false);
    for (int si = 0; si < m_spriteTree->topLevelItemCount(); ++si) {
        QTreeWidgetItem* srcNode = m_spriteTree->topLevelItem(si);
        for (int j = 0; j < srcNode->childCount(); ++j) {
            if (srcNode->child(j)->data(0, Qt::UserRole + 2).toInt() == 3) {
                QTreeWidgetItem* trash = srcNode->takeChild(j);
                srcNode->insertChild(0, trash);
                trash->setExpanded(true);
                break;
            }
        }
    }

    m_spriteTree->blockSignals(false);

    if (hadItems)
        m_spriteTree->verticalScrollBar()->setValue(scrollPos);
    if (!pendingThumbnails.isEmpty()) loadThumbnailsAsync(pendingThumbnails);
}

void NavigatorPanel::applyThumbnail(QTreeWidgetItem* leaf, const QIcon& thumbnail)
{
    const auto sp = leaf->data(0, Qt::UserRole).value<SpritePtr>();
    if (!m_badgeCallback || !sp) {
        leaf->setIcon(0, thumbnail);
        return;
    }
    // Always allocate [badge | gap | thumbnail] so all items align.
    constexpr int badgeSize = 12;
    constexpr int gap       = 2;
    QPixmap combined(badgeSize + gap + THUMBNAIL_SIZE, THUMBNAIL_SIZE);
    combined.fill(Qt::transparent);
    QPainter painter(&combined);
    const QIcon badge = m_badgeCallback(sp);
    if (!badge.isNull()) {
        const int badgeY = (THUMBNAIL_SIZE - badgeSize) / 2;
        painter.drawPixmap(0, badgeY, badge.pixmap(badgeSize, badgeSize));
    }
    painter.drawPixmap(badgeSize + gap, 0, thumbnail.pixmap(THUMBNAIL_SIZE, THUMBNAIL_SIZE));
    leaf->setIcon(0, QIcon(combined));
}

void NavigatorPanel::loadThumbnailsAsync(const QStringList& paths)
{
    ThumbnailLoader::instance()->load(paths);
}

void NavigatorPanel::applyLoadedThumbnails(const QHash<QString, QImage>& thumbnails)
{
    // An icon change is an itemChanged, which the tree's users handle as a check change.
    const QSignalBlocker blocker(m_spriteTree);
    for (QTreeWidgetItemIterator it(m_spriteTree); *it; ++it) {
        const auto sp = (*it)->data(0, Qt::UserRole).value<SpritePtr>();
        if (!sp) continue;
        const auto image = thumbnails.constFind(sp->path);
        if (image != thumbnails.constEnd() && !image->isNull()) applyThumbnail(*it, QIcon(QPixmap::fromImage(*image)));
    }
}
