#include "NineSliceWorkspace.h"
#include "NavigatorPanel.h"
#include "NavigatorTreeWidget.h"
#include "NineSliceEditorCanvas.h"
#include "ProjectSession.h"
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
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// filterItemNineSliced — recursively hide tree items whose sprite is not nine-sliced
// Returns true if the item (or any descendant) is visible after filtering.
// ---------------------------------------------------------------------------
static bool filterItemNineSliced(QTreeWidgetItem* item)
{
    auto sp = item->data(0, Qt::UserRole).value<SpritePtr>();
    if (sp && !sp->path.isEmpty()) {
        // Leaf (sprite) item
        item->setHidden(!sp->isNineSliced);
        return sp->isNineSliced;
    }
    // Folder item: show only if at least one child is visible
    bool anyVisible = false;
    for (int i = 0; i < item->childCount(); ++i) {
        if (filterItemNineSliced(item->child(i)))
            anyVisible = true;
    }
    item->setHidden(!anyVisible);
    return anyVisible;
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

    // === Left panel: filter toggle + NavigatorPanel ===
    auto* leftWidget = new QWidget;
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    m_filterNineSlicedBtn = new QPushButton(QIcon(QStringLiteral(":/icons/filter.svg")), tr("Nine-sliced only"), this);
    m_filterNineSlicedBtn->setCheckable(true);
    m_filterNineSlicedBtn->setToolTip(tr("Show only sprites marked as nine-sliced"));
    {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(4, 2, 4, 2);
        row->addWidget(m_filterNineSlicedBtn);
        row->addStretch();
        leftLayout->addLayout(row);
    }

    m_navigatorPanel = new NavigatorPanel(this);
    m_navigatorPanel->configure({
        /* atlasCombo      */ false,
        /* showHidden      */ false,
        /* checkboxes      */ false,
        /* filterBar       */ true,
        /* addSourceButton */ false,
        /* selectionMode   */ QAbstractItemView::SingleSelection
    });
    m_navigatorPanel->setSpriteBadgeCallback([](const SpritePtr& sp) -> QIcon {
        if (sp && sp->isNineSliced)
            return QIcon(QStringLiteral(":/icons/nine-slice.svg"));
        return {};
    });
    leftLayout->addWidget(m_navigatorPanel, 1);
    splitter->addWidget(leftWidget);

    // === Right panel ===
    auto* rightPanel = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(4, 4, 4, 4);

    // -- Nine-Sliced toggle --
    {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 2);
        m_nineSlicedBtn = new QPushButton(QIcon(QStringLiteral(":/icons/nine-slice.svg")), tr("Nine-Sliced"), this);
        m_nineSlicedBtn->setCheckable(true);
        m_nineSlicedBtn->setEnabled(false);
        m_nineSlicedBtn->setToolTip(tr("Mark or unmark the selected sprite as nine-sliced"));
        row->addWidget(m_nineSlicedBtn);
        row->addStretch();
        rightLayout->addLayout(row);
    }

    // -- Configuration group --
    m_configGroup = new QWidget;
    auto* form = new QFormLayout(m_configGroup);
    form->setLabelAlignment(Qt::AlignRight);

    auto makeSpin = [&]() -> QSpinBox* {
        auto* spin = new QSpinBox;
        spin->setRange(0, 9999);
        spin->setSuffix(QStringLiteral(" px"));
        return spin;
    };
    m_leftSpin   = makeSpin();
    m_topSpin    = makeSpin();
    m_rightSpin  = makeSpin();
    m_bottomSpin = makeSpin();
    m_hModeCombo = new QComboBox;
    m_hModeCombo->addItem(QIcon(QStringLiteral(":/icons/stretch-h.svg")), QStringLiteral("stretch"));
    m_hModeCombo->addItem(QIcon(QStringLiteral(":/icons/repeat-h.svg")),  QStringLiteral("repeat"));
    m_hModeCombo->addItem(QIcon(QStringLiteral(":/icons/mirror-h.svg")),  QStringLiteral("mirror"));
    m_vModeCombo = new QComboBox;
    m_vModeCombo->addItem(QIcon(QStringLiteral(":/icons/stretch-v.svg")), QStringLiteral("stretch"));
    m_vModeCombo->addItem(QIcon(QStringLiteral(":/icons/repeat-v.svg")),  QStringLiteral("repeat"));
    m_vModeCombo->addItem(QIcon(QStringLiteral(":/icons/mirror-v.svg")),  QStringLiteral("mirror"));
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

    {
        auto* row = new QHBoxLayout;

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
        m_widthSpin->setRange(1, 4096);
        m_widthSpin->setSuffix(QStringLiteral(" px"));
        row->addWidget(new QLabel(tr("W:")));
        row->addWidget(m_widthSpin);

        m_heightSpin = new QSpinBox(this);
        m_heightSpin->setRange(1, 4096);
        m_heightSpin->setSuffix(QStringLiteral(" px"));
        row->addWidget(new QLabel(tr("H:")));
        row->addWidget(m_heightSpin);

        m_gridCheck = new QPushButton(QIcon(QStringLiteral(":/icons/grid.svg")), tr("Grid"), this);
        m_gridCheck->setCheckable(true);
        m_gridCheck->setToolTip(tr("Show a pixel grid overlay on the canvas"));
        row->addWidget(m_gridCheck);

        m_colorizeCheck = new QPushButton(QIcon(QStringLiteral(":/icons/colorize.svg")), tr("Colorize"), this);
        m_colorizeCheck->setCheckable(true);
        m_colorizeCheck->setChecked(true);
        m_colorizeCheck->setToolTip(tr("Show colored region hints (corners, edges, center)"));
        row->addWidget(m_colorizeCheck);

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
    connect(m_filterNineSlicedBtn, &QPushButton::toggled,
            this, &NineSliceWorkspace::applyNineSliceFilter);
    connect(m_nineSlicedBtn, &QPushButton::toggled,
            this, &NineSliceWorkspace::onNineSlicedToggled);

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
            this, [this](double v) { m_canvas->setZoom(v / 100.0); });
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
    connect(m_colorizeCheck, &QPushButton::toggled,
            m_canvas, &NineSliceEditorCanvas::setOverlayVisible);

    // Navigator context menu
    m_navigatorPanel->tree()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_navigatorPanel->tree(), &QTreeWidget::customContextMenuRequested,
            this, &NineSliceWorkspace::onNavigatorContextMenuRequested);
}

