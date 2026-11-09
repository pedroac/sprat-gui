#include "NineSliceWorkspace.h"
#include "AppIcons.h"
#include "UiStyle.h"
#include "NavigatorPanel.h"
#include "NavigatorTreeWidget.h"
#include "NineSliceEditorCanvas.h"
#include "ProjectSession.h"
#include "SpritePickerDialog.h"
#include "SpriteModels.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace {
/// Largest inset the spin boxes accept before a sprite image sets the real limit.
constexpr int MAX_INSET = 9999;

/// Splits a sprite into thirds the first time it is nine-sliced, when it has no insets yet.
void applyDefaultInsets(Sprite& sprite)
{
    if (sprite.nsLeft != 0 || sprite.nsTop != 0 || sprite.nsRight != 0 || sprite.nsBottom != 0)
        return;
    const QPixmap pix(sprite.path);
    if (pix.isNull()) return;
    sprite.nsLeft   = pix.width()  / 3;
    sprite.nsTop    = pix.height() / 3;
    sprite.nsRight  = pix.width()  / 3;
    sprite.nsBottom = pix.height() / 3;
    // Clear any stale target size written by saveCurrentSprite() before the sprite was
    // nine-sliced (e.g. the spinbox minimum of 1 px), so that setTargetSize is not called
    // with a value unrelated to this sprite.
    sprite.nsTargetWidth  = 0;
    sprite.nsTargetHeight = 0;
}
}

