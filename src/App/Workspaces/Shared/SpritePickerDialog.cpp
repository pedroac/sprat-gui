#include "SpritePickerDialog.h"
#include "NavigatorPanel.h"
#include "NavigatorTreeWidget.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
/// Longest side of the preview image, in pixels.
constexpr int PREVIEW_SIZE = 256;
/// Default dialog size: room for the tree and the preview side by side.
constexpr int DIALOG_WIDTH = 760;
constexpr int DIALOG_HEIGHT = 540;

/// Scales `pixmap` to fit the preview. Small sprites grow by whole factors without
/// smoothing, so pixel art stays sharp.
QPixmap fitPreview(const QPixmap& pixmap)
{
    const double factor = std::min(double(PREVIEW_SIZE) / pixmap.width(),
                                   double(PREVIEW_SIZE) / pixmap.height());
    if (factor >= 2.0) {
        const int whole = int(std::floor(factor));
        return pixmap.scaled(pixmap.size() * whole, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    if (factor >= 1.0) return pixmap;
    return pixmap.scaled(PREVIEW_SIZE, PREVIEW_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

/// Paths of the visible sprites directly in `group` (not in its subgroups), in tree order.
QStringList directSpritePaths(QTreeWidgetItem* group)
{
    QStringList paths;
    for (int i = 0; i < group->childCount(); ++i) {
        QTreeWidgetItem* child = group->child(i);
        if (child->isHidden()) continue;
        const auto sprite = child->data(0, Qt::UserRole).value<SpritePtr>();
        if (sprite && !sprite->path.isEmpty() && !paths.contains(sprite->path))
            paths.append(sprite->path);
    }
    return paths;
}

/// The item's name and its parents' names, from the top of the tree down, joined by "/".
QString treePath(QTreeWidgetItem* item)
{
    QStringList names;
    for (QTreeWidgetItem* node = item; node; node = node->parent())
        names.prepend(node->text(0));
    return names.join(QLatin1Char('/'));
}

/// Paths of the visible sprites under `group`, in tree order.
QStringList spritePathsUnder(QTreeWidgetItem* group)
{
    QStringList paths;
    // QTreeWidgetItemIterator walks on past the group's subtree, so stop at the first item
    // that isn't inside it.
    for (QTreeWidgetItemIterator it(group); *it; ++it) {
        bool inside = false;
        for (QTreeWidgetItem* node = *it; node; node = node->parent())
            if (node == group) { inside = true; break; }
        if (!inside) break;
        if ((*it)->isHidden()) continue;
        const auto sprite = (*it)->data(0, Qt::UserRole).value<SpritePtr>();
        if (sprite && !sprite->path.isEmpty() && !paths.contains(sprite->path))
            paths.append(sprite->path);
    }
    return paths;
}
}

SpritePickerDialog::SpritePickerDialog(const ProjectSession* session, const Options& options,
                                       QWidget* parent)
    : QDialog(parent)
    , m_multiSelect(options.multiSelect && !options.pickGroups)
    , m_pickGroups(options.pickGroups)
    , m_allowEmpty(options.allowEmpty)
    , m_acceptText(options.acceptText.isEmpty() ? tr("OK") : options.acceptText)
{
    setWindowTitle(options.title.isEmpty() ? tr("Sprites") : options.title);
    resize(DIALOG_WIDTH, DIALOG_HEIGHT);

    auto* layout = new QVBoxLayout(this);

    if (!options.name.isEmpty()) {
        auto* nameRow = new QHBoxLayout;
        auto* nameLabel = new QLabel(tr("Name:"), this);
        m_nameEdit = new QLineEdit(options.name, this);
        m_nameEdit->setObjectName(QStringLiteral("spritePickerName"));
        m_nameEdit->selectAll();
        nameLabel->setBuddy(m_nameEdit);
        nameRow->addWidget(nameLabel);
        nameRow->addWidget(m_nameEdit, 1);
        layout->addLayout(nameRow);
        m_nameProblemOf = options.nameProblem;
        m_nameProblem = new QLabel(this);
        m_nameProblem->setObjectName(QStringLiteral("spritePickerNameProblem"));
        m_nameProblem->setStyleSheet(QStringLiteral("color: #d9534f;"));
        m_nameProblem->setWordWrap(true);
        m_nameProblem->hide();
        layout->addWidget(m_nameProblem);
        connect(m_nameEdit, &QLineEdit::textChanged, this, &SpritePickerDialog::updateAcceptButton);
    }

    m_navigatorPanel = new NavigatorPanel(this);
    m_navigatorPanel->configure({
        /* atlasCombo      */ false,
        /* showHidden      */ false,
        /* checkboxes      */ m_multiSelect,
        /* filterBar       */ true,
        /* addSourceButton */ false,
        /* selectionMode   */ m_pickGroups ? QAbstractItemView::ExtendedSelection
                                           : QAbstractItemView::SingleSelection
    });
    m_navigatorPanel->setSpriteFilter(options.filter);
    m_navigatorPanel->refresh(session, false);
    if (m_pickGroups) {
        // Only groups can be picked; sprites stay visible to show what each group holds.
        for (QTreeWidgetItemIterator it(m_navigatorPanel->tree()); *it; ++it)
            if ((*it)->data(0, Qt::UserRole).value<SpritePtr>())
                (*it)->setFlags((*it)->flags() & ~Qt::ItemIsSelectable);
    }

    auto* previewPanel = new QWidget(this);
    auto* previewLayout = new QVBoxLayout(previewPanel);
    m_previewImage = new QLabel(previewPanel);
    m_previewImage->setAlignment(Qt::AlignCenter);
    m_previewImage->setMinimumSize(PREVIEW_SIZE, PREVIEW_SIZE);
    m_previewInfo = new QLabel(previewPanel);
    m_previewInfo->setAlignment(Qt::AlignCenter);
    m_previewInfo->setWordWrap(true);
    previewLayout->addWidget(m_previewImage, 1);
    previewLayout->addWidget(m_previewInfo);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_navigatorPanel);
    splitter->addWidget(previewPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);

    auto* emptyLabel = new QLabel(options.emptyText.isEmpty() ? tr("There are no sprites to pick.")
                                                              : options.emptyText, this);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setWordWrap(true);

    m_contentStack = new QStackedWidget(this);
    m_contentStack->addWidget(splitter);
    m_contentStack->addWidget(emptyLabel);
    if (!m_navigatorPanel->firstSpriteItem())
        m_contentStack->setCurrentWidget(emptyLabel);
    layout->addWidget(m_contentStack, 1);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* tree = m_navigatorPanel->tree();
    connect(tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current) { updatePreview(current); });
    connect(tree, &QTreeWidget::itemSelectionChanged, this, &SpritePickerDialog::updateAcceptButton);
    connect(tree, &QTreeWidget::itemChanged, this, &SpritePickerDialog::updateAcceptButton);
    connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (m_pickGroups) return;  // double-click expands or collapses the group
        const auto sprite = item->data(0, Qt::UserRole).value<SpritePtr>();
        if (!sprite || sprite->path.isEmpty()) return;
        // Double-clicking picks this sprite alone, whatever else is ticked.
        m_doubleClickedPath = sprite->path;
        accept();
    });

    updatePreview(nullptr);
    updateAcceptButton();
    if (m_nameEdit) m_nameEdit->setFocus();
    else if (m_navigatorPanel->filterEdit()) m_navigatorPanel->filterEdit()->setFocus();
}