void NineSliceWorkspace::enter()
{
    // Save path before refresh(): if refresh emits currentItemChanged(null) it
    // will clear m_currentSpritePath via onNavigatorSelectionChanged.  After
    // refresh we use the post-refresh value as the source of truth and only
    // fall back to the saved path when refresh left no selection.
    const QString savedPath = m_currentSpritePath;
    if (m_session)
        m_navigatorPanel->refresh(m_session, false);
    if (m_currentSpritePath.isEmpty() && !savedPath.isEmpty())
        m_currentSpritePath = savedPath;

    // If still no path, auto-select the first sprite in the tree.
    // refresh() blocks signals so currentItemChanged never fires during rebuild.
    if (m_currentSpritePath.isEmpty()) {
        auto* tree = m_navigatorPanel->tree();
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            auto sp = (*it)->data(0, Qt::UserRole).value<SpritePtr>();
            if (sp && !sp->path.isEmpty()) {
                m_currentSpritePath = sp->path;
                tree->setCurrentItem(*it);
                break;
            }
            ++it;
        }
    }

    if (!m_currentSpritePath.isEmpty()) {
        loadSprite(m_currentSpritePath);
    } else {
        m_canvas->clearSprite();
    }
    applyNineSliceFilter();
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
    // Apply thirds defaults the first time a sprite is marked as nine-sliced.
    if (sp->isNineSliced
            && sp->nsLeft == 0 && sp->nsTop == 0
            && sp->nsRight == 0 && sp->nsBottom == 0) {
        QPixmap pix(path);
        if (!pix.isNull()) {
            sp->nsLeft   = pix.width()  / 3;
            sp->nsTop    = pix.height() / 3;
            sp->nsRight  = pix.width()  / 3;
            sp->nsBottom = pix.height() / 3;
            // Clear any stale target size written by saveCurrentSprite() before
            // the sprite was marked (e.g. the spinbox minimum of 1 px), so that
            // setTargetSize is not called with a value unrelated to this sprite.
            sp->nsTargetWidth  = 0;
            sp->nsTargetHeight = 0;
        }
    }

    const double prevZoom = m_zoomSpin->value() / 100.0;
    m_updatingUi = true;

    m_nineSlicedBtn->setEnabled(true);
    m_nineSlicedBtn->setChecked(sp->isNineSliced);

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
        m_canvas->setSpriteImage(path);
        m_canvas->setInsets(sp->nsLeft, sp->nsTop, sp->nsRight, sp->nsBottom);
        m_canvas->setFillModes(sp->nsHMode, sp->nsVMode);
        m_canvas->setSliceLinesVisible(true);
        if (sp->nsTargetWidth > 0 && sp->nsTargetHeight > 0)
            m_canvas->setTargetSize(sp->nsTargetWidth, sp->nsTargetHeight);
    } else {
        m_canvas->clearSprite();
    }

    m_updatingUi = false;

    switch (m_settings.nineSliceZoomOnChange) {
        case NineSliceZoomOnChange::FitToFrame: m_canvas->initialFit();      break;
        case NineSliceZoomOnChange::Reset100:   m_canvas->setZoom(1.0);      break;
        case NineSliceZoomOnChange::NoChange:   m_canvas->setZoom(prevZoom); break;
    }
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