NineSliceWorkspace::NineSliceWorkspace(ProjectSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
{
    setupUi();
}

void NineSliceWorkspace::setupUi()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    mainLayout->addWidget(splitter);

    // === Left panel: the nine-sliced sprites, with Add/Remove under them as in the Atlases workspace ===
    auto* leftWidget = new QWidget;
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    m_navigatorPanel = new NavigatorPanel(this);
    m_navigatorPanel->configure({
        /* atlasCombo      */ false,
        /* showHidden      */ false,
        /* checkboxes      */ false,
        /* filterBar       */ true,
        /* addSourceButton */ false,
        /* selectionMode   */ QAbstractItemView::ExtendedSelection
    });
    m_navigatorPanel->setSpriteFilter([](const SpritePtr& sp) { return sp->isNineSliced; });

    m_emptyState = new QWidget(this);
    {
        auto* emptyLayout = new QVBoxLayout(m_emptyState);
        auto* emptyLabel = new QLabel(tr("No nine-sliced sprites yet.\nClick Add to pick the sprites "
                                         "that should stretch, such as panels and buttons."), m_emptyState);
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setWordWrap(true);
        emptyLayout->addWidget(emptyLabel);
    }
    m_emptyState->setObjectName(QStringLiteral("nineSliceEmptyState"));
    m_emptyState->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_emptyState, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.addAction(m_addBtn->icon(), tr("Add..."), this, &NineSliceWorkspace::onAddSpritesClicked);
        menu.exec(m_emptyState->mapToGlobal(pos));
    });
    m_navigatorStack = new QStackedWidget(this);
    m_navigatorStack->setObjectName(QStringLiteral("nineSliceNavigatorStack"));
    m_navigatorStack->addWidget(m_navigatorPanel);
    m_navigatorStack->addWidget(m_emptyState);
    leftLayout->addWidget(m_navigatorStack, 1);

    m_addBtn = new QPushButton(AppIcons::icon(QStringLiteral("add")),
                               tr("Add"), leftWidget);
    m_addBtn->setObjectName(QStringLiteral("nineSliceAddButton"));
    m_addBtn->setToolTip(tr("Pick the sprites that stretch; they start split in thirds"));
    UiStyle::setRole(m_addBtn, UiStyle::Role::Add);
    m_addBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_removeBtn = new QPushButton(AppIcons::icon(QStringLiteral("exclude")),
                                  tr("Remove"), leftWidget);
    m_removeBtn->setObjectName(QStringLiteral("nineSliceRemoveButton"));
    m_removeBtn->setToolTip(tr("Stop nine-slicing the selected sprites (Delete). Their insets are kept "
                               "if you add them again"));
    m_removeBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_removeBtn->setEnabled(false);
    UiStyle::setRole(m_removeBtn, UiStyle::Role::Destructive);
    {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(4, 2, 4, 2);
        row->addWidget(m_addBtn);
        row->addWidget(m_removeBtn);
        leftLayout->addLayout(row);
    }
    splitter->addWidget(leftWidget);

    // === Right panel ===
    auto* rightPanel = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(4, 4, 4, 4);

    // -- Top row: Zoom + W/H + Lines/Grid/Colorize
    {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 2);

        // Magnifying glass icon (matches SpriteEditorPanel style)
        {
            QPixmap pix(16, 16);
            pix.fill(Qt::transparent);
            QPainter painter(&pix);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(palette().color(QPalette::WindowText), 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QPointF(6, 6), 4.5, 4.5);
            painter.drawLine(QPointF(9.2, 9.2), QPointF(14, 14));
            painter.end();
            auto* zoomLabel = new QLabel(this);
            zoomLabel->setPixmap(pix);
            zoomLabel->setToolTip(tr("Zoom"));
            row->addWidget(zoomLabel);
        }

        m_zoomSpin = new QDoubleSpinBox(this);
        m_zoomSpin->setRange(10.0, 1600.0);
        m_zoomSpin->setValue(100.0);
        m_zoomSpin->setSuffix(QStringLiteral("%"));
        m_zoomSpin->setSingleStep(10.0);
        row->addWidget(m_zoomSpin);

        m_widthSpin = new QSpinBox(this);
        m_widthSpin->setObjectName(QStringLiteral("nineSliceWidth"));
        m_widthSpin->setRange(1, 4096);
        m_widthSpin->setSuffix(QStringLiteral(" px"));
        row->addWidget(new QLabel(tr("W:")));
        row->addWidget(m_widthSpin);

        m_heightSpin = new QSpinBox(this);
        m_heightSpin->setObjectName(QStringLiteral("nineSliceHeight"));
        m_heightSpin->setRange(1, 4096);
        m_heightSpin->setSuffix(QStringLiteral(" px"));
        row->addWidget(new QLabel(tr("H:")));
        row->addWidget(m_heightSpin);

        m_linesCheck = new QPushButton(AppIcons::icon(QStringLiteral("slice-lines")), tr("Lines"), this);
        m_linesCheck->setCheckable(true);
        m_linesCheck->setChecked(true);
        m_linesCheck->setToolTip(tr("Show or hide the nine-slice guide lines"));
        row->addWidget(m_linesCheck);

        m_gridCheck = new QPushButton(AppIcons::icon(QStringLiteral("grid")), tr("Grid"), this);
        m_gridCheck->setCheckable(true);
        m_gridCheck->setToolTip(tr("Show a pixel grid overlay on the canvas"));
        row->addWidget(m_gridCheck);

        m_colorizeCheck = new QPushButton(AppIcons::icon(QStringLiteral("colorize")), tr("Colorize"), this);
        m_colorizeCheck->setCheckable(true);
        m_colorizeCheck->setChecked(true);
        m_colorizeCheck->setToolTip(tr("Show colored region hints (corners, edges, center)"));
        row->addWidget(m_colorizeCheck);

        row->addStretch();
        rightLayout->addLayout(row);
    }

    // -- Configuration group: slice options only (disabled when sprite is not nine-sliced) --
    m_configGroup = new QWidget;
    auto* form = new QFormLayout(m_configGroup);
    form->setLabelAlignment(Qt::AlignRight);

    auto makeSpin = [&]() -> QSpinBox* {
        auto* spin = new QSpinBox;
        spin->setRange(0, MAX_INSET);
        spin->setSuffix(QStringLiteral(" px"));
        return spin;
    };
    m_leftSpin   = makeSpin();
    m_topSpin    = makeSpin();
    m_rightSpin  = makeSpin();
    m_bottomSpin = makeSpin();
    m_leftSpin->setObjectName(QStringLiteral("nineSliceLeftInset"));
    m_topSpin->setObjectName(QStringLiteral("nineSliceTopInset"));
    m_rightSpin->setObjectName(QStringLiteral("nineSliceRightInset"));
    m_bottomSpin->setObjectName(QStringLiteral("nineSliceBottomInset"));
    m_hModeCombo = new QComboBox;
    m_hModeCombo->addItem(AppIcons::icon(QStringLiteral("stretch-h")), QStringLiteral("stretch"));
    m_hModeCombo->addItem(AppIcons::icon(QStringLiteral("repeat-h")),  QStringLiteral("repeat"));
    m_hModeCombo->addItem(AppIcons::icon(QStringLiteral("mirror-h")),  QStringLiteral("mirror"));
    m_hModeCombo->addItem(AppIcons::icon(QStringLiteral("fixed-h")),   QStringLiteral("fixed"));
    m_vModeCombo = new QComboBox;
    m_vModeCombo->addItem(AppIcons::icon(QStringLiteral("stretch-v")), QStringLiteral("stretch"));
    m_vModeCombo->addItem(AppIcons::icon(QStringLiteral("repeat-v")),  QStringLiteral("repeat"));
    m_vModeCombo->addItem(AppIcons::icon(QStringLiteral("mirror-v")),  QStringLiteral("mirror"));
    m_vModeCombo->addItem(AppIcons::icon(QStringLiteral("fixed-v")),   QStringLiteral("fixed"));
    {
        auto* sep = new QFrame;
        sep->setFrameShape(QFrame::VLine);
        sep->setFrameShadow(QFrame::Sunken);

        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("L:"))); row->addWidget(m_leftSpin);
        row->addWidget(new QLabel(tr("T:"))); row->addWidget(m_topSpin);
        row->addWidget(new QLabel(tr("R:"))); row->addWidget(m_rightSpin);
        row->addWidget(new QLabel(tr("B:"))); row->addWidget(m_bottomSpin);
        row->addWidget(sep);
        row->addWidget(new QLabel(tr("H:"))); row->addWidget(m_hModeCombo);
        row->addWidget(new QLabel(tr("V:"))); row->addWidget(m_vModeCombo);
        row->addStretch();
        form->addRow(row);
    }

    rightLayout->addWidget(m_configGroup);

    // -- Slice editor canvas --
    m_canvas = new NineSliceEditorCanvas;
    m_canvas->setMinimumHeight(200);
    rightLayout->addWidget(m_canvas, 1);

    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({220, 1});

    // === Connections ===
    connect(m_navigatorPanel->tree(), &QTreeWidget::currentItemChanged,
            this, &NineSliceWorkspace::onNavigatorSelectionChanged);
    connect(m_navigatorPanel->tree(), &QTreeWidget::itemSelectionChanged, this, [this]() {
        m_removeBtn->setEnabled(!m_navigatorPanel->selectedSpritePaths().isEmpty());
    });
    connect(m_navigatorPanel, &NavigatorPanel::excludeKeyPressed,
            this, &NineSliceWorkspace::onRemoveSpritesClicked);
    connect(m_addBtn, &QPushButton::clicked, this, &NineSliceWorkspace::onAddSpritesClicked);
    connect(m_removeBtn, &QPushButton::clicked, this, &NineSliceWorkspace::onRemoveSpritesClicked);

    connect(m_leftSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &NineSliceWorkspace::onInsetSpinChanged);
    connect(m_topSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &NineSliceWorkspace::onInsetSpinChanged);
    connect(m_rightSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &NineSliceWorkspace::onInsetSpinChanged);
    connect(m_bottomSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &NineSliceWorkspace::onInsetSpinChanged);
    connect(m_hModeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &NineSliceWorkspace::onFillModeChanged);
    connect(m_vModeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &NineSliceWorkspace::onFillModeChanged);
    connect(m_canvas, &NineSliceEditorCanvas::insetsChanged,
            this, &NineSliceWorkspace::onCanvasInsetsChanged);
    connect(m_canvas, &NineSliceEditorCanvas::zoomChanged,
            this, &NineSliceWorkspace::onCanvasZoomChanged);
    connect(m_zoomSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setZoomManual(true); m_canvas->setZoom(v / 100.0); });
    connect(m_widthSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v) { m_canvas->setTargetSize(v, m_heightSpin->value()); });
    connect(m_heightSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v) { m_canvas->setTargetSize(m_widthSpin->value(), v); });
    connect(m_canvas, &NineSliceEditorCanvas::targetSizeChanged,
            this, &NineSliceWorkspace::onCanvasTargetSizeChanged);
    connect(m_gridCheck, &QPushButton::toggled, this, [this](bool checked) {
        m_settings.showGrid = checked;
        m_canvas->setSettings(m_settings);
    });
    connect(m_linesCheck, &QPushButton::toggled,
            m_canvas, &NineSliceEditorCanvas::setSliceLinesVisible);
    connect(m_colorizeCheck, &QPushButton::toggled,
            m_canvas, &NineSliceEditorCanvas::setOverlayVisible);

    // Navigator context menu
    m_navigatorPanel->tree()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_navigatorPanel->tree(), &QTreeWidget::customContextMenuRequested,
            this, &NineSliceWorkspace::onNavigatorContextMenuRequested);
}