QString SpritePickerDialog::name() const
{
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

QStringList SpritePickerDialog::selectedSpritePaths() const
{
    if (!m_doubleClickedPath.isEmpty()) return {m_doubleClickedPath};
    if (m_multiSelect) return m_navigatorPanel->checkedPaths();
    return m_navigatorPanel->selectedSpritePaths();
}

QVector<SpritePickerDialog::Group> SpritePickerDialog::selectedGroups() const
{
    QVector<Group> groups;
    if (!m_pickGroups) return groups;
    auto* tree = m_navigatorPanel->tree();
    const QList<QTreeWidgetItem*> selectedList = tree->selectedItems();
    const QSet<QTreeWidgetItem*> selected(selectedList.begin(), selectedList.end());
    auto isUnderSelection = [&selected](QTreeWidgetItem* item) {
        for (QTreeWidgetItem* node = item; node; node = node->parent())
            if (selected.contains(node)) return true;
        return false;
    };
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        QTreeWidgetItem* item = *it;
        if (item->isHidden() || item->data(0, Qt::UserRole).value<SpritePtr>()) continue;
        if (!isUnderSelection(item)) continue;
        const QStringList sprites = directSpritePaths(item);
        if (!sprites.isEmpty()) groups.append({item->text(0), treePath(item), sprites});
    }
    return groups;
}