void NineSliceWorkspace::applyNineSliceFilter()
{
    if (!m_filterNineSlicedBtn->isChecked()) {
        // Restore text filter (un-hides all items, then re-applies text match)
        m_navigatorPanel->applyFilter(m_navigatorPanel->filterText());
        return;
    }
    auto* tree = m_navigatorPanel->tree();
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        filterItemNineSliced(tree->topLevelItem(i));
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
        m_currentSpritePath.clear();
        m_updatingUi = true;
        m_nineSlicedBtn->setChecked(false);
        m_nineSlicedBtn->setEnabled(false);
        m_updatingUi = false;
        m_configGroup->setEnabled(false);
        m_canvas->clearSprite();
    }
}

void NineSliceWorkspace::onNineSlicedToggled(bool checked)
{
    if (m_updatingUi || m_currentSpritePath.isEmpty() || !m_session) return;
    auto sp = m_session->spriteIndex.value(QDir::cleanPath(m_currentSpritePath));
    if (!sp) return;
    saveCurrentSprite();
    sp->isNineSliced = checked;
    loadSprite(m_currentSpritePath);
    if (m_session) {
        m_navigatorPanel->refresh(m_session, false);
        auto* tree = m_navigatorPanel->tree();
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            auto sp2 = (*it)->data(0, Qt::UserRole).value<SpritePtr>();
            if (sp2 && sp2->path == m_currentSpritePath) {
                tree->blockSignals(true);
                tree->setCurrentItem(*it);
                tree->blockSignals(false);
                break;
            }
            ++it;
        }
    }
    applyNineSliceFilter();
    emit definitionsChanged();
}

void NineSliceWorkspace::onInsetSpinChanged()
{
    if (m_updatingUi) return;
    m_canvas->setInsets(m_leftSpin->value(), m_topSpin->value(),
                        m_rightSpin->value(), m_bottomSpin->value());
    saveCurrentSprite();
    emit definitionsChanged();
}

void NineSliceWorkspace::onCanvasInsetsChanged(int left, int top, int right, int bottom)
{
    if (m_updatingUi) return;
    m_updatingUi = true;
    m_leftSpin->setValue(left);
    m_topSpin->setValue(top);
    m_rightSpin->setValue(right);
    m_bottomSpin->setValue(bottom);
    m_updatingUi = false;
    saveCurrentSprite();
    emit definitionsChanged();
}

void NineSliceWorkspace::onFillModeChanged()
{
    if (m_updatingUi) return;
    m_canvas->setFillModes(m_hModeCombo->currentText(), m_vModeCombo->currentText());
    saveCurrentSprite();
    emit definitionsChanged();
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
    auto* item = m_navigatorPanel->tree()->itemAt(pos);
    if (!item) return;
    auto sprite = item->data(0, Qt::UserRole).value<SpritePtr>();
    if (!sprite || sprite->path.isEmpty()) return;

    // Silently select a tree item by path without firing currentItemChanged.
    // Used after refresh() so the badge updates without re-triggering onNavigatorSelectionChanged.
    auto silentReselect = [this](const QString& path) {
        auto* tree = m_navigatorPanel->tree();
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            auto sp2 = (*it)->data(0, Qt::UserRole).value<SpritePtr>();
            if (sp2 && sp2->path == path) {
                tree->blockSignals(true);
                tree->setCurrentItem(*it);
                tree->blockSignals(false);
                break;
            }
            ++it;
        }
    };

    QMenu menu(this);
    if (sprite->isNineSliced) {
        auto* act = menu.addAction(QIcon(QStringLiteral(":/icons/disabled.svg")), tr("Remove Nine-Slice"));
        connect(act, &QAction::triggered, this, [this, sprite, silentReselect]() {
            saveCurrentSprite();
            sprite->isNineSliced = false;
            m_currentSpritePath = sprite->path;
            loadSprite(sprite->path);
            if (m_session) {
                m_navigatorPanel->refresh(m_session, false);
                silentReselect(sprite->path);
            }
            applyNineSliceFilter();
            emit definitionsChanged();
        });
    } else {
        auto* act = menu.addAction(QIcon(QStringLiteral(":/icons/nine-slice.svg")), tr("Mark as Nine-Sliced"));
        connect(act, &QAction::triggered, this, [this, sprite, silentReselect]() {
            saveCurrentSprite();
            sprite->isNineSliced = true;
            m_currentSpritePath = sprite->path;
            loadSprite(sprite->path);  // quarters applied here if first mark
            if (m_session) {
                m_navigatorPanel->refresh(m_session, false);
                silentReselect(sprite->path);
            }
            applyNineSliceFilter();
            emit definitionsChanged();
        });
    }
    menu.exec(m_navigatorPanel->tree()->mapToGlobal(pos));
}