void NineSliceWorkspace::enter()
{
    // refresh() rebuilds the tree with its signals blocked, so the current path survives it.
    const QString savedPath = m_currentSpritePath;
    refreshNavigator();
    showSprite(savedPath);
}

void NineSliceWorkspace::refreshSprites()
{
    const QString currentPath = m_currentSpritePath;
    refreshNavigator();
    if (auto* item = m_navigatorPanel->findSpriteItem(currentPath)) {
        const QSignalBlocker blocker(m_navigatorPanel->tree());
        m_navigatorPanel->tree()->setCurrentItem(item);
        m_removeBtn->setEnabled(true);
    } else {
        showSprite(currentPath);
    }
}

void NineSliceWorkspace::leave()
{
    saveCurrentSprite();
}

void NineSliceWorkspace::setSettings(const AppSettings& settings)
{
    m_settings = settings;
    {
        QSignalBlocker b(m_gridCheck);
        m_gridCheck->setChecked(settings.showGrid);
    }
    m_canvas->setSettings(m_settings);
}

// === Private helpers ===

void NineSliceWorkspace::loadSprite(const QString& path)
{
    if (!m_session) return;
    auto sp = m_session->spriteIndex.value(QDir::cleanPath(path));
    if (!sp) {
        m_canvas->clearSprite();
        return;
    }
    if (sp->isNineSliced) applyDefaultInsets(*sp);

    const double prevZoom = m_zoomSpin->value() / 100.0;
    bool insetsFitted = false;
    m_updatingUi = true;

    resetInsetLimits(); // the previous sprite's caps must not clip this one's insets
    m_leftSpin->setValue(sp->nsLeft);
    m_topSpin->setValue(sp->nsTop);
    m_rightSpin->setValue(sp->nsRight);
    m_bottomSpin->setValue(sp->nsBottom);
    {
        int hIdx = m_hModeCombo->findText(sp->nsHMode);
        m_hModeCombo->setCurrentIndex(hIdx >= 0 ? hIdx : 0);
        int vIdx = m_vModeCombo->findText(sp->nsVMode);
        m_vModeCombo->setCurrentIndex(vIdx >= 0 ? vIdx : 0);
    }

    m_configGroup->setEnabled(sp->isNineSliced);
    if (sp->isNineSliced) {
        m_canvas->setReadOnly(false);
        m_canvas->setSpriteImage(path);
        m_canvas->setInsets(sp->nsLeft, sp->nsTop, sp->nsRight, sp->nsBottom);
        // Projects saved before the editor kept insets fitted may hold ones that
        // don't fit the image; keep the canvas's fitted insets everywhere.
        insetsFitted = sp->nsLeft != m_canvas->leftInset() || sp->nsTop != m_canvas->topInset()
            || sp->nsRight != m_canvas->rightInset() || sp->nsBottom != m_canvas->bottomInset();
        sp->nsLeft   = m_canvas->leftInset();
        sp->nsTop    = m_canvas->topInset();
        sp->nsRight  = m_canvas->rightInset();
        sp->nsBottom = m_canvas->bottomInset();
        m_leftSpin->setValue(sp->nsLeft);
        m_topSpin->setValue(sp->nsTop);
        m_rightSpin->setValue(sp->nsRight);
        m_bottomSpin->setValue(sp->nsBottom);
        m_canvas->setFillModes(sp->nsHMode, sp->nsVMode);
        m_canvas->setSliceLinesVisible(m_linesCheck->isChecked());
        if (sp->nsTargetWidth > 0 && sp->nsTargetHeight > 0)
            m_canvas->setTargetSize(sp->nsTargetWidth, sp->nsTargetHeight);
        updateFixedDimensions();
        updateInsetLimits();
    } else {
        // Show the sprite read-only (no lines/handles, dim overlay, forbidden cursor)
        m_canvas->clearSprite();  // clears m_currentPath so setSpriteImage always reloads
        m_canvas->setSpriteImage(path);
        m_canvas->setSliceLinesVisible(false);
        m_canvas->setResizableAxes(false, false);
        m_canvas->setReadOnly(true);
    }

    m_updatingUi = false;

    switch (m_settings.nineSliceZoomOnChange) {
        case NineSliceZoomOnChange::FitToFrame: m_canvas->initialFit();      break;
        case NineSliceZoomOnChange::Reset100:   m_canvas->setZoom(1.0);      break;
        case NineSliceZoomOnChange::NoChange:   m_canvas->setZoom(prevZoom); break;
    }
    if (insetsFitted) emit definitionsChanged();
}