QVector<SpritePickerDialog::Group> SpritePickerDialog::pickGroups(QWidget* parent, const ProjectSession* session,
                                                                  Options options)
{
    options.pickGroups = true;
    SpritePickerDialog dialog(session, options, parent);
    if (dialog.exec() != QDialog::Accepted) return {};
    return dialog.selectedGroups();
}

QStringList SpritePickerDialog::pick(QWidget* parent, const ProjectSession* session, const Options& options)
{
    SpritePickerDialog dialog(session, options, parent);
    if (dialog.exec() != QDialog::Accepted) return {};
    return dialog.selectedSpritePaths();
}

void SpritePickerDialog::updatePreview(QTreeWidgetItem* current)
{
    const auto sprite = current ? current->data(0, Qt::UserRole).value<SpritePtr>() : nullptr;
    if (m_pickGroups && current && !sprite) {
        // A group: show its first sprite and how many it holds.
        const QStringList paths = spritePathsUnder(current);
        const QPixmap first = paths.isEmpty() ? QPixmap() : QPixmap(paths.first());
        if (first.isNull()) m_previewImage->clear();
        else m_previewImage->setPixmap(fitPreview(first));
        m_previewInfo->setText(tr("%1\n%n sprite(s)", nullptr, paths.size()).arg(current->text(0)));
        return;
    }
    const QPixmap pixmap = sprite ? QPixmap(sprite->path) : QPixmap();
    if (pixmap.isNull()) {
        m_previewImage->clear();
        m_previewInfo->setText(current && !sprite ? current->text(0) : QString());
        return;
    }
    m_previewImage->setPixmap(fitPreview(pixmap));
    const QString name = sprite->name.isEmpty() ? QFileInfo(sprite->path).completeBaseName() : sprite->name;
    m_previewInfo->setText(tr("%1\n%2 × %3 px").arg(name).arg(pixmap.width()).arg(pixmap.height()));
}

void SpritePickerDialog::updateAcceptButton()
{
    const int count = m_pickGroups ? selectedGroups().size() : selectedSpritePaths().size();
    QPushButton* acceptButton = m_buttons->button(QDialogButtonBox::Ok);
    bool named = true;
    if (m_nameEdit) {
        const QString problem = name().isEmpty() ? tr("Type a name.")
                              : m_nameProblemOf ? m_nameProblemOf(name()) : QString();
        m_nameProblem->setText(problem);
        m_nameProblem->setVisible(!problem.isEmpty() && !name().isEmpty());
        named = problem.isEmpty();
    }
    acceptButton->setEnabled((count > 0 || m_allowEmpty) && named);
    const bool showCount = (m_multiSelect || m_pickGroups) && count > 0;
    acceptButton->setText(showCount ? tr("%1 (%2)").arg(m_acceptText).arg(count) : m_acceptText);
}
