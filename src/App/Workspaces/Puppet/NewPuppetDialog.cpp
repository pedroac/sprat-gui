#include "NewPuppetDialog.h"
#include "NavigatorPanel.h"
#include "NavigatorTreeWidget.h"
#include "PuppetEditing.h"

#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

/// What the Sprites tree drags: sprite paths, one per line.
const char* SPRITE_MIME = "application/x-sprat-sprite";
const char* WARNING_STYLE = "color: #d9534f;";
constexpr int DIALOG_WIDTH = 820;
constexpr int DIALOG_HEIGHT = 560;
/// The part index of a row, and for a sprite row its variant index.
constexpr int PART_ROLE = Qt::UserRole;
constexpr int VARIANT_ROLE = Qt::UserRole + 1;

QStringList draggedSprites(const QMimeData* mime) {
    if (!mime || !mime->hasFormat(QLatin1String(SPRITE_MIME))) return {};
    return QString::fromUtf8(mime->data(QLatin1String(SPRITE_MIME))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

/// The parts list: takes sprites dropped from the Sprites tree, on a part or on empty space.
class PartsTree : public QTreeWidget {
public:
    using DropHandler = std::function<void(int partIndex, const QStringList& spritePaths)>;

    explicit PartsTree(QWidget* parent) : QTreeWidget(parent) {
        setAcceptDrops(true);
        viewport()->setAcceptDrops(true);
        setDragDropMode(QAbstractItemView::DropOnly);
        setDropIndicatorShown(false);
    }

    void setDropHandler(DropHandler handler) { m_onDrop = std::move(handler); }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (draggedSprites(event->mimeData()).isEmpty()) {
            event->ignore();
            return;
        }
        event->acceptProposedAction();
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        if (draggedSprites(event->mimeData()).isEmpty()) {
            event->ignore();
            return;
        }
        // The part it would go to is highlighted; none over empty space (a new part).
        QTreeWidgetItem* part = partItemAt(event->position().toPoint());
        if (part) {
            setCurrentItem(part);
        } else {
            clearSelection();
            setCurrentItem(nullptr);
        }
        event->acceptProposedAction();
    }

    void dropEvent(QDropEvent* event) override {
        const QStringList paths = draggedSprites(event->mimeData());
        if (paths.isEmpty()) {
            event->ignore();
            return;
        }
        QTreeWidgetItem* part = partItemAt(event->position().toPoint());
        event->acceptProposedAction();
        if (m_onDrop) m_onDrop(part ? part->data(0, PART_ROLE).toInt() : -1, paths);
    }

private:
    QTreeWidgetItem* partItemAt(const QPoint& pos) const {
        QTreeWidgetItem* item = itemAt(pos);
        while (item && item->parent()) item = item->parent();
        return item;
    }

    DropHandler m_onDrop;
};

}  // namespace