void NineSliceWorkspace::saveCurrentSprite()
{
    if (m_updatingUi || m_currentSpritePath.isEmpty() || !m_session) return;
    auto sp = m_session->spriteIndex.value(QDir::cleanPath(m_currentSpritePath));
    if (!sp) return;
    sp->nsLeft         = m_leftSpin->value();
    sp->nsTop          = m_topSpin->value();
    sp->nsRight        = m_rightSpin->value();
    sp->nsBottom       = m_bottomSpin->value();
    sp->nsHMode        = m_hModeCombo->currentText();
    sp->nsVMode        = m_vModeCombo->currentText();
    sp->nsTargetWidth  = m_widthSpin->value();
    sp->nsTargetHeight = m_heightSpin->value();
}

void NineSliceWorkspace::refreshNavigator()
{
    if (m_session) m_navigatorPanel->refresh(m_session, false);
    const bool hasSprites = m_navigatorPanel->firstSpriteItem() != nullptr;
    m_navigatorStack->setCurrentWidget(hasSprites ? static_cast<QWidget*>(m_navigatorPanel) : m_emptyState);
    m_removeBtn->setEnabled(!m_navigatorPanel->selectedSpritePaths().isEmpty());
}

void NineSliceWorkspace::showSprite(const QString& path)
{
    QTreeWidgetItem* item = m_navigatorPanel->findSpriteItem(path);
    if (!item) item = m_navigatorPanel->firstSpriteItem();
    const auto sprite = item ? item->data(0, Qt::UserRole).value<SpritePtr>() : nullptr;
    if (!sprite) {
        clearSpriteView();
        return;
    }
    auto* tree = m_navigatorPanel->tree();
    {
        const QSignalBlocker blocker(tree);
        tree->setCurrentItem(item);
    }
    m_removeBtn->setEnabled(true);
    m_currentSpritePath = sprite->path;
    m_canvas->setEmptyText(QString());
    loadSprite(sprite->path);
    updateFixedDimensions();
}

