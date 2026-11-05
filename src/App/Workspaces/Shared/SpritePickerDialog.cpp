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
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeWidgetItem>
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
}

SpritePickerDialog::SpritePickerDialog(const ProjectSession* session, const Options& options,
                                       QWidget* parent)
    : QDialog(parent)
    , m_multiSelect(options.multiSelect)
    , m_acceptText(options.acceptText.isEmpty() ? tr("OK") : options.acceptText)
{
    setWindowTitle(options.title.isEmpty() ? tr("Sprites") : options.title);
    resize(DIALOG_WIDTH, DIALOG_HEIGHT);

    auto* layout = new QVBoxLayout(this);

    m_navigatorPanel = new NavigatorPanel(this);
    m_navigatorPanel->configure({
        /* atlasCombo      */ false,
        /* showHidden      */ false,
        /* checkboxes      */ options.multiSelect,
        /* filterBar       */ true,
        /* addSourceButton */ false,
        /* selectionMode   */ QAbstractItemView::SingleSelection
    });
    m_navigatorPanel->setSpriteFilter(options.filter);
    m_navigatorPanel->refresh(session, false);

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
        const auto sprite = item->data(0, Qt::UserRole).value<SpritePtr>();
        if (!sprite || sprite->path.isEmpty()) return;
        // Double-clicking picks this sprite alone, whatever else is ticked.
        m_doubleClickedPath = sprite->path;
        accept();
    });

    updatePreview(nullptr);
    updateAcceptButton();
    if (m_navigatorPanel->filterEdit()) m_navigatorPanel->filterEdit()->setFocus();
}

QStringList SpritePickerDialog::selectedSpritePaths() const
{
    if (!m_doubleClickedPath.isEmpty()) return {m_doubleClickedPath};
    if (m_multiSelect) return m_navigatorPanel->checkedPaths();
    return m_navigatorPanel->selectedSpritePaths();
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
    const int count = selectedSpritePaths().size();
    QPushButton* acceptButton = m_buttons->button(QDialogButtonBox::Ok);
    acceptButton->setEnabled(count > 0);
    acceptButton->setText(m_multiSelect && count > 0 ? tr("%1 (%2)").arg(m_acceptText).arg(count)
                                                     : m_acceptText);
}