NewPuppetDialog::NewPuppetDialog(const ProjectSession* session, const QVector<Puppet>& existing,
                                 std::function<bool(const SpritePtr&)> filter, QWidget* parent)
    : QDialog(parent)
    , m_existing(existing)
{
    setWindowTitle(tr("New Puppet"));
    resize(DIALOG_WIDTH, DIALOG_HEIGHT);
    auto* layout = new QVBoxLayout(this);

    // Name
    auto* nameForm = new QFormLayout;
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName(QStringLiteral("newPuppetName"));
    m_nameEdit->setText(PuppetEditing::uniqueName(tr("puppet"), PuppetEditing::puppetNames(existing)));
    m_nameEdit->selectAll();
    nameForm->addRow(tr("Name:"), m_nameEdit);
    layout->addLayout(nameForm);
    m_nameProblem = new QLabel(this);
    m_nameProblem->setStyleSheet(QLatin1String(WARNING_STYLE));
    m_nameProblem->hide();
    layout->addWidget(m_nameProblem);

    // Parts
    auto* partsPanel = new QWidget(this);
    auto* partsLayout = new QVBoxLayout(partsPanel);
    partsLayout->setContentsMargins(0, 0, 0, 0);
    partsLayout->addWidget(new QLabel(tr("<b>Parts</b>"), partsPanel));
    auto* partsTree = new PartsTree(partsPanel);
    partsTree->setDropHandler([this](int part, const QStringList& paths) { addSprites(part, paths); });
    m_partsTree = partsTree;
    m_partsTree->setObjectName(QStringLiteral("newPuppetParts"));
    m_partsTree->setHeaderHidden(true);
    m_partsTree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_partsTree->setToolTip(tr("Drag sprites onto a part to add them to it, or onto empty space to "
                               "make a part of each. Double-click a part to rename it"));
    partsLayout->addWidget(m_partsTree, 1);
    m_partsProblem = new QLabel(partsPanel);
    m_partsProblem->setObjectName(QStringLiteral("newPuppetPartProblem"));
    m_partsProblem->setStyleSheet(QLatin1String(WARNING_STYLE));
    m_partsProblem->setWordWrap(true);
    m_partsProblem->hide();
    partsLayout->addWidget(m_partsProblem);
    auto* partButtons = new QHBoxLayout;
    auto* addPartButton = new QPushButton(tr("Add Part"), partsPanel);
    addPartButton->setObjectName(QStringLiteral("newPuppetAddPart"));
    m_removeButton = new QPushButton(tr("Remove"), partsPanel);
    m_removeButton->setObjectName(QStringLiteral("newPuppetRemove"));
    m_removeButton->setToolTip(tr("Remove the selected part, or the selected sprite from its part (Delete)"));
    partButtons->addWidget(addPartButton);
    partButtons->addWidget(m_removeButton);
    partButtons->addStretch();
    partsLayout->addLayout(partButtons);

    // Sprites
    auto* spritesPanel = new QWidget(this);
    auto* spritesLayout = new QVBoxLayout(spritesPanel);
    spritesLayout->setContentsMargins(0, 0, 0, 0);
    spritesLayout->addWidget(new QLabel(tr("<b>Sprites</b>"), spritesPanel));
    m_sprites = new NavigatorPanel(spritesPanel);
    m_sprites->configure({
        /* atlasCombo      */ false,
        /* showHidden      */ false,
        /* checkboxes      */ false,
        /* filterBar       */ true,
        /* addSourceButton */ false,
        /* selectionMode   */ QAbstractItemView::ExtendedSelection
    });
    m_sprites->setSpriteFilter(std::move(filter));
    m_sprites->refresh(session, false);
    spritesLayout->addWidget(m_sprites, 1);
    m_addToPartButton = new QPushButton(spritesPanel);
    m_addToPartButton->setObjectName(QStringLiteral("newPuppetAddToPart"));
    spritesLayout->addWidget(m_addToPartButton);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(partsPanel);
    splitter->addWidget(spritesPanel);
    splitter->setChildrenCollapsible(false);
    splitter->setSizes({DIALOG_WIDTH / 2, DIALOG_WIDTH / 2});
    layout->addWidget(splitter, 1);

    auto* hint = new QLabel(tr("Drag sprites onto a part, or onto empty space to make a part of each. "
                               "Attach the parts to each other in the workspace."), this);
    hint->setWordWrap(true);
    hint->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(hint);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    layout->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &NewPuppetDialog::checkName);
    connect(addPartButton, &QPushButton::clicked, this, &NewPuppetDialog::addPart);
    connect(m_removeButton, &QPushButton::clicked, this, &NewPuppetDialog::removeSelected);
    auto* removeShortcut = new QShortcut(QKeySequence::Delete, m_partsTree);
    removeShortcut->setContext(Qt::WidgetShortcut);
    connect(removeShortcut, &QShortcut::activated, this, &NewPuppetDialog::removeSelected);
    connect(m_partsTree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item) {
        if (!m_rebuilding) renamePart(item);
    });
    connect(m_partsTree, &QTreeWidget::currentItemChanged, this, &NewPuppetDialog::updateButtons);
    connect(m_sprites->tree(), &QTreeWidget::itemSelectionChanged, this, &NewPuppetDialog::updateButtons);
    connect(m_addToPartButton, &QPushButton::clicked, this, [this]() {
        addSprites(selectedPart(), m_sprites->selectedSpritePaths());
    });

    m_nameEdit->setFocus();
    rebuildParts(-1);
    checkName();
}

Puppet NewPuppetDialog::puppet() const {
    Puppet puppet = m_puppet;
    puppet.name = m_nameEdit->text().trimmed();
    return puppet;
}

void NewPuppetDialog::addPart() {
    PuppetPart part;
    part.name = PuppetEditing::uniqueName(tr("part"), PuppetEditing::partNames(m_puppet));
    part.position = QPoint(0, 0);
    m_puppet.parts.append(part);
    rebuildParts(m_puppet.parts.size() - 1);
}

void NewPuppetDialog::addSprites(int partIndex, const QStringList& spritePaths) {
    if (spritePaths.isEmpty()) return;
    if (partIndex >= 0 && partIndex < m_puppet.parts.size()) {
        PuppetEditing::addSpriteVariants(m_puppet, partIndex, spritePaths);
        rebuildParts(partIndex);
        return;
    }
    for (const QString& path : spritePaths) {
        PuppetPart part;
        part.name = PuppetEditing::uniqueName(puppetSpriteName(path), PuppetEditing::partNames(m_puppet));
        part.position = QPoint(0, 0);
        m_puppet.parts.append(part);
        PuppetEditing::addSpriteVariants(m_puppet, m_puppet.parts.size() - 1, {path});
    }
    rebuildParts(m_puppet.parts.size() - 1);
}