void NineSliceWorkspace::clearSpriteView()
{
    m_currentSpritePath.clear();
    m_configGroup->setEnabled(false);
    // Nothing to size without a sprite.
    m_widthSpin->setEnabled(false);
    m_heightSpin->setEnabled(false);
    m_canvas->setReadOnly(false);
    m_canvas->clearSprite();
    m_canvas->setEmptyText(m_navigatorPanel->firstSpriteItem() ? tr("Select a sprite on the left")
                                                               : tr("Add sprites that stretch, such as panels and buttons"));
}

void NineSliceWorkspace::setNineSliced(const QStringList& paths, bool nineSliced, const QString& nextPath)
{
    if (!m_session || paths.isEmpty()) return;
    saveCurrentSprite();
    for (const QString& path : paths) {
        const auto sp = m_session->spriteIndex.value(QDir::cleanPath(path));
        if (!sp) continue;
        sp->isNineSliced = nineSliced;
        // Every added sprite gets insets now, not only the one shown, so it exports sliced.
        if (nineSliced) applyDefaultInsets(*sp);
    }
    refreshNavigator();
    showSprite(nextPath);
    emit definitionsChanged();
}

QString NineSliceWorkspace::spriteAfterRemoving(const QStringList& removedPaths) const
{
    if (!m_currentSpritePath.isEmpty() && !removedPaths.contains(m_currentSpritePath))
        return m_currentSpritePath;
    QStringList listed;
    QTreeWidgetItemIterator it(m_navigatorPanel->tree());
    while (*it) {
        const auto sp = (*it)->data(0, Qt::UserRole).value<SpritePtr>();
        if (sp && !sp->path.isEmpty()) listed.append(sp->path);
        ++it;
    }
    // The next sprite below the removed ones, else the nearest one above them.
    const int anchor = listed.indexOf(m_currentSpritePath);
    for (int i = anchor + 1; anchor >= 0 && i < listed.size(); ++i) {
        if (!removedPaths.contains(listed[i])) return listed[i];
    }
    for (int i = (anchor >= 0 ? anchor : listed.size()) - 1; i >= 0; --i) {
        if (!removedPaths.contains(listed[i])) return listed[i];
    }
    return {};
}

// === Slots ===

void NineSliceWorkspace::onNavigatorSelectionChanged(QTreeWidgetItem* current, QTreeWidgetItem*)
{
    saveCurrentSprite();
    auto sprite = current ? current->data(0, Qt::UserRole).value<SpritePtr>() : nullptr;
    if (sprite && !sprite->path.isEmpty()) {
        m_currentSpritePath = sprite->path;
        loadSprite(sprite->path);
    } else {
        clearSpriteView();
    }
}

void NineSliceWorkspace::onAddSpritesClicked()
{
    if (!m_session) return;
    SpritePickerDialog::Options options;
    options.title = tr("Add Nine-Slice Sprites");
    options.acceptText = tr("Add");
    options.emptyText = tr("Every sprite is already nine-sliced.");
    options.filter = [](const SpritePtr& sp) { return !sp->isNineSliced; };
    addSprites(SpritePickerDialog::pick(this, m_session, options));
}

void NineSliceWorkspace::onRemoveSpritesClicked()
{
    removeSprites(m_navigatorPanel->selectedSpritePaths());
}

void NineSliceWorkspace::addSprites(const QStringList& paths)
{
    if (paths.isEmpty()) return;
    setNineSliced(paths, true, paths.first());
}

void NineSliceWorkspace::removeSprites(const QStringList& paths)
{
    if (paths.isEmpty()) return;
    setNineSliced(paths, false, spriteAfterRemoving(paths));
}

void NineSliceWorkspace::onInsetSpinChanged()
{
    if (m_updatingUi) return;
    m_canvas->setInsets(m_leftSpin->value(), m_topSpin->value(),
                        m_rightSpin->value(), m_bottomSpin->value());
    updateInsetLimits();
    saveCurrentSprite();
    emit definitionsChanged();
}

void NineSliceWorkspace::onCanvasInsetsChanged(int left, int top, int right, int bottom)
{
    if (m_updatingUi) return;
    m_updatingUi = true;
    // The canvas keeps its insets fitted; lift the old caps so none is clipped.
    resetInsetLimits();
    m_leftSpin->setValue(left);
    m_topSpin->setValue(top);
    m_rightSpin->setValue(right);
    m_bottomSpin->setValue(bottom);
    updateInsetLimits();
    m_updatingUi = false;
    saveCurrentSprite();
    emit definitionsChanged();
}