void NewPuppetDialog::removeSelected() {
    QTreeWidgetItem* item = m_partsTree->currentItem();
    if (!item) return;
    const int part = item->data(0, PART_ROLE).toInt();
    const QVariant variant = item->data(0, VARIANT_ROLE);
    if (variant.isValid()) {
        PuppetEditing::removeVariant(m_puppet, part, variant.toInt());
        rebuildParts(part);
    } else {
        PuppetEditing::removePart(m_puppet, part);
        rebuildParts(qMin(part, int(m_puppet.parts.size()) - 1));
    }
}

void NewPuppetDialog::rebuildParts(int selectPart, int selectVariant) {
    m_rebuilding = true;
    m_partsTree->clear();
    QTreeWidgetItem* selected = nullptr;
    for (int p = 0; p < m_puppet.parts.size(); ++p) {
        const PuppetPart& part = m_puppet.parts[p];
        auto* partItem = new QTreeWidgetItem(m_partsTree, {part.name});
        partItem->setData(0, PART_ROLE, p);
        partItem->setFlags(partItem->flags() | Qt::ItemIsEditable);
        QFont bold = partItem->font(0);
        bold.setBold(true);
        partItem->setFont(0, bold);
        if (part.variants.isEmpty()) {
            auto* empty = new QTreeWidgetItem(partItem, {tr("(drop sprites here)")});
            empty->setData(0, PART_ROLE, p);
            empty->setForeground(0, palette().color(QPalette::PlaceholderText));
            empty->setFlags(Qt::ItemIsEnabled);
        }
        for (int v = 0; v < part.variants.size(); ++v) {
            const PuppetVariant& variant = part.variants[v];
            auto* variantItem = new QTreeWidgetItem(partItem, {puppetVariantName(variant)});
            variantItem->setData(0, PART_ROLE, p);
            variantItem->setData(0, VARIANT_ROLE, v);
            variantItem->setToolTip(0, variant.source);
            if (QTreeWidgetItem* sprite = m_sprites->findSpriteItem(variant.source))
                variantItem->setIcon(0, sprite->icon(0));
            if (p == selectPart && v == selectVariant) selected = variantItem;
        }
        if (p == selectPart && !selected) selected = partItem;
    }
    m_partsTree->expandAll();
    m_partsTree->setCurrentItem(selected);
    m_rebuilding = false;
    updateButtons();
}

void NewPuppetDialog::checkName() {
    const QString problem = PuppetEditing::puppetNameProblem(m_existing, -1, m_nameEdit->text().trimmed());
    m_nameProblem->setText(problem);
    m_nameProblem->setVisible(!problem.isEmpty());
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
}

void NewPuppetDialog::renamePart(QTreeWidgetItem* item) {
    if (item->parent()) return;
    const int part = item->data(0, PART_ROLE).toInt();
    const QString name = item->text(0).trimmed();
    const QString problem = PuppetEditing::partNameProblem(m_puppet, part, name);
    m_partsProblem->setText(problem);
    m_partsProblem->setVisible(!problem.isEmpty());
    if (problem.isEmpty()) PuppetEditing::renamePart(m_puppet, part, name);
    // Rebuilt after the editor closes: rebuilding inside itemChanged would delete the edited item.
    QMetaObject::invokeMethod(this, [this, part]() { rebuildParts(part); }, Qt::QueuedConnection);
}

int NewPuppetDialog::selectedPart() const {
    const QTreeWidgetItem* item = m_partsTree->currentItem();
    return item && item->isSelected() ? item->data(0, PART_ROLE).toInt() : -1;
}

void NewPuppetDialog::updateButtons() {
    m_removeButton->setEnabled(m_partsTree->currentItem() != nullptr
                               && (m_partsTree->currentItem()->flags() & Qt::ItemIsSelectable));
    const int part = selectedPart();
    m_addToPartButton->setText(part >= 0 ? tr("Add to \"%1\"").arg(m_puppet.parts[part].name)
                                         : tr("Add as New Parts"));
    m_addToPartButton->setToolTip(part >= 0 ? tr("Add the selected sprites to the selected part")
                                            : tr("Make a part of each selected sprite"));
    m_addToPartButton->setEnabled(!m_sprites->selectedSpritePaths().isEmpty());
}