void NineSliceWorkspace::resetInsetLimits()
{
    const QSignalBlocker blockLeft(m_leftSpin), blockTop(m_topSpin),
                         blockRight(m_rightSpin), blockBottom(m_bottomSpin);
    for (QSpinBox* spin : {m_leftSpin, m_topSpin, m_rightSpin, m_bottomSpin})
        spin->setMaximum(MAX_INSET);
}

void NineSliceWorkspace::updateInsetLimits()
{
    const QSize source = m_canvas->sourceSize();
    if (!source.isValid()) {
        resetInsetLimits();
        return;
    }
    const QSignalBlocker blockLeft(m_leftSpin), blockTop(m_topSpin),
                         blockRight(m_rightSpin), blockBottom(m_bottomSpin);
    // Left and top first: when stale values overflow, right and bottom give up the
    // excess, as in fitNineSliceInsets().
    m_leftSpin->setMaximum(qMax(0, source.width() - m_rightSpin->value()));
    m_rightSpin->setMaximum(qMax(0, source.width() - m_leftSpin->value()));
    m_topSpin->setMaximum(qMax(0, source.height() - m_bottomSpin->value()));
    m_bottomSpin->setMaximum(qMax(0, source.height() - m_topSpin->value()));
}

void NineSliceWorkspace::onFillModeChanged()
{
    if (m_updatingUi) return;
    m_canvas->setFillModes(m_hModeCombo->currentText(), m_vModeCombo->currentText());
    updateFixedDimensions();
    saveCurrentSprite();
    emit definitionsChanged();
}

void NineSliceWorkspace::updateFixedDimensions()
{
    const bool hFixed = (m_hModeCombo->currentText() == QLatin1String("fixed"));
    const bool vFixed = (m_vModeCombo->currentText() == QLatin1String("fixed"));
    const QSize src = m_canvas->sourceSize();

    {
        QSignalBlocker bw(m_widthSpin), bh(m_heightSpin);
        if (hFixed && src.isValid()) m_widthSpin->setValue(src.width());
        if (vFixed && src.isValid()) m_heightSpin->setValue(src.height());
    }
    m_widthSpin->setEnabled(!hFixed);
    m_heightSpin->setEnabled(!vFixed);

    if ((hFixed || vFixed) && src.isValid())
        m_canvas->setTargetSize(m_widthSpin->value(), m_heightSpin->value());

    m_canvas->setResizableAxes(!hFixed, !vFixed);
}

void NineSliceWorkspace::onCanvasZoomChanged(double zoom)
{
    QSignalBlocker blocker(m_zoomSpin);
    m_zoomSpin->setValue(zoom * 100.0);
}

void NineSliceWorkspace::onCanvasTargetSizeChanged(int w, int h)
{
    QSignalBlocker bw(m_widthSpin), bh(m_heightSpin);
    m_widthSpin->setValue(w);
    m_heightSpin->setValue(h);
}

void NineSliceWorkspace::onNavigatorContextMenuRequested(const QPoint& pos)
{
    if (!m_session) return;
    auto* tree = m_navigatorPanel->tree();
    // Right-clicking outside the selection acts on the clicked item, as file managers do.
    if (auto* item = tree->itemAt(pos); item && !item->isSelected()) {
        tree->setCurrentItem(item);
    }

    QMenu menu(this);
    menu.addAction(m_addBtn->icon(), tr("Add..."), this, &NineSliceWorkspace::onAddSpritesClicked);
    const int selectedCount = m_navigatorPanel->selectedSpritePaths().size();
    if (selectedCount > 0) {
        menu.addAction(m_removeBtn->icon(), tr("Remove Nine-Slice (%n sprite(s))", nullptr, selectedCount),
                       this, &NineSliceWorkspace::onRemoveSpritesClicked);
    }
    menu.exec(tree->mapToGlobal(pos));
}
