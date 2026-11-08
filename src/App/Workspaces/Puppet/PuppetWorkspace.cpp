#include "PuppetWorkspace.h"
#include "AppIcons.h"
#include "UiStyle.h"
#include "AppSettings.h"
#include "MessageDialog.h"
#include "ProjectSession.h"
#include "PuppetCanvas.h"
#include "NewPuppetDialog.h"
#include "PuppetEditing.h"
#include "PuppetMotion.h"
#include "PuppetPoseStrip.h"
#include "PuppetPosing.h"
#include "PuppetSequenceTrack.h"
#include "PuppetSequencing.h"
#include "PuppetPropertiesPanel.h"
#include "PuppetTreeWidget.h"
#include "SpritePickerDialog.h"
#include "SpriteTreeUtils.h"
#include "MarkerCommands.h"

#include <cmath>
#include <QAction>
#include <QtMath>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDir>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabBar>
#include <QTimer>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidgetItemIterator>
#include <QUndoStack>
#include <QVBoxLayout>

namespace {
/// Most height of the selected pose's part list; it scrolls beyond that.
constexpr int POSE_PARTS_HEIGHT = 160;
/// The tabs below the canvas.
constexpr int POSES_TAB = 0;
constexpr int ANIMATIONS_TAB = 1;
constexpr int SEQUENCE_LIST_WIDTH = 180;
constexpr int NAME_COLUMN = 0;
constexpr int DETAIL_COLUMN = 1;

/// A star icon for the variant a part shows, drawn from the font so it needs no resource.
QIcon shownVariantIcon(const QPalette& palette) {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(palette.color(QPalette::WindowText));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("★"));
    return QIcon(pixmap);
}

/// Whether a sprite has a marker a joint can use: "pivot" always does (the sprite's pivot
/// stands in for it), a child's marker only when it's a point.
bool hasJointMarker(const Sprite& sprite, const QString& name, bool asChild) {
    for (const NamedPoint& marker : sprite.points) {
        if (marker.name == name) return !asChild || marker.kind == MarkerKind::Point;
    }
    return name == QLatin1String("pivot");
}

const QString PIVOT_MARKER = QStringLiteral("pivot");

/// Where a part hangs by its pivot in @p sprite: its "pivot" point marker, else its pivot, as
/// PuppetEvaluator and the export use.
QPointF spritePivot(const Sprite& sprite) {
    for (const NamedPoint& marker : sprite.points) {
        if (marker.name == PIVOT_MARKER) return PuppetMotion::markerPoint(marker);
    }
    return QPointF(sprite.pivotX, sprite.pivotY);
}

}  // namespace

PuppetWorkspace::PuppetWorkspace(ProjectSession* session, QUndoStack* undoStack, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
    , m_undoStack(undoStack)
{
    setupUi();
}

void PuppetWorkspace::setupUi() {
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    mainLayout->addWidget(splitter);

    // === Left: atlas selector, puppets tree, Add / Remove ===
    auto* left = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    m_atlasRow = new QWidget(left);
    {
        auto* row = new QHBoxLayout(m_atlasRow);
        row->setContentsMargins(4, 4, 4, 4);
        row->addWidget(new QLabel(tr("Atlas:"), m_atlasRow));
        m_atlasCombo = new QComboBox(m_atlasRow);
        m_atlasCombo->setToolTip(tr("Puppets belong to an atlas and use its sprites and animations"));
        row->addWidget(m_atlasCombo, 1);
        connect(m_atlasCombo, &QComboBox::currentIndexChanged, this, [this]() {
            const QVariant index = m_atlasCombo->currentData();
            if (index.isValid()) emit atlasSelected(index.toInt());
        });
    }
    m_atlasRow->hide();
    leftLayout->addWidget(m_atlasRow);

    m_treeFilter = new QLineEdit(left);
    m_treeFilter->setObjectName(QStringLiteral("puppetFilter"));
    m_treeFilter->setPlaceholderText(tr("Search puppets..."));
    m_treeFilter->setToolTip(tr("Shows the puppets, parts and variants whose names contain this text"));
    m_treeFilter->setClearButtonEnabled(true);
    {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(4, 4, 4, 4);
        row->addWidget(m_treeFilter);
        leftLayout->addLayout(row);
    }
    connect(m_treeFilter, &QLineEdit::textChanged, this, [this](const QString& text) {
        SpriteTreeUtils::filterTree(m_tree, text, {NAME_COLUMN});
    });

    m_tree = new PuppetTreeWidget(left);
    m_tree->setObjectName(QStringLiteral("puppetTree"));
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Name"), tr("Details")});
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(NAME_COLUMN, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(DETAIL_COLUMN, QHeaderView::ResizeToContents);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setToolTip(tr("Parts are listed in draw order: the first is drawn at the back. "
                          "Drag parts or variants to reorder them."));

    auto* emptyState = new QLabel(tr("No puppets yet.\nClick Add › New Puppet to build one from this "
                                     "atlas's sprites."), left);
    emptyState->setAlignment(Qt::AlignCenter);
    emptyState->setWordWrap(true);
    emptyState->setObjectName(QStringLiteral("puppetEmptyState"));
    emptyState->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(emptyState, &QWidget::customContextMenuRequested, this, [this, emptyState](const QPoint& pos) {
        QMenu* menu = createContextMenu();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(emptyState->mapToGlobal(pos));
    });
    m_treeStack = new QStackedWidget(left);
    m_treeStack->addWidget(m_tree);
    m_treeStack->addWidget(emptyState);
    leftLayout->addWidget(m_treeStack, 1);

    m_addPuppetAction = new QAction(AppIcons::icon(QStringLiteral("puppet")), tr("New Puppet..."), this);
    m_addPuppetAction->setToolTip(tr("Add a puppet to this atlas"));
    connect(m_addPuppetAction, &QAction::triggered, this, &PuppetWorkspace::newPuppet);
    m_addPartAction = new QAction(AppIcons::icon(QStringLiteral("add")), tr("New Part"), this);
    m_addPartAction->setToolTip(tr("Add a part to the selected puppet, in front of the others"));
    connect(m_addPartAction, &QAction::triggered, this, &PuppetWorkspace::addPart);
    m_addSpritesAction = new QAction(AppIcons::icon(QStringLiteral("image-add")), tr("Add Sprites..."), this);
    m_addSpritesAction->setToolTip(tr("Pick sprites the selected part can show"));
    connect(m_addSpritesAction, &QAction::triggered, this, &PuppetWorkspace::pickSpriteVariants);
    m_removeAction = new QAction(AppIcons::icon(QStringLiteral("delete")),
                                 tr("Remove"), this);
    m_removeAction->setShortcut(QKeySequence::Delete);
    m_removeAction->setShortcutContext(Qt::WidgetShortcut);
    m_removeAction->setToolTip(tr("Remove the selected puppet, part or variant (Delete)"));
    connect(m_removeAction, &QAction::triggered, this, &PuppetWorkspace::removeSelected);
    m_tree->addAction(m_removeAction);
    m_renameAction = new QAction(tr("Rename"), this);
    m_renameAction->setShortcut(QKeySequence(Qt::Key_F2));
    m_renameAction->setShortcutContext(Qt::WidgetShortcut);
    connect(m_renameAction, &QAction::triggered, this, [this]() {
        if (QTreeWidgetItem* item = m_tree->currentItem()) m_tree->editItem(item, NAME_COLUMN);
    });

    m_addButton = new QToolButton(left);
    m_addButton->setObjectName(QStringLiteral("puppetAddButton"));
    m_addButton->setText(tr("Add"));
    m_addButton->setIcon(AppIcons::icon(QStringLiteral("add")));
    m_addButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_addButton->setPopupMode(QToolButton::InstantPopup);
    m_addButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* addMenu = new QMenu(m_addButton);
    addMenu->addAction(m_addPuppetAction);
    addMenu->addAction(m_addPartAction);
    addMenu->addAction(m_addSpritesAction);
    m_addButton->setMenu(addMenu);
    m_addButton->setToolTip(tr("Add a puppet, a part to the selected puppet, or sprites the selected part can show"));
    UiStyle::setRole(m_addButton, UiStyle::Role::Add);
    m_removeButton = new QPushButton(m_removeAction->icon(), tr("Remove"), left);
    UiStyle::setRole(m_removeButton, UiStyle::Role::Destructive);
    m_removeButton->setObjectName(QStringLiteral("puppetRemoveButton"));
    m_removeButton->setToolTip(m_removeAction->toolTip());
    m_removeButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_removeButton, &QPushButton::clicked, this, &PuppetWorkspace::removeSelected);
    {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(4, 2, 4, 2);
        row->addWidget(m_addButton);
        row->addWidget(m_removeButton);
        leftLayout->addLayout(row);
    }
    {
        // The selected pose's parts: ticked when it sets them, so poses can be combined.
        m_posePartsTitle = new QLabel(left);
        m_posePartsTitle->setContentsMargins(4, 6, 4, 0);
        leftLayout->addWidget(m_posePartsTitle);
        m_poseParts = new QListWidget(left);
        m_poseParts->setObjectName(QStringLiteral("puppetPoseParts"));
        m_poseParts->setWordWrap(true);
        m_posePartsHint = new QLabel(left);
        m_posePartsHint->setObjectName(QStringLiteral("puppetPosePartsHint"));
        m_posePartsHint->setAlignment(Qt::AlignCenter);
        m_posePartsHint->setWordWrap(true);
        m_posePartsHint->setForegroundRole(QPalette::PlaceholderText);
        m_posePartsHint->setContentsMargins(8, 8, 8, 8);
        m_posePartsStack = new QStackedWidget(left);
        m_posePartsStack->addWidget(m_poseParts);
        m_posePartsStack->addWidget(m_posePartsHint);
        m_posePartsStack->setMaximumHeight(POSE_PARTS_HEIGHT);
        leftLayout->addWidget(m_posePartsStack);
        // Both act once the list's own handling is done, since they rebuild it. They take what
        // they need from the item now: by then a click's other signal may have deleted it.
        connect(m_poseParts, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
            const QVariant part = item->data(Qt::UserRole);
            if (!part.isValid()) return;
            const bool included = item->checkState() == Qt::Checked;
            QMetaObject::invokeMethod(this, [this, part = part.toInt(), included]() {
                setPartInPose(part, included);
            }, Qt::QueuedConnection);
        });
        connect(m_poseParts, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            const QVariant part = item->data(Qt::UserRole);
            if (!part.isValid() || part.toInt() == m_selection.part) return;
            QMetaObject::invokeMethod(this, [this, part = part.toInt()]() {
                select({m_selection.puppet, part, -1});
            }, Qt::QueuedConnection);
        });
    }
    splitter->addWidget(left);

    // === Middle: zoom row and canvas ===
    auto* middle = new QWidget(splitter);
    auto* middleLayout = new QVBoxLayout(middle);
    middleLayout->setContentsMargins(4, 4, 4, 4);
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("Zoom:"), middle));
        m_zoomSpin = new QDoubleSpinBox(middle);
        m_zoomSpin->setRange(10.0, 1600.0);
        m_zoomSpin->setValue(100.0);
        m_zoomSpin->setSuffix(QStringLiteral("%"));
        m_zoomSpin->setSingleStep(10.0);
        row->addWidget(m_zoomSpin);
        auto* fitButton = new QPushButton(AppIcons::icon(QStringLiteral("fit")), tr("Fit"), middle);
        fitButton->setObjectName(QStringLiteral("puppetFit"));
        fitButton->setToolTip(tr("Zoom to show the whole puppet"));
        row->addWidget(fitButton);
        m_gridButton = new QPushButton(AppIcons::icon(QStringLiteral("grid")), tr("Grid"), middle);
        m_gridButton->setObjectName(QStringLiteral("puppetGrid"));
        m_gridButton->setCheckable(true);
        m_gridButton->setToolTip(tr("Show a grid from the puppet's origin; its cells and colour are in Settings"));
        row->addWidget(m_gridButton);
        row->addStretch();
        middleLayout->addLayout(row);
        connect(fitButton, &QPushButton::clicked, this, [this]() { m_canvas->fitPuppet(); });
        connect(m_gridButton, &QPushButton::toggled, this, [this](bool checked) {
            PuppetCanvas::Grid grid = m_canvas->grid();
            grid.visible = checked;
            m_canvas->setGrid(grid);
        });
    }
    m_canvas = new PuppetCanvas(middle);
    m_canvas->setObjectName(QStringLiteral("puppetCanvas"));
    middleLayout->addWidget(m_canvas, 1);
    // Below the canvas: the poses, and the animations that play them.
    // A tab bar over pages of which only the current one is shown: the area is as tall as
    // that page (a QTabWidget is as tall as its tallest page, spreading the rest around).
    m_bottomTabs = new QTabBar(middle);
    m_bottomTabs->setObjectName(QStringLiteral("puppetBottomTabs"));
    m_bottomTabs->setExpanding(false);
    m_bottomTabs->setDrawBase(true);
    middleLayout->addWidget(m_bottomTabs);
    {
        // The poses, one frame each, Rest first, like an animation's frames.
        auto* posesPage = new QWidget(middle);
        auto* posesLayout = new QVBoxLayout(posesPage);
        posesLayout->setContentsMargins(4, 4, 4, 4);
        auto* row = new QHBoxLayout;
        m_newPoseButton = new QPushButton(AppIcons::icon(QStringLiteral("add")), tr("Add Pose"), middle);
        m_newPoseButton->setObjectName(QStringLiteral("puppetNewPose"));
        m_newPoseButton->setToolTip(tr("Add a pose after the selected frame. It sets no part until you change one. "
                                       "Right-click a frame for more"));
        UiStyle::setRole(m_newPoseButton, UiStyle::Role::Add);
        m_duplicatePoseButton = new QPushButton(AppIcons::icon(QStringLiteral("duplicate")), tr("Duplicate"), middle);
        m_duplicatePoseButton->setObjectName(QStringLiteral("puppetDuplicatePose"));
        m_duplicatePoseButton->setToolTip(tr("Add a copy of the selected pose after it"));
        m_removePoseButton = new QPushButton(AppIcons::icon(QStringLiteral("delete")), tr("Delete"), middle);
        m_removePoseButton->setObjectName(QStringLiteral("puppetRemovePose"));
        UiStyle::setRole(m_removePoseButton, UiStyle::Role::Destructive);
        m_removePoseButton->setToolTip(tr("Animation steps that play the selected pose go with it (Delete in the strip)"));
        m_poseStatus = new QLabel(middle);
        m_poseStatus->setObjectName(QStringLiteral("puppetPoseStatus"));
        m_poseStatus->setForegroundRole(QPalette::PlaceholderText);
        row->addWidget(m_newPoseButton);
        row->addWidget(m_duplicatePoseButton);
        row->addWidget(m_removePoseButton);
        row->addWidget(m_poseStatus, 1);
        posesLayout->addLayout(row);
        m_poseStrip = new PuppetPoseStrip(posesPage);
        m_poseStrip->setObjectName(QStringLiteral("puppetPoseStrip"));
        posesLayout->addWidget(m_poseStrip);
        m_bottomTabs->addTab(AppIcons::icon(QStringLiteral("pose")), tr("Poses"));
        m_bottomTabs->setTabToolTip(POSES_TAB, tr("Still poses of the puppet, with Rest first"));
        m_bottomPages.append(posesPage);
        middleLayout->addWidget(posesPage);
        connect(m_newPoseButton, &QPushButton::clicked, this, [this]() { addPose(m_pose + 1); });
        connect(m_duplicatePoseButton, &QPushButton::clicked, this, [this]() { duplicatePose(m_pose); });
        connect(m_poseStrip, &PuppetPoseStrip::addRequested, this, &PuppetWorkspace::addPose);
        connect(m_poseStrip, &PuppetPoseStrip::duplicateRequested, this, &PuppetWorkspace::duplicatePose);
        connect(m_removePoseButton, &QPushButton::clicked, this, [this]() { removePose(m_pose); });
        connect(m_poseStrip, &PuppetPoseStrip::frameSelected, this, &PuppetWorkspace::selectPose);
        connect(m_poseStrip, &PuppetPoseStrip::renameRequested, this, &PuppetWorkspace::renamePose);
        connect(m_poseStrip, &PuppetPoseStrip::moveRequested, this, &PuppetWorkspace::movePose);
        connect(m_poseStrip, &PuppetPoseStrip::removeRequested, this, &PuppetWorkspace::removePose);
    }
    m_bottomTabs->addTab(AppIcons::icon(QStringLiteral("keyframes")), tr("Animations"));
    m_bottomTabs->setTabToolTip(ANIMATIONS_TAB, tr("Steps from pose to pose, played over time"));
    m_bottomPages.append(buildAnimationsTab(middle));
    middleLayout->addWidget(m_bottomPages.last());
    fitBottomTabs();
    connect(m_bottomTabs, &QTabBar::currentChanged, this, [this](int tab) {
        fitBottomTabs();
        m_animating = tab == ANIMATIONS_TAB;
        if (!m_animating) setPlaying(false);
        rebuildTree();
        updateCanvas();
        updateProperties();
    });
    splitter->addWidget(middle);
    connect(m_zoomSpin, &QDoubleSpinBox::valueChanged, this, [this](double percent) {
        m_canvas->setZoomManual(true);
        m_canvas->setZoom(percent / 100.0);
    });
    connect(m_canvas, &ZoomableGraphicsView::zoomChanged, this, [this](double zoom) {
        const QSignalBlocker blocker(m_zoomSpin);
        m_zoomSpin->setValue(zoom * 100.0);
    });
    connect(m_canvas, &PuppetCanvas::partClicked, this, [this](int part) {
        // A click on the selected part keeps its variant selected.
        if (m_selection.puppet >= 0 && (part < 0 || part != m_selection.part))
            select({m_selection.puppet, part, -1});
    });
    connect(m_canvas, &PuppetCanvas::partContextMenuRequested, this, [this](int part, const QPoint& globalPos) {
        if (m_selection.puppet < 0) return;
        if (part >= 0 && part != m_selection.part) select({m_selection.puppet, part, -1});
        QMenu* menu = createContextMenu();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(globalPos);
    });
    connect(m_canvas, &PuppetCanvas::partDragStarted, this, [this](int part) {
        const Puppet* puppet = selectedPuppet();
        if (!puppet || part < 0 || part >= puppet->parts.size()) return;
        m_dragStartPosition = shownPosition(*puppet, part);
        ++m_dragCount;
    });
    connect(m_canvas, &PuppetCanvas::partDragged, this, [this](int part, const QPoint& offset) {
        if (part != m_selection.part) return;
        movePositionedPart(part, m_dragStartPosition + offset, QStringLiteral("drag %1").arg(m_dragCount));
    });
    connect(m_canvas, &PuppetCanvas::attachRequested, this, [this](int part, int parent, const QString& marker) {
        const Puppet* puppet = selectedPuppet();
        if (!puppet || part != m_selection.part) return;
        // A part already hanging keeps the marker it hangs by; a new joint uses its pivot.
        const PuppetAttachment* joint = puppetParentAttachment(*puppet, puppet->parts[part].name);
        attachSelectedPart(parent, marker, joint ? joint->childMarker : PIVOT_MARKER);
    });
    connect(m_canvas, &PuppetCanvas::attachToNewMarkerRequested, this,
            [this](int part, int parent, const QPointF& scenePos) {
        if (part != m_selection.part) return;
        attachSelectedPartToNewMarker(parent, scenePos);
    });
    connect(m_canvas, &PuppetCanvas::pivotDoubleClicked, this, [this](int part, const QVector<int>& overlapping) {
        if (part != m_selection.part) return;
        // One part under the pivot: that is the parent. Otherwise the user picks it.
        if (overlapping.size() == 1) {
            attachSelectedPartToNewMarkerAtPivot(overlapping.first());
            return;
        }
        QMenu* menu = createNewMarkerParentMenu(overlapping);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(QCursor::pos());
    });
    connect(m_canvas, &PuppetCanvas::detachRequested, this, [this](int part, const QPointF& scenePos) {
        if (part != m_selection.part) return;
        placeSelectedPartJointAt(scenePos);
    });
    connect(m_canvas, &PuppetCanvas::partTurnStarted, this, [this](int part, bool) {
        const Puppet* puppet = selectedPuppet();
        if (!puppet || part < 0 || part >= puppet->parts.size()) return;
        m_turnStart = shownStates(*puppet)[part];
        ++m_dragCount;
    });
    connect(m_canvas, &PuppetCanvas::partTurned, this, [this](int part, double degrees) {
        setPartAngle(part, m_turnStart.angle + degrees, QStringLiteral("turn %1").arg(m_dragCount));
    });
    connect(m_canvas, &PuppetCanvas::partPullStarted, this, [this](int part, const QPointF& scenePos) {
        const Puppet* puppet = selectedPuppet();
        if (!puppet || part < 0 || part >= puppet->parts.size()) return;
        const QVector<PuppetPartPlacement> placements =
            PuppetEvaluator::evaluate(*puppet, shownStates(*puppet), spriteLookup());
        // Held by that point of its sprite, which then follows the pointer.
        m_pullGrip = placements[part].transform.inverted().map(scenePos);
        ++m_dragCount;
    });
    connect(m_canvas, &PuppetCanvas::partPulled, this, [this](int part, const QPointF& scenePos) {
        pullPart(part, m_pullGrip, scenePos, QStringLiteral("pull %1").arg(m_dragCount));
    });
    connect(m_canvas, &PuppetCanvas::partDrifted, this, [this](int part, const QPointF& offset) {
        driftPart(part, offset, QStringLiteral("turn %1").arg(m_dragCount));
    });
    connect(m_canvas, &PuppetCanvas::partNudged, this, [this](int part, const QPoint& offset) {
        const Puppet* puppet = selectedPuppet();
        if (!puppet || part != m_selection.part || !puppet->parts[part].position) return;
        movePositionedPart(part, shownPosition(*puppet, part) + offset,
                           QStringLiteral("nudge %1 %2").arg(m_selection.puppet).arg(part));
    });

    // === Right: properties ===
    m_properties = new PuppetPropertiesPanel(splitter);
    m_properties->setMinimumWidth(260);
    splitter->addWidget(m_properties);
    connect(m_properties, &PuppetPropertiesPanel::renameRequested, this, &PuppetWorkspace::renameSelected);
    connect(m_properties, &PuppetPropertiesPanel::positionChanged, this,
            [this](const QPoint& position) { movePositionedPart(m_selection.part, position); });
    connect(m_properties, &PuppetPropertiesPanel::shownVariantChanged, this,
            [this](int variant) { setPartVariant(m_selection.part, variant); });
    connect(m_properties, &PuppetPropertiesPanel::attachRequested, this,
            [this](const QString& parent, const QString& parentMarker, const QString& childMarker) {
        const Puppet* puppet = selectedPuppet();
        if (!puppet) return;
        attachSelectedPart(PuppetEditing::partNames(*puppet).indexOf(parent), parentMarker, childMarker);
    });
    connect(m_properties, &PuppetPropertiesPanel::detachRequested, this, &PuppetWorkspace::detachSelectedPart);
    connect(m_properties, &PuppetPropertiesPanel::newParentMarkerRequested, this, [this](const QString& parent) {
        if (const Puppet* puppet = selectedPuppet())
            attachSelectedPartToNewMarkerAtPivot(PuppetEditing::partNames(*puppet).indexOf(parent));
    });
    connect(m_properties, &PuppetPropertiesPanel::hiddenMarkersChanged, this,
            &PuppetWorkspace::setSelectedPartHiddenMarkers);
    connect(m_properties, &PuppetPropertiesPanel::rigLockChanged, this, &PuppetWorkspace::setSelectedPartRigLocked);
    connect(m_properties, &PuppetPropertiesPanel::poseLockChanged, this, &PuppetWorkspace::setSelectedPartPoseLocked);
    connect(m_properties, &PuppetPropertiesPanel::stepEaseChanged, this, [this](PuppetPropertiesPanel::PartEase ease) {
        setSelectedPartStepEase(ease == PuppetPropertiesPanel::PartEase::AsStep ? std::nullopt
                                : std::optional<bool>(ease == PuppetPropertiesPanel::PartEase::Step));
    });
    connect(m_properties, &PuppetPropertiesPanel::angleChanged, this, [this](int angle) {
        setPartAngle(m_selection.part, angle, partEditKey(QStringLiteral("angle")));
    });
    connect(m_properties, &PuppetPropertiesPanel::scaleChanged, this, [this](const QPointF& scale) {
        setPartScale(m_selection.part, scale, partEditKey(QStringLiteral("scale")));
    });
    connect(m_properties, &PuppetPropertiesPanel::opacityChanged, this, [this](double opacity) {
        setPartOpacity(m_selection.part, opacity, partEditKey(QStringLiteral("opacity")));
    });
    connect(m_properties, &PuppetPropertiesPanel::tintChanged, this,
            [this](const QColor& tint) { setPartTint(m_selection.part, tint); });
    connect(m_properties, &PuppetPropertiesPanel::limitsChanged, this,
            [this](int leeway, bool limited, int angleMin, int angleMax) {
        setSelectedPartLimits(leeway, limited ? std::optional<int>(angleMin) : std::nullopt,
                              limited ? std::optional<int>(angleMax) : std::nullopt);
    });

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({260, 580, 280});

    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current) {
        if (m_rebuilding) return;
        m_selection = PuppetTreeWidget::selectionOf(current);
        updateCanvas();
        updateProperties();
        updateButtons();
        updatePoseParts();
    });
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (m_rebuilding || column != NAME_COLUMN) return;
        // Renaming rebuilds the tree, so it waits until Qt is done with the edited item.
        const PuppetSelection renamed = PuppetTreeWidget::selectionOf(item);
        const QString name = item->text(NAME_COLUMN);
        QMetaObject::invokeMethod(this, [this, renamed, name]() {
            m_selection = renamed;
            renameSelected(name);
        }, Qt::QueuedConnection);
    });
    connect(m_tree, &PuppetTreeWidget::moveRequested, this, [this](const PuppetSelection& moved, int to) {
        m_selection = moved;
        moveSelected(to);
    });
    connect(m_tree, &QWidget::customContextMenuRequested, this, &PuppetWorkspace::showContextMenu);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        const PuppetSelection clicked = PuppetTreeWidget::selectionOf(item);
        if (clicked.variant < 0) return;
        // Showing it rebuilds the tree, so it waits until Qt is done with the clicked item.
        QMetaObject::invokeMethod(this, [this, clicked]() { showVariantInPose(clicked); }, Qt::QueuedConnection);
    });

    updateButtons();
}

void PuppetWorkspace::setSettings(const AppSettings& settings) {
    m_canvas->setBackgroundBrush(settings.workspaceColor);
    PuppetCanvas::CanvasSettings canvas;
    canvas.pivotRingRadius = settings.puppetPivotRingRadius;
    canvas.pivotDoubleClickRadius = settings.puppetPivotDoubleClickRadius;
    canvas.turnHandleDistance = settings.puppetTurnHandleDistance;
    canvas.turnHandleRadius = settings.puppetTurnHandleRadius;
    canvas.markerSnapDistance = settings.puppetMarkerSnapDistance;
    canvas.dragDistance = settings.puppetDragDistance;
    canvas.pivotCrossSize = settings.puppetPivotCrossSize;
    canvas.originCrossSize = settings.puppetOriginCrossSize;
    canvas.markerDotRadius = settings.puppetMarkerDotRadius;
    canvas.handleLineWidth = settings.puppetHandleLineWidth;
    canvas.handleColor = settings.puppetHandleColor;
    canvas.rangeColor = settings.puppetRangeColor;
    canvas.originColor = settings.puppetOriginColor;
    canvas.onionBeforeColor = settings.puppetOnionBeforeColor;
    canvas.onionAfterColor = settings.puppetOnionAfterColor;
    canvas.onionSkinOpacity = settings.puppetOnionSkinOpacity;
    canvas.fadedPartOpacity = settings.puppetFadedPartOpacity;
    m_canvas->setCanvasSettings(canvas);
    m_newStep.in = std::clamp(settings.puppetStepIn, 0, PUPPET_MAX_STEP_FRAMES);
    m_newStep.hold = std::clamp(settings.puppetStepHold, 0, PUPPET_MAX_STEP_FRAMES);
    m_newAnimationFps = sanitizedAnimationFps(settings.puppetAnimationFps);
    {
        const QSignalBlocker blocker(m_gridButton);
        m_gridButton->setChecked(settings.showGrid);
    }
    m_canvas->setGrid({settings.showGrid, QSize(settings.gridCellWidth, settings.gridCellHeight),
                       QPoint(settings.gridOffsetX, settings.gridOffsetY), settings.gridColor});
    updateCanvas();  // the crosses and outline are drawn in the new colours and sizes
}

void PuppetWorkspace::enter() {
    refreshSprites();
}

void PuppetWorkspace::leave() {}

void PuppetWorkspace::refreshSprites() {
    m_images.clear();
    m_spritesByPath.clear();
    m_spritesByName.clear();
    if (m_session) {
        for (const LayoutModel& model : m_session->activeAtlas().layoutModels) {
            for (const SpritePtr& sprite : model.sprites) {
                m_spritesByPath.insert(QDir::cleanPath(sprite->path), sprite);
                m_spritesByName.insert(sprite->name, sprite);
            }
        }
    }
    refresh();
}

void PuppetWorkspace::refresh() {
    // Puppets put in the session some other way than an edit or a project load get their
    // Rest pose here, as a load would give them.
    if (m_session) {
        for (Puppet& puppet : m_session->activeAtlas().puppets) {
            PuppetEditing::keepRestPose(puppet);
            PuppetEditing::keepAnimationPosesLast(puppet);
        }
    }
    m_selection = clampedSelection(puppets(), m_selection);
    rebuildTree();
    updateCanvas();
    updateProperties();
    updateButtons();
    updatePoseStrip();
}

void PuppetWorkspace::updateAtlasCombo(const QVector<AtlasEntry>& atlases, int activeIndex) {
    const QSignalBlocker blocker(m_atlasCombo);
    m_atlasCombo->clear();
    for (int i = 0; i < atlases.size(); ++i) {
        const AtlasEntry& atlas = atlases[i];
        if (atlas.isExcluded) continue;
        // An empty atlas has no sprites for puppets, unless it is the active one.
        if (atlas.spritePaths.isEmpty() && i != activeIndex) continue;
        m_atlasCombo->addItem(atlas.name, i);
        if (i == activeIndex) m_atlasCombo->setCurrentIndex(m_atlasCombo->count() - 1);
    }
    m_atlasRow->setVisible(m_atlasCombo->count() > 1);
}

const QVector<Puppet>& PuppetWorkspace::puppets() const {
    static const QVector<Puppet> none;
    return m_session ? m_session->activeAtlas().puppets : none;
}

const Puppet* PuppetWorkspace::selectedPuppet() const {
    const QVector<Puppet>& all = puppets();
    return (m_selection.puppet >= 0 && m_selection.puppet < all.size()) ? &all[m_selection.puppet] : nullptr;
}

const AnimationTimeline* PuppetWorkspace::timelineFor(const PuppetVariant& variant) const {
    if (!m_session || variant.kind != PuppetVariant::Kind::Animation) return nullptr;
    const QVector<AnimationTimeline>& timelines = m_session->activeAtlas().timelines;
    const AnimationTimeline* timeline = nullptr;
    QString name = variant.source;
    // An alias shows its source's frames; the hop limit stops a loop of aliases.
    for (int hops = 0; hops <= timelines.size() && !name.isEmpty(); ++hops) {
        timeline = nullptr;
        for (const AnimationTimeline& t : timelines) {
            if (t.name == name) { timeline = &t; break; }
        }
        if (!timeline || timeline->aliasOf.isEmpty()) break;
        name = timeline->aliasOf;
    }
    return timeline;
}

QVector<SpritePtr> PuppetWorkspace::spritesOf(const PuppetPart& part, QString* problem) const {
    QVector<SpritePtr> sprites;
    const auto add = [&](const SpritePtr& sprite) {
        if (!sprites.contains(sprite)) sprites.append(sprite);
    };
    for (const PuppetVariant& variant : part.variants) {
        if (variant.kind == PuppetVariant::Kind::Animation) {
            const AnimationTimeline* timeline = timelineFor(variant);
            if (!timeline || timeline->frames.isEmpty()) {
                if (problem) *problem = tr("Animation \"%1\" isn't in the atlas").arg(variant.source);
                continue;
            }
            for (const QString& frame : timeline->frames) {
                SpritePtr sprite = m_spritesByPath.value(QDir::cleanPath(frame));
                if (!sprite) sprite = m_spritesByName.value(frame);
                if (sprite) add(sprite);
                else if (problem) *problem = tr("A frame of animation \"%1\" isn't in the atlas").arg(variant.source);
            }
        } else if (const SpritePtr sprite = spriteFor(variant)) {
            add(sprite);
        } else if (problem) {
            *problem = tr("Sprite \"%1\" isn't in the atlas").arg(puppetVariantName(variant));
        }
    }
    return sprites;
}

QString PuppetWorkspace::newMarkerProblem(const PuppetPart& part) const {
    if (part.variants.isEmpty()) return tr("Part \"%1\" has no sprites to add a marker to").arg(part.name);
    // Every sprite gets the marker at its pivot's offset, so each must be found.
    QString problem;
    spritesOf(part, &problem);
    return problem.isEmpty() ? QString() : tr("Can't add a marker: %1").arg(problem);
}

std::optional<double> PuppetWorkspace::animationSeconds() const {
    const Puppet* puppet = selectedPuppet();
    if (!m_animating || m_stillFrames || !puppet || m_sequence < 0 || m_sequence >= puppet->sequences.size())
        return std::nullopt;
    return double(m_playFrame) / sanitizedAnimationFps(puppet->sequences[m_sequence].fps);
}

SpritePtr PuppetWorkspace::spriteFor(const PuppetVariant& variant) const {
    if (!m_session) return nullptr;
    QString spriteSource = variant.source;
    if (variant.kind == PuppetVariant::Kind::Animation) {
        const AnimationTimeline* timeline = timelineFor(variant);
        if (!timeline || timeline->frames.isEmpty()) return nullptr;
        int frame = 0;
        if (const std::optional<double> seconds = animationSeconds()) {
            // The epsilon keeps a frame boundary from rounding down to the frame before.
            const int tick = int(std::floor(*seconds * sanitizedAnimationFps(timeline->fps) + 1e-9));
            frame = std::max(0, animationFrameAt(int(timeline->frames.size()), timeline->loopMode, tick));
        }
        spriteSource = timeline->frames[frame];
    } else if (variant.byIndex) {
        return nullptr;
    }
    if (SpritePtr sprite = m_spritesByPath.value(QDir::cleanPath(spriteSource))) return sprite;
    return m_spritesByName.value(spriteSource);
}

QPixmap PuppetWorkspace::imageOf(const SpritePtr& sprite) const {
    if (!sprite) return {};
    auto it = m_images.constFind(sprite->path);
    if (it == m_images.constEnd()) it = m_images.insert(sprite->path, QPixmap(sprite->path));
    return *it;
}

PuppetSpriteLookup PuppetWorkspace::spriteLookup() const {
    return [this](const PuppetVariant& variant) -> std::optional<PuppetSpriteGeometry> {
        const SpritePtr sprite = spriteFor(variant);
        if (!sprite) return std::nullopt;
        PuppetSpriteGeometry geometry;
        geometry.pivot = QPointF(sprite->pivotX, sprite->pivotY);
        for (const NamedPoint& marker : sprite->points) geometry.markers.insert(marker.name, PuppetMotion::markerPoint(marker));
        return geometry;
    };
}

QString PuppetWorkspace::describeSource(const PuppetVariant& variant) const {
    if (variant.kind == PuppetVariant::Kind::Animation) return tr("Animation \"%1\"").arg(variant.source);
    if (variant.byIndex) return tr("Sprite number %1").arg(variant.source);
    const QString folder = m_session ? m_session->currentFolder : QString();
    const QString relative = folder.isEmpty() ? variant.source : QDir(folder).relativeFilePath(variant.source);
    return tr("Sprite %1").arg(QDir::toNativeSeparators(relative.startsWith(QLatin1String("..")) ? variant.source
                                                                                                 : relative));
}

PuppetSelection PuppetWorkspace::clampedSelection(const QVector<Puppet>& puppets, PuppetSelection selection) {
    if (selection.puppet < 0 || puppets.isEmpty()) return {};
    selection.puppet = qMin(selection.puppet, int(puppets.size()) - 1);
    const Puppet& puppet = puppets[selection.puppet];
    if (selection.part < 0 || puppet.parts.isEmpty()) return {selection.puppet, -1, -1};
    selection.part = qMin(selection.part, int(puppet.parts.size()) - 1);
    const PuppetPart& part = puppet.parts[selection.part];
    if (selection.variant < 0 || part.variants.isEmpty()) return {selection.puppet, selection.part, -1};
    selection.variant = qMin(selection.variant, int(part.variants.size()) - 1);
    return selection;
}

QTreeWidgetItem* PuppetWorkspace::itemFor(const PuppetSelection& selection) const {
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        if (PuppetTreeWidget::selectionOf(*it) == selection) return *it;
    }
    return nullptr;
}

void PuppetWorkspace::rebuildTree() {
    m_rebuilding = true;
    m_tree->clear();
    const QVector<Puppet>& all = puppets();
    const QIcon shownIcon = shownVariantIcon(palette());
    // Unstarred variants get a blank icon as wide, so the names line up.
    QPixmap blank(16, 16);
    blank.fill(Qt::transparent);
    const QIcon blankIcon(blank);
    const QIcon warningIcon = AppIcons::icon(QStringLiteral("warning"));
    const PuppetSpriteLookup lookup = spriteLookup();
    const Qt::ItemFlags baseFlags = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;

    for (int p = 0; p < all.size(); ++p) {
        const Puppet& puppet = all[p];
        auto* puppetItem = new QTreeWidgetItem(m_tree);
        puppetItem->setText(NAME_COLUMN, puppet.name);
        puppetItem->setText(DETAIL_COLUMN, tr("%n part(s)", nullptr, puppet.parts.size()));
        QFont bold = puppetItem->font(NAME_COLUMN);
        bold.setBold(true);
        puppetItem->setFont(NAME_COLUMN, bold);
        puppetItem->setFlags(baseFlags | Qt::ItemIsDropEnabled);
        PuppetTreeWidget::setSelectionOf(puppetItem, {p, -1, -1});

        const QVector<PuppetPartPlacement> placements = PuppetEvaluator::evaluateRest(puppet, lookup);
        // The variants the canvas shows (the edited pose or frame, or the animation where its
        // playhead is) are starred on the selected puppet; other puppets show their rest variants.
        const int edited = p == m_selection.puppet ? editedPose() : -1;
        const bool posing = edited >= 0;
        QVector<int> shownVariants(puppet.parts.size(), 0);
        if (p == m_selection.puppet) {
            const QVector<PuppetPartState> states = shownStates(puppet);
            for (int i = 0; i < states.size() && i < shownVariants.size(); ++i) shownVariants[i] = states[i].variant;
        }
        for (int i = 0; i < puppet.parts.size(); ++i) {
            const PuppetPart& part = puppet.parts[i];
            auto* partItem = new QTreeWidgetItem(puppetItem);
            partItem->setText(NAME_COLUMN, part.name);
            if (const PuppetAttachment* joint = puppetParentAttachment(puppet, part.name)) {
                partItem->setText(DETAIL_COLUMN, QStringLiteral("↳ %1:%2").arg(joint->parent, joint->parentMarker));
                partItem->setToolTip(DETAIL_COLUMN, tr("Hangs from part \"%1\" at its marker \"%2\"")
                                                        .arg(joint->parent, joint->parentMarker));
            } else if (part.position) {
                partItem->setText(DETAIL_COLUMN, tr("at %1,%2").arg(part.position->x()).arg(part.position->y()));
                partItem->setToolTip(DETAIL_COLUMN, tr("Its pivot is at this position from the puppet's origin"));
            } else {
                partItem->setText(DETAIL_COLUMN, tr("not placed"));
            }
            if (!placements[i].problem.isEmpty()) {
                partItem->setIcon(NAME_COLUMN, warningIcon);
                partItem->setToolTip(NAME_COLUMN, placements[i].problem);
            }
            partItem->setFlags(baseFlags | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            PuppetTreeWidget::setSelectionOf(partItem, {p, i, -1});

            for (const int v : PuppetEditing::variantsByName(part)) {
                const PuppetVariant& variant = part.variants[v];
                auto* variantItem = new QTreeWidgetItem(partItem);
                variantItem->setText(NAME_COLUMN, puppetVariantName(variant));
                const QString kind = variant.kind == PuppetVariant::Kind::Animation ? tr("animation") : tr("sprite");
                // Only worth marking when there is a choice.
                if (posing && part.variants.size() > 1 && v == shownVariants.value(i, -1)) {
                    variantItem->setText(DETAIL_COLUMN, tr("%1 · shown").arg(kind));
                    variantItem->setToolTip(DETAIL_COLUMN, tr("Pose \"%1\" shows it").arg(puppet.poses[edited].name));
                    QFont shownFont = variantItem->font(NAME_COLUMN);
                    shownFont.setBold(true);
                    variantItem->setFont(NAME_COLUMN, shownFont);
                } else {
                    variantItem->setText(DETAIL_COLUMN, kind);
                    if (posing) variantItem->setToolTip(DETAIL_COLUMN, tr("Double-click to show it in the selected pose"));
                }
                if (!spriteFor(variant)) {
                    variantItem->setIcon(NAME_COLUMN, warningIcon);
                    variantItem->setToolTip(NAME_COLUMN, tr("%1 isn't in this atlas").arg(describeSource(variant)));
                } else {
                    variantItem->setIcon(NAME_COLUMN, v == shownVariants.value(i, 0) ? shownIcon : blankIcon);
                    variantItem->setToolTip(NAME_COLUMN, v == 0 ? tr("%1 (rest variant)").arg(describeSource(variant))
                                                                : describeSource(variant));
                }
                variantItem->setFlags(baseFlags);
                PuppetTreeWidget::setSelectionOf(variantItem, {p, i, v});
            }
        }
    }
    m_tree->expandAll();
    SpriteTreeUtils::filterTree(m_tree, m_treeFilter->text(), {NAME_COLUMN});
    m_treeStack->setCurrentIndex(all.isEmpty() ? 1 : 0);
    m_treeFilter->setEnabled(!all.isEmpty());
    m_tree->setCurrentItem(itemFor(m_selection));
    m_rebuilding = false;
}

void PuppetWorkspace::updateCanvas() {
    const Puppet* puppet = selectedPuppet();
    if (!puppet) {
        // With no puppets, the tree's own empty state says how to start.
        m_canvas->setEmptyText(puppets().isEmpty() ? QString() : tr("Select a puppet on the left"));
        m_canvas->clearPuppet();
        m_fittedPuppet.clear();
        return;
    }
    m_canvas->setEmptyText(QString());
    // Another puppet starts at rest.
    const QString puppetKey = m_session->activeAtlas().id + QLatin1Char('/') + puppet->name;
    if (puppetKey != m_posedPuppet) {
        m_posedPuppet = puppetKey;
        m_pose = puppet->poses.isEmpty() ? -1 : 0;  // the Rest pose
        m_sequence = -1;
        m_step = -1;
        m_playFrame = 0;
        m_playAlong.clear();
        setPlaying(false);
        updatePoseStrip();
    }
    const QVector<PuppetPartState> states = shownStates(*puppet);
    const QSet<int> inPose = partsInPose(*puppet);
    const QVector<PuppetPartPlacement> placements = PuppetEvaluator::evaluate(*puppet, states, spriteLookup());
    QVector<PuppetCanvas::Part> parts;
    parts.reserve(puppet->parts.size());
    for (int i = 0; i < puppet->parts.size(); ++i) {
        const PuppetPart& part = puppet->parts[i];
        PuppetCanvas::Part drawn;
        drawn.placed = placements[i].placed;
        drawn.transform = placements[i].transform;
        // A part placed by its attachment moves with its parent, not on its own.
        drawn.movable = part.position.has_value() && !puppetParentAttachment(*puppet, part.name);
        // Locked in the pose it can't move or turn; on Rest, moving it moves the rig.
        drawn.locked = lockedInPose(*puppet, i);
        const bool onRest = !m_animating && (m_pose < 0 || puppetIsRestPose(*puppet, m_pose));
        if (drawn.locked || (onRest && part.rigLocked)) drawn.movable = false;
        drawn.attachTarget = m_selection.part >= 0 && PuppetEditing::canAttach(*puppet, m_selection.part, i);
        if (drawn.attachTarget) drawn.newMarkerProblem = newMarkerProblem(part);
        drawn.attached = puppetParentAttachment(*puppet, part.name) != nullptr;
        drawn.joint = placements[i].pivot;
        drawn.opacity = placements[i].opacity;
        drawn.tint = placements[i].tint;
        // Only the Poses tab fades the parts a pose leaves alone: in an animation, faint
        // parts would be mistaken for the onion skin.
        drawn.included = m_animating || editedPose() < 0 || inPose.contains(i);
        // The variant the pose shows (the rest variant unless it picks another).
        const SpritePtr sprite = part.variants.isEmpty() ? nullptr : spriteFor(part.variants[states[i].variant]);
        drawn.image = imageOf(sprite);
        if (sprite) {
            bool hasPivotMarker = false;
            for (const NamedPoint& marker : sprite->points) {
                hasPivotMarker = hasPivotMarker || marker.name == PIVOT_MARKER;
                if (marker.name != PIVOT_MARKER && part.hiddenMarkers.contains(marker.name)) continue;
                drawn.markers.append({marker.name, drawn.transform.map(PuppetMotion::markerPoint(marker)),
                                      drawn.transform.map(PuppetMotion::markerShape(marker))});
            }
            if (!hasPivotMarker)
                drawn.markers.append({PIVOT_MARKER, drawn.transform.map(QPointF(sprite->pivotX, sprite->pivotY)), {}});
        }
        parts.append(drawn);
    }
    const int selectedPart = m_selection.part;
    std::optional<QPointF> pivot;
    if (selectedPart >= 0 && selectedPart < placements.size() && placements[selectedPart].placed)
        pivot = placements[selectedPart].pivot;
    // While animating, the canvas plays, and edits a step's pose only once it has arrived;
    // the rig is changed in the Poses tab.
    m_canvas->setPosing(editedPose() >= 0);
    m_canvas->setEditable(!m_animating || editingStep());
    m_canvas->setRigEditable(!m_animating && !(selectedPart >= 0 && selectedPart < puppet->parts.size()
                                                 && puppet->parts[selectedPart].rigLocked));
    m_canvas->setOnionSkins(onionSkins(*puppet));
    m_canvas->showPuppet(parts, selectedPart, pivot, jointRange(*puppet, placements));
    updatePoseButtons();

    // Fit when another puppet is shown, not after every edit of this one.
    const QString fitKey = m_session->activeAtlas().id + QLatin1Char('/') + QString::number(m_selection.puppet);
    if (fitKey != m_fittedPuppet) {
        m_fittedPuppet = fitKey;
        m_canvas->fitPuppet();
    }
}

void PuppetWorkspace::updateProperties() {
    const Puppet* puppet = selectedPuppet();
    if (!puppet) {
        m_properties->showNothing();
        return;
    }
    const int p = m_selection.puppet;
    if (m_selection.part < 0) {
        m_properties->showPuppet(puppet->name, puppet->parts.size(), [this, p](const QString& name) {
            return PuppetEditing::puppetNameProblem(puppets(), p, name);
        });
        return;
    }
    const int i = m_selection.part;
    const PuppetPart& part = puppet->parts[i];
    if (m_selection.variant < 0) {
        PuppetPropertiesPanel::Placement placement;
        placement.position = part.position;
        // While posing, a positioned part's place is the pose's.
        if (part.position && (m_pose >= 0 || m_animating)) placement.position = shownPosition(*puppet, i);
        placement.variants = PuppetEditing::variantNames(part);
        placement.variantOrder = PuppetEditing::variantsByName(part);
        if (const PuppetAttachment* joint = puppetParentAttachment(*puppet, part.name)) {
            placement.parent = joint->parent;
            placement.parentMarker = joint->parentMarker;
            placement.childMarker = joint->childMarker;
            placement.leeway = joint->leeway;
            placement.angleMin = joint->angleMin;
            placement.angleMax = joint->angleMax;
        }
        // Hidden markers aren't offered, but the joint's own stay listed.
        const auto offered = [](QVector<PuppetPropertiesPanel::MarkerChoice> choices, const QStringList& hidden,
                                const QString& used) {
            choices.removeIf([&](const PuppetPropertiesPanel::MarkerChoice& choice) {
                return choice.name != used && hidden.contains(choice.name);
            });
            return choices;
        };
        for (int j = 0; j < puppet->parts.size(); ++j) {
            if (!PuppetEditing::canAttach(*puppet, i, j)) continue;
            const PuppetPart& parent = puppet->parts[j];
            const QString used = parent.name == placement.parent ? placement.parentMarker : QString();
            placement.parents.append(parent.name);
            placement.parentMarkers.insert(parent.name, offered(jointMarkers(parent, false), parent.hiddenMarkers, used));
            const QString problem = newMarkerProblem(parent);
            if (!problem.isEmpty()) placement.newMarkerProblems.insert(parent.name, problem);
        }
        placement.childMarkers = offered(jointMarkers(part, true), part.hiddenMarkers, placement.childMarker);
        for (const PuppetPropertiesPanel::MarkerChoice& marker : jointMarkers(part, false)) {
            if (marker.name != PIVOT_MARKER) placement.markers.append(marker.name);
        }
        placement.hiddenMarkers = part.hiddenMarkers;
        const PuppetPartState shown = shownStates(*puppet)[i];
        placement.posing = editedPose() >= 0;
        placement.angle = int(std::lround(shown.angle));
        placement.scale = shown.scale;
        placement.opacity = shown.opacity;
        placement.tint = shown.tint;
        placement.variant = shown.variant;
        placement.rigLocked = part.rigLocked;
        const int edited = editedPose();
        placement.pose = edited >= 0 ? puppet->poses[edited].name : QString();
        placement.poseLocked = lockedInPose(*puppet, i);
        // On Rest (or with no pose), X and Y move the rig.
        const bool movesRig = !m_animating && (m_pose < 0 || puppetIsRestPose(*puppet, m_pose));
        placement.positionLocked = placement.poseLocked || (movesRig && part.rigLocked);
        placement.animating = m_animating;
        if (m_animating && m_sequence >= 0 && m_step >= 0 && m_step < puppet->sequences[m_sequence].steps.size()) {
            const PuppetSequence& sequence = puppet->sequences[m_sequence];
            const PuppetSequenceStep& step = sequence.steps[m_step];
            PuppetPropertiesPanel::StepEase ease;
            ease.sequence = sequence.name;
            ease.step = m_step + 1;
            ease.stepSnaps = step.snap;
            if (step.partSnaps.contains(part.name)) ease.snaps = step.partSnaps.value(part.name);
            ease.setByPose = false;
            for (const PuppetPose& pose : puppet->poses) {
                if (pose.name != step.pose) continue;
                for (const PuppetPoseSetting& setting : pose.settings) {
                    if (setting.part == part.name) ease.setByPose = true;
                }
            }
            placement.stepEase = ease;
        }
        const QVector<PuppetPartPlacement> placements = PuppetEvaluator::evaluateRest(*puppet, spriteLookup());
        m_properties->showPart(part.name, placement, placements[i].problem,
                               [this, p, i](const QString& name) {
            return PuppetEditing::partNameProblem(puppets()[p], i, name);
        });
        return;
    }
    const int v = m_selection.variant;
    const PuppetVariant& variant = part.variants[v];
    PuppetVariant unnamed = variant;
    unnamed.name.clear();
    m_properties->showVariant(variant.name, puppetVariantName(unnamed), describeSource(variant),
                              imageOf(spriteFor(variant)), v == 0, [this, p, i, v](const QString& name) {
        return PuppetEditing::variantNameProblem(puppets()[p].parts[i], v, name);
    });
}

void PuppetWorkspace::updateButtons() {
    const bool hasPuppet = m_selection.puppet >= 0;
    m_addPuppetAction->setEnabled(m_session != nullptr);
    m_addPartAction->setEnabled(hasPuppet);
    m_addSpritesAction->setEnabled(m_selection.part >= 0);
    m_removeAction->setEnabled(hasPuppet);
    m_removeButton->setEnabled(hasPuppet);
    m_renameAction->setEnabled(hasPuppet);
}

void PuppetWorkspace::showContextMenu(const QPoint& pos) {
    if (QTreeWidgetItem* item = m_tree->itemAt(pos)) m_tree->setCurrentItem(item);
    QMenu* menu = createContextMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(m_tree->viewport()->mapToGlobal(pos));
}

QMenu* PuppetWorkspace::createContextMenu() {
    auto* menu = new QMenu(this);
    menu->addAction(m_addPuppetAction);
    if (m_selection.puppet >= 0) menu->addAction(m_addPartAction);
    if (m_selection.part >= 0) menu->addAction(m_addSpritesAction);
    if (m_selection.puppet >= 0) {
        menu->addSeparator();
        if (m_selection.variant >= 0) {
            const Puppet& puppet = puppets()[m_selection.puppet];
            const int edited = editedPose();
            const bool posing = edited >= 0;
            const PuppetSelection variant = m_selection;
            QAction* show = menu->addAction(posing ? tr("Show in Pose \"%1\"").arg(puppet.poses[edited].name)
                                                   : tr("Show in Pose"),
                                            this, [this, variant]() { showVariantInPose(variant); });
            show->setEnabled(posing && shownStates(puppet)[variant.part].variant != variant.variant
                             && !lockedInPose(puppet, variant.part));
        } else if (m_selection.part >= 0) {
            const int count = puppets()[m_selection.puppet].parts.size();
            const int part = m_selection.part;
            const bool front = part + 1 >= count;
            const bool back = part <= 0;
            menu->addAction(tr("Bring to Front"), this, [this, count]() { moveSelected(count - 1); })->setEnabled(!front);
            menu->addAction(tr("Bring Forward"), this, [this, part]() { moveSelected(part + 1); })->setEnabled(!front);
            menu->addAction(tr("Send Backward"), this, [this, part]() { moveSelected(part - 1); })->setEnabled(!back);
            menu->addAction(tr("Send to Back"), this, [this]() { moveSelected(0); })->setEnabled(!back);
        }
        menu->addSeparator();
        menu->addAction(m_renameAction);
        menu->addAction(m_removeAction);
    }
    return menu;
}

void PuppetWorkspace::select(const PuppetSelection& selection) {
    m_selection = clampedSelection(puppets(), selection);
    if (QTreeWidgetItem* item = itemFor(m_selection)) {
        m_rebuilding = true;  // updated below, once
        m_tree->setCurrentItem(item);
        m_tree->scrollToItem(item);
        m_rebuilding = false;
    }
    updateCanvas();
    updateProperties();
    updateButtons();
    updatePoseParts();
}

void PuppetWorkspace::pushEdit(const QString& text, const QVector<Puppet>& after,
                               const PuppetSelection& selectionAfter, const QString& mergeKey) {
    if (!m_session || !m_undoStack) return;
    // Every puppet keeps its Rest pose, setting new parts too and keeping up with the rig.
    QVector<Puppet> kept = after;
    for (Puppet& puppet : kept) {
        const Puppet* before = nullptr;
        for (const Puppet& candidate : puppets()) {
            if (candidate.name == puppet.name) before = &candidate;
        }
        PuppetEditing::keepRestPose(puppet, before);
        PuppetEditing::keepAnimationPosesLast(puppet);
    }
    m_undoStack->push(new PuppetsEditCommand(
        text, m_session->activeAtlas().id, puppets(), kept, m_selection, selectionAfter,
        [this](const QString& atlasId, const QVector<Puppet>& puppets, const PuppetSelection& selection) {
            applyPuppets(atlasId, puppets, selection);
        },
        mergeKey));
}

void PuppetWorkspace::applyPuppets(const QString& atlasId, const QVector<Puppet>& puppets,
                                   const PuppetSelection& selection) {
    if (!m_session) return;
    for (AtlasEntry& atlas : m_session->atlases) {
        if (atlas.id == atlasId) atlas.puppets = puppets;
    }
    if (m_session->activeAtlas().id != atlasId) return;
    m_selection = selection;
    refresh();
}

void PuppetWorkspace::newPuppet() {
    if (!m_session) return;
    NewPuppetDialog dialog(m_session, puppets(), atlasSpriteFilter(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    addPuppet(dialog.puppet());
}

void PuppetWorkspace::addPuppet(const Puppet& puppet) {
    QVector<Puppet> after = puppets();
    after.append(puppet);
    if (!PuppetEditing::puppetNameProblem(puppets(), -1, puppet.name).isEmpty()) {
        const QString base = puppet.name.trimmed().isEmpty() ? tr("puppet") : puppet.name.trimmed();
        after.last().name = PuppetEditing::uniqueName(base, PuppetEditing::puppetNames(puppets()));
    }
    pushEdit(tr("New Puppet"), after, {int(after.size()) - 1, -1, -1});
}

std::function<bool(const SpritePtr&)> PuppetWorkspace::atlasSpriteFilter() const {
    QSet<QString> inAtlas;
    for (const QString& path : m_session->activeAtlas().spritePaths) inAtlas.insert(QDir::cleanPath(path));
    return [inAtlas](const SpritePtr& sprite) { return inAtlas.contains(QDir::cleanPath(sprite->path)); };
}

void PuppetWorkspace::addPart() {
    if (!selectedPuppet()) return;
    QVector<Puppet> after = puppets();
    Puppet& puppet = after[m_selection.puppet];
    PuppetPart part;
    part.name = PuppetEditing::uniqueName(tr("part"), PuppetEditing::partNames(puppet));
    // A part with no attachment is placed at the puppet's origin.
    part.position = QPoint(0, 0);
    puppet.parts.append(part);
    const PuppetSelection added{m_selection.puppet, int(puppet.parts.size()) - 1, -1};
    pushEdit(tr("New Part"), after, added);
    if (QTreeWidgetItem* item = itemFor(added)) m_tree->editItem(item, NAME_COLUMN);
}

void PuppetWorkspace::pickSpriteVariants() {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || m_selection.part < 0) return;
    SpritePickerDialog::Options options;
    options.title = tr("Add Sprites to Part \"%1\"").arg(puppet->parts[m_selection.part].name);
    options.acceptText = tr("Add");
    options.emptyText = tr("This atlas has no sprites.");
    options.filter = atlasSpriteFilter();
    addSpriteVariants(SpritePickerDialog::pick(this, m_session, options));
}

void PuppetWorkspace::addSpriteVariants(const QStringList& spritePaths) {
    if (!selectedPuppet() || m_selection.part < 0 || spritePaths.isEmpty()) return;
    QVector<Puppet> after = puppets();
    const int firstAdded = after[m_selection.puppet].parts[m_selection.part].variants.size();
    if (PuppetEditing::addSpriteVariants(after[m_selection.puppet], m_selection.part, spritePaths) == 0) return;
    pushEdit(tr("Add Sprites to Part"), after, {m_selection.puppet, m_selection.part, firstAdded});
}

void PuppetWorkspace::removeSelected() {
    if (!selectedPuppet()) return;
    QVector<Puppet> after = puppets();
    const PuppetSelection s = m_selection;
    if (s.variant >= 0) {
        PuppetEditing::removeVariant(after[s.puppet], s.part, s.variant);
        pushEdit(tr("Remove Variant"), after, {s.puppet, s.part, s.variant});
    } else if (s.part >= 0) {
        PuppetEditing::removePart(after[s.puppet], s.part);
        pushEdit(tr("Remove Part"), after, {s.puppet, s.part, -1});
    } else {
        after.removeAt(s.puppet);
        pushEdit(tr("Remove Puppet"), after, {s.puppet, -1, -1});
    }
}

void PuppetWorkspace::renameSelected(const QString& name) {
    if (!selectedPuppet()) return;
    QVector<Puppet> after = puppets();
    const PuppetSelection s = m_selection;
    QString problem;
    QString text;
    if (s.variant >= 0) {
        problem = PuppetEditing::variantNameProblem(after[s.puppet].parts[s.part], s.variant, name);
        PuppetEditing::setVariantName(after[s.puppet], s.part, s.variant, name);
        text = tr("Rename Variant");
    } else if (s.part >= 0) {
        problem = PuppetEditing::partNameProblem(after[s.puppet], s.part, name);
        PuppetEditing::renamePart(after[s.puppet], s.part, name);
        text = tr("Rename Part");
    } else {
        problem = PuppetEditing::puppetNameProblem(after, s.puppet, name);
        after[s.puppet].name = name;
        text = tr("Rename Puppet");
    }
    if (!problem.isEmpty()) {
        MessageDialog::warning(this, tr("Can't Rename"), problem);
        refresh();  // puts the old name back in the tree
        return;
    }
    if (after == puppets()) {
        refresh();
        return;
    }
    pushEdit(text, after, s);
}

void PuppetWorkspace::moveSelected(int to) {
    if (!selectedPuppet() || m_selection.part < 0 || m_selection.variant >= 0) return;
    QVector<Puppet> after = puppets();
    const PuppetSelection s = m_selection;
    PuppetEditing::movePart(after[s.puppet], s.part, to);
    if (after == puppets()) return;
    pushEdit(tr("Change Draw Order"), after, {s.puppet, to, -1});
}

void PuppetWorkspace::setSelectedPartPosition(const QPoint& position, const QString& mergeKey) {
    if (!selectedPuppet() || m_selection.part < 0 || selectedPuppet()->parts[m_selection.part].rigLocked) return;
    QVector<Puppet> after = puppets();
    PuppetPart& part = after[m_selection.puppet].parts[m_selection.part];
    if (part.position == position) return;
    part.position = position;
    pushEdit(tr("Move Part"), after, m_selection,
             mergeKey.isEmpty() ? QStringLiteral("position %1 %2").arg(m_selection.puppet).arg(m_selection.part)
                                : mergeKey);
}

QVector<PuppetPropertiesPanel::MarkerChoice> PuppetWorkspace::jointMarkers(const PuppetPart& part, bool asChild) const {
    // Every marker any variant has, "pivot" first, each with the variants that lack it.
    QStringList names{PIVOT_MARKER};
    QVector<SpritePtr> sprites;
    for (const PuppetVariant& variant : part.variants) {
        const SpritePtr sprite = spriteFor(variant);
        sprites.append(sprite);
        if (!sprite) continue;
        for (const NamedPoint& marker : sprite->points) {
            const bool usable = !asChild || marker.kind == MarkerKind::Point;
            if (usable && !names.contains(marker.name)) names.append(marker.name);
        }
    }
    QVector<PuppetPropertiesPanel::MarkerChoice> choices;
    for (const QString& name : names) {
        PuppetPropertiesPanel::MarkerChoice choice{name, {}};
        for (int v = 0; v < part.variants.size(); ++v) {
            if (!sprites[v] || !hasJointMarker(*sprites[v], name, asChild))
                choice.missingIn.append(puppetVariantName(part.variants[v]));
        }
        choices.append(choice);
    }
    return choices;
}

void PuppetWorkspace::attachSelectedPart(int parentIndex, const QString& parentMarker, const QString& childMarker) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || m_selection.part < 0 || puppet->parts[m_selection.part].rigLocked) return;
    QVector<Puppet> after = puppets();
    PuppetEditing::attachPart(after[m_selection.puppet], m_selection.part, parentIndex, parentMarker, childMarker);
    if (after == puppets()) return;
    pushEdit(tr("Attach Part"), after, {m_selection.puppet, m_selection.part, -1});
}

void PuppetWorkspace::attachSelectedPartToNewMarker(int parentIndex, const QPointF& scenePos) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || m_selection.part < 0 || parentIndex < 0 || parentIndex >= puppet->parts.size()
        || puppet->parts[m_selection.part].rigLocked || !PuppetEditing::canAttach(*puppet, m_selection.part, parentIndex))
        return;
    const PuppetPart& parent = puppet->parts[parentIndex];
    const QString problem = newMarkerProblem(parent);
    if (!problem.isEmpty()) {
        MessageDialog::warning(this, tr("Can't Add a Marker"), problem);
        return;
    }
    const QVector<PuppetPartState> states = shownStates(*puppet);
    const QVector<PuppetPartPlacement> placements = PuppetEvaluator::evaluate(*puppet, states, spriteLookup());
    const SpritePtr shown = spriteFor(parent.variants.value(states[parentIndex].variant));
    if (!placements[parentIndex].placed || !shown) return;
    // Where the drop is on the sprite shown, from its pivot: the same offset on every sprite.
    const QPointF offset = placements[parentIndex].transform.inverted().map(scenePos) - spritePivot(*shown);

    const QVector<SpritePtr> sprites = spritesOf(parent, nullptr);
    QStringList taken{PIVOT_MARKER};
    for (const SpritePtr& sprite : sprites) {
        for (const NamedPoint& marker : sprite->points) taken.append(marker.name);
    }
    const QString name = PuppetEditing::uniqueName(puppet->parts[m_selection.part].name, taken);

    QVector<SetMarkersCommand::CoTarget> changes;
    for (const SpritePtr& sprite : sprites) {
        const QVector<NamedPoint> before = sprite->points;
        NamedPoint marker;
        marker.name = name;
        const QPointF at = spritePivot(*sprite) + offset;
        marker.x = int(std::lround(at.x()));
        marker.y = int(std::lround(at.y()));
        sprite->points.append(marker);
        changes.append({sprite, before, sprite->points});
    }
    m_undoStack->beginMacro(tr("Attach to New Marker"));
    // Applied above: the command only redoes and undoes them.
    m_undoStack->push(new SetMarkersCommand(changes.first().sprite, changes.first().oldPoints,
                                            changes.first().newPoints, [this]() {
        refresh();
        emit spriteMarkersChanged();
    }, changes.mid(1)));
    attachSelectedPart(parentIndex, name, PIVOT_MARKER);
    m_undoStack->endMacro();
    emit spriteMarkersChanged();
}

void PuppetWorkspace::attachSelectedPartToNewMarkerAtPivot(int parentIndex) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || m_selection.part < 0) return;
    const QVector<PuppetPartState> states = shownStates(*puppet);
    const QVector<PuppetPartPlacement> placements = PuppetEvaluator::evaluate(*puppet, states, spriteLookup());
    const PuppetPart& part = puppet->parts[m_selection.part];
    const SpritePtr sprite = spriteFor(part.variants.value(states[m_selection.part].variant));
    if (!sprite || !placements[m_selection.part].placed) return;
    attachSelectedPartToNewMarker(parentIndex, placements[m_selection.part].transform.map(spritePivot(*sprite)));
}

QMenu* PuppetWorkspace::createNewMarkerParentMenu(const QVector<int>& overlapping) {
    auto* menu = new QMenu(this);
    menu->setToolTipsVisible(true);
    const Puppet* puppet = selectedPuppet();
    if (!puppet || m_selection.part < 0) return menu;
    QVector<int> candidates = overlapping;
    if (candidates.size() < 2) {
        candidates.clear();
        for (int i = 0; i < puppet->parts.size(); ++i) {
            if (PuppetEditing::canAttach(*puppet, m_selection.part, i)) candidates.append(i);
        }
    }
    menu->addSection(tr("Hang From a New Marker On"));
    if (candidates.isEmpty()) menu->addAction(tr("No part it can hang from"))->setEnabled(false);
    for (const int candidate : candidates) {
        const QString problem = newMarkerProblem(puppet->parts[candidate]);
        QAction* action = menu->addAction(puppet->parts[candidate].name, this, [this, candidate]() {
            attachSelectedPartToNewMarkerAtPivot(candidate);
        });
        action->setEnabled(problem.isEmpty());
        action->setToolTip(problem);
    }
    return menu;
}

void PuppetWorkspace::detachSelectedPart() {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || m_selection.part < 0 || puppet->parts[m_selection.part].rigLocked) return;
    const PuppetPart& part = puppet->parts[m_selection.part];
    // Keep it where it is: its sprite's pivot, wherever the attachment put it.
    const QVector<PuppetPartPlacement> placements = PuppetEvaluator::evaluateRest(*puppet, spriteLookup());
    const PuppetPartPlacement& placement = placements[m_selection.part];
    const SpritePtr sprite = part.variants.isEmpty() ? nullptr : spriteFor(part.variants.first());
    QPoint position = part.position.value_or(QPoint());
    if (placement.placed && sprite)
        position = placement.transform.map(QPointF(sprite->pivotX, sprite->pivotY)).toPoint();
    QVector<Puppet> after = puppets();
    PuppetEditing::detachPart(after[m_selection.puppet], m_selection.part, position);
    if (after == puppets()) return;
    pushEdit(tr("Detach Part"), after, {m_selection.puppet, m_selection.part, -1});
}

void PuppetWorkspace::placeSelectedPartJointAt(const QPointF& scenePos) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || m_selection.part < 0 || puppet->parts[m_selection.part].rigLocked) return;
    const PuppetPart& part = puppet->parts[m_selection.part];
    // The handle is the part's joint: its pivot, or the marker it hangs by. The position
    // is where the pivot goes, so it keeps the marker's offset from the pivot.
    QPointF pivotOffset;
    const PuppetAttachment* joint = puppetParentAttachment(*puppet, part.name);
    const SpritePtr sprite = part.variants.isEmpty() ? nullptr : spriteFor(part.variants.first());
    if (joint && sprite) {
        for (const NamedPoint& marker : sprite->points) {
            if (marker.name == joint->childMarker)
                pivotOffset = PuppetMotion::markerPoint(marker) - QPointF(sprite->pivotX, sprite->pivotY);
        }
    }
    const QPointF position = scenePos - pivotOffset;
    QVector<Puppet> after = puppets();
    PuppetEditing::detachPart(after[m_selection.puppet], m_selection.part,
                              QPoint(qRound(position.x()), qRound(position.y())));
    if (after == puppets()) return;
    pushEdit(joint ? tr("Detach Part") : tr("Move Part"), after, {m_selection.puppet, m_selection.part, -1});
}

void PuppetWorkspace::setSelectedPartLimits(int leeway, std::optional<int> angleMin, std::optional<int> angleMax) {
    if (!selectedPuppet() || m_selection.part < 0 || selectedPuppet()->parts[m_selection.part].rigLocked) return;
    QVector<Puppet> after = puppets();
    PuppetEditing::setAttachmentLimits(after[m_selection.puppet], m_selection.part, leeway, angleMin, angleMax);
    if (after == puppets()) return;
    pushEdit(tr("Change Joint Limits"), after, m_selection,
             QStringLiteral("limits %1 %2").arg(m_selection.puppet).arg(m_selection.part));
}

std::optional<NamedPoint> PuppetWorkspace::parentMarkerOf(const Puppet& puppet, int partIndex) const {
    const PuppetAttachment* joint = puppetParentAttachment(puppet, puppet.parts[partIndex].name);
    const PuppetPart* parent = joint ? puppetPart(puppet, joint->parent) : nullptr;
    const SpritePtr sprite = (parent && !parent->variants.isEmpty()) ? spriteFor(parent->variants.first()) : nullptr;
    if (!sprite) return std::nullopt;
    for (const NamedPoint& marker : sprite->points) {
        if (marker.name == joint->parentMarker) return marker;
    }
    if (joint->parentMarker != PIVOT_MARKER) return std::nullopt;
    NamedPoint pivot;
    pivot.name = PIVOT_MARKER;
    pivot.x = sprite->pivotX;
    pivot.y = sprite->pivotY;
    return pivot;
}

QVector<PuppetPartState> PuppetWorkspace::shownStates(const Puppet& puppet) const {
    if (m_animating) return animationStates(puppet);
    if (m_pose < 0 || m_pose >= puppet.poses.size()) return QVector<PuppetPartState>(puppet.parts.size());
    return limitedStates(puppet, PuppetPosing::statesOf(puppet, puppet.poses[m_pose]));
}

QVector<PuppetPartState> PuppetWorkspace::limitedStates(const Puppet& puppet, QVector<PuppetPartState> states) const {
    states.resize(puppet.parts.size());
    for (int i = 0; i < puppet.parts.size(); ++i) {
        // The rig may have changed since: keep within its limits as they are now.
        states[i].angle = limitedAngle(puppet, i, states[i].angle);
        const PuppetAttachment* joint = puppetParentAttachment(puppet, puppet.parts[i].name);
        const std::optional<NamedPoint> marker = joint ? parentMarkerOf(puppet, i) : std::nullopt;
        states[i].offset = marker ? QPointF(PuppetMotion::clampOffset(*marker, joint->leeway, states[i].offset))
                                  : QPointF();
        // Only a part placed by position goes elsewhere in a pose.
        if (joint || !puppet.parts[i].position) states[i].position.reset();
    }
    return states;
}

int PuppetWorkspace::limitedAngle(const Puppet& puppet, int partIndex, double angle) const {
    const PuppetAttachment* joint = puppetParentAttachment(puppet, puppet.parts[partIndex].name);
    return joint ? PuppetMotion::clampAngle(*joint, angle) : PuppetMotion::freeAngle(angle);
}

QSet<int> PuppetWorkspace::partsInPose(const Puppet& puppet) const {
    QSet<int> parts;
    const int edited = editedPose();
    if (edited < 0) return parts;
    const QStringList names = PuppetEditing::partNames(puppet);
    for (const PuppetPoseSetting& setting : puppet.poses[edited].settings) {
        const int index = names.indexOf(setting.part);
        if (index >= 0) parts.insert(index);
    }
    return parts;
}

std::optional<PuppetCanvas::JointRange> PuppetWorkspace::jointRange(const Puppet& puppet,
                                                                    const QVector<PuppetPartPlacement>& placements) const {
    const int i = m_selection.part;
    if (i < 0 || i >= puppet.parts.size() || !placements[i].placed) return std::nullopt;
    const PuppetPart& part = puppet.parts[i];
    const PuppetAttachment* joint = puppetParentAttachment(puppet, part.name);
    const int parent = joint ? PuppetEditing::partNames(puppet).indexOf(joint->parent) : -1;
    // A part hanging from one that can't be drawn can't be tried either.
    if (joint && (parent < 0 || !placements[parent].placed)) return std::nullopt;

    PuppetCanvas::JointRange range;
    range.attached = joint != nullptr;
    range.point = placements[i].pivot;
    range.angle = shownStates(puppet)[i].angle;
    if (joint) {
        range.angleMin = joint->angleMin;
        range.angleMax = joint->angleMax;
    }
    // The direction the part points in at angle 0: from its joint to its middle.
    const QPixmap image = part.variants.isEmpty() ? QPixmap() : imageOf(spriteFor(part.variants.first()));
    const QPointF middle = placements[i].transform.map(QRectF(image.rect()).center());
    const QPointF towards = middle - range.point;
    const double now = towards.isNull() ? placements[i].totalAngle
                                        : qRadiansToDegrees(std::atan2(towards.y(), towards.x()));
    range.restDirection = now - range.angle;
    if (const std::optional<NamedPoint> marker = joint ? parentMarkerOf(puppet, i) : std::nullopt)
        range.area = placements[parent].transform.map(PuppetMotion::jointArea(*marker, joint->leeway));
    return range;
}

namespace {
/// The setting of part @p name in @p settings, added when the pose doesn't set it yet.
PuppetPoseSetting& settingFor(QVector<PuppetPoseSetting>& settings, const QString& name) {
    for (PuppetPoseSetting& setting : settings) {
        if (setting.part == name) return setting;
    }
    PuppetPoseSetting setting;
    setting.part = name;
    settings.append(setting);
    return settings.last();
}

const PuppetPoseSetting* findSetting(const QVector<PuppetPoseSetting>& settings, const QString& name) {
    for (const PuppetPoseSetting& setting : settings) {
        if (setting.part == name) return &setting;
    }
    return nullptr;
}

/// Keys what pose @p poseIndex newly sets (compared with @p previous) in the other frames
/// of animation @p sequenceIndex that are its own: each keeps the value it showed, given by
/// @p shown (per step, from before the edit). Otherwise a frame that doesn't set a value
/// keeps it from the frame before, and a loop would never go back.
void keyOtherFrames(Puppet& puppet, int sequenceIndex, int poseIndex, const QVector<PuppetPoseSetting>& previous,
                    const QVector<QVector<PuppetPartState>>& shown) {
    const PuppetSequence& sequence = puppet.sequences[sequenceIndex];
    const QVector<PuppetPoseSetting> settings = puppet.poses[poseIndex].settings;
    QSet<int> keyed{poseIndex};
    for (int step = 0; step < sequence.steps.size() && step < shown.size(); ++step) {
        const int pose = PuppetEditing::poseNames(puppet).indexOf(sequence.steps[step].pose);
        if (pose < 0 || keyed.contains(pose) || puppet.poses[pose].owner != sequence.name) continue;
        keyed.insert(pose);
        QVector<PuppetPoseSetting>& frame = puppet.poses[pose].settings;
        for (const PuppetPoseSetting& setting : settings) {
            const int part = PuppetEditing::partNames(puppet).indexOf(setting.part);
            if (part < 0 || part >= shown[step].size()) continue;
            const PuppetPartState& state = shown[step][part];
            const PuppetPoseSetting* before = findSetting(previous, setting.part);
            const PuppetPoseSetting* has = findSetting(frame, setting.part);
            if (!setting.variant.isEmpty() && (!before || before->variant.isEmpty()) && (!has || has->variant.isEmpty())) {
                const QVector<PuppetVariant>& variants = puppet.parts[part].variants;
                if (state.variant >= 0 && state.variant < variants.size())
                    settingFor(frame, setting.part).variant = puppetVariantName(variants[state.variant]);
            }
            if (setting.angle && !(before && before->angle) && !(has && has->angle))
                settingFor(frame, setting.part).angle = int(std::lround(state.angle));
            if (setting.offset && !(before && before->offset) && !(has && has->offset))
                settingFor(frame, setting.part).offset = state.offset.toPoint();
            if (setting.position && !(before && before->position) && !(has && has->position))
                settingFor(frame, setting.part).position =
                    state.position ? state.position->toPoint() : puppet.parts[part].position.value_or(QPoint());
        }
    }
}
}  // namespace

void PuppetWorkspace::editPose(const QString& text, const QString& mergeKey,
                               const std::function<void(const Puppet&, QVector<PuppetPoseSetting>&)>& change) {
    const Puppet* puppet = selectedPuppet();
    int pose = editedPose();
    if (!puppet || pose < 0) return;
    QVector<Puppet> after = puppets();
    Puppet& edited = after[m_selection.puppet];
    QVector<QVector<PuppetPartState>> shownBefore;
    if (m_animating) {
        for (int step = 0; step < puppet->sequences[m_sequence].steps.size(); ++step)
            shownBefore.append(arrivalStates(*puppet, step));
        // A shared pose (or another animation's) is copied first: only this step plays the copy.
        const QString played = puppet->poses[pose].name;
        pose = PuppetEditing::ownStepPose(edited, m_sequence, m_step);
        if (pose < 0) return;
        if (edited.poses[pose].name != played)
            m_copyNote = tr("This step now plays \"%1\", this animation's own copy of \"%2\"")
                             .arg(edited.poses[pose].name, played);
    }
    QVector<PuppetPoseSetting>& settings = edited.poses[pose].settings;
    const QVector<PuppetPoseSetting> previous = settings;
    change(*puppet, settings);
    // Kept in part order, as the pose's lines are written.
    const QStringList names = PuppetEditing::partNames(*puppet);
    std::stable_sort(settings.begin(), settings.end(), [&names](const PuppetPoseSetting& a, const PuppetPoseSetting& b) {
        return names.indexOf(a.part) < names.indexOf(b.part);
    });
    if (m_animating) keyOtherFrames(edited, m_sequence, pose, previous, shownBefore);
    if (after == puppets()) return;
    pushEdit(text, after, m_selection, mergeKey);
}

void PuppetWorkspace::setPartAngle(int partIndex, double angle, const QString& mergeKey) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || partIndex < 0 || partIndex >= puppet->parts.size() || lockedInPose(*puppet, partIndex)) return;
    const int limited = limitedAngle(*puppet, partIndex, angle);
    const QString name = puppet->parts[partIndex].name;
    editPose(tr("Turn Part"), mergeKey, [&](const Puppet&, QVector<PuppetPoseSetting>& settings) {
        settingFor(settings, name).angle = limited;
    });
    if (partIndex == m_selection.part) m_properties->setAngle(limited);
}

void PuppetWorkspace::setPartScale(int partIndex, const QPointF& scale, const QString& mergeKey) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || partIndex < 0 || partIndex >= puppet->parts.size() || lockedInPose(*puppet, partIndex)) return;
    const QPointF limited(qBound(-PUPPET_MAX_SCALE, scale.x(), PUPPET_MAX_SCALE),
                          qBound(-PUPPET_MAX_SCALE, scale.y(), PUPPET_MAX_SCALE));
    const QString name = puppet->parts[partIndex].name;
    editPose(tr("Scale Part"), mergeKey, [&](const Puppet&, QVector<PuppetPoseSetting>& settings) {
        settingFor(settings, name).scale = limited;
    });
}

void PuppetWorkspace::setPartOpacity(int partIndex, double opacity, const QString& mergeKey) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || partIndex < 0 || partIndex >= puppet->parts.size() || lockedInPose(*puppet, partIndex)) return;
    const QString name = puppet->parts[partIndex].name;
    editPose(tr("Fade Part"), mergeKey, [&](const Puppet&, QVector<PuppetPoseSetting>& settings) {
        settingFor(settings, name).opacity = qBound(0.0, opacity, 1.0);
    });
}

void PuppetWorkspace::setPartTint(int partIndex, const QColor& tint) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || partIndex < 0 || partIndex >= puppet->parts.size() || !tint.isValid()
        || lockedInPose(*puppet, partIndex))
        return;
    const QString name = puppet->parts[partIndex].name;
    // sprat-cli takes #RRGGBB: no alpha, 8 bits a channel.
    const QColor color(tint.rgb());
    editPose(tr("Tint Part"), QString(), [&](const Puppet&, QVector<PuppetPoseSetting>& settings) {
        settingFor(settings, name).tint = color;
    });
}

void PuppetWorkspace::driftPart(int partIndex, const QPointF& sceneOffset, const QString& mergeKey) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || partIndex < 0 || partIndex >= puppet->parts.size() || lockedInPose(*puppet, partIndex)) return;
    const PuppetAttachment* joint = puppetParentAttachment(*puppet, puppet->parts[partIndex].name);
    const std::optional<NamedPoint> marker = parentMarkerOf(*puppet, partIndex);
    const int parent = joint ? PuppetEditing::partNames(*puppet).indexOf(joint->parent) : -1;
    if (!marker || parent < 0) return;
    // The offset is in the parent's sprite pixels, so it turns with the parent.
    const QVector<PuppetPartPlacement> placements =
        PuppetEvaluator::evaluate(*puppet, shownStates(*puppet), spriteLookup());
    const QTransform toParent = placements[parent].transform.inverted();
    const QPointF delta = toParent.map(sceneOffset) - toParent.map(QPointF(0, 0));
    const QPoint offset = PuppetMotion::clampOffset(*marker, joint->leeway, m_turnStart.offset + delta);
    const QString name = puppet->parts[partIndex].name;
    editPose(tr("Move Joint"), mergeKey, [&](const Puppet&, QVector<PuppetPoseSetting>& settings) {
        settingFor(settings, name).offset = offset;
    });
}

void PuppetWorkspace::pullPart(int partIndex, const QPointF& grip, const QPointF& target, const QString& mergeKey) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || partIndex < 0 || partIndex >= puppet->parts.size() || lockedInPose(*puppet, partIndex)) return;
    const QVector<PuppetPartState> before = shownStates(*puppet);
    // The parts locked in the pose hold still: the pull bends only the joints below them.
    QSet<int> locked;
    for (int i = 0; i < puppet->parts.size(); ++i) {
        if (lockedInPose(*puppet, i)) locked.insert(i);
    }
    const QVector<PuppetPartState> after = PuppetMotion::reach(
        *puppet, before, partIndex, grip, target, spriteLookup(),
        [this, puppet](int part) { return parentMarkerOf(*puppet, part); }, locked);
    editPose(tr("Pull Part"), mergeKey, [&](const Puppet& posed, QVector<PuppetPoseSetting>& settings) {
        // Only what the pull changed joins the pose.
        for (int i = 0; i < posed.parts.size(); ++i) {
            if (after[i].angle != before[i].angle)
                settingFor(settings, posed.parts[i].name).angle = int(std::lround(after[i].angle));
            if (after[i].offset != before[i].offset)
                settingFor(settings, posed.parts[i].name).offset =
                    QPoint(int(std::lround(after[i].offset.x())), int(std::lround(after[i].offset.y())));
        }
    });
}

void PuppetWorkspace::setPartVariant(int partIndex, int variantIndex) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || partIndex < 0 || partIndex >= puppet->parts.size()) return;
    const PuppetPart& part = puppet->parts[partIndex];
    if (variantIndex < 0 || variantIndex >= part.variants.size() || lockedInPose(*puppet, partIndex)) return;
    const QString variant = puppetVariantName(part.variants[variantIndex]);
    editPose(tr("Show Variant"), QString(), [&](const Puppet&, QVector<PuppetPoseSetting>& settings) {
        settingFor(settings, part.name).variant = variant;
    });
}

void PuppetWorkspace::showVariantInPose(const PuppetSelection& variant) {
    m_selection = variant;
    const Puppet* puppet = selectedPuppet();
    if (!puppet || editedPose() < 0 || variant.part < 0
        || variant.part >= puppet->parts.size() || variant.variant < 0
        || variant.variant >= puppet->parts[variant.part].variants.size()) return;
    const QString name = puppetVariantName(puppet->parts[variant.part].variants[variant.variant]);
    setPartVariant(variant.part, variant.variant);
    // On Rest it became the rest variant, the first: keep it selected wherever it went.
    const int now = PuppetEditing::variantNames(selectedPuppet()->parts[variant.part]).indexOf(name);
    select({variant.puppet, variant.part, now});
}

void PuppetWorkspace::setPartInPose(int partIndex, bool included) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || partIndex < 0 || partIndex >= puppet->parts.size() || lockedInPose(*puppet, partIndex)) {
        updatePoseParts();  // puts the tick back
        return;
    }
    const QString name = puppet->parts[partIndex].name;
    const PuppetPartState shown = shownStates(*puppet)[partIndex];
    editPose(included ? tr("Add Part to Pose") : tr("Remove Part from Pose"), QString(),
             [&](const Puppet&, QVector<PuppetPoseSetting>& settings) {
        settings.removeIf([&name](const PuppetPoseSetting& setting) { return setting.part == name; });
        // A pose line must set something: a part added as it is sets its angle (its rest one).
        if (included) settingFor(settings, name).angle = int(std::lround(shown.angle));
    });
}

void PuppetWorkspace::movePositionedPart(int partIndex, const QPoint& position, const QString& mergeKey) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || partIndex < 0 || partIndex >= puppet->parts.size() || !puppet->parts[partIndex].position
        || lockedInPose(*puppet, partIndex))
        return;
    // The Rest pose places a positioned part where the rig does, so moving it there moves the rig.
    if (!m_animating && (m_pose < 0 || puppetIsRestPose(*puppet, m_pose))) {
        if (puppet->parts[partIndex].rigLocked) return;
        if (partIndex == m_selection.part) setSelectedPartPosition(position, mergeKey);
        return;
    }
    const QString name = puppet->parts[partIndex].name;
    editPose(tr("Move Part"), mergeKey, [&](const Puppet&, QVector<PuppetPoseSetting>& settings) {
        settingFor(settings, name).position = position;
    });
}

QPoint PuppetWorkspace::shownPosition(const Puppet& puppet, int partIndex) const {
    const PuppetPartState state = shownStates(puppet)[partIndex];
    if (state.position)
        return QPoint(int(std::lround(state.position->x())), int(std::lround(state.position->y())));
    return puppet.parts[partIndex].position.value_or(QPoint());
}

bool PuppetWorkspace::lockedInPose(const Puppet& puppet, int partIndex) const {
    const int pose = editedPose();
    return pose >= 0 && pose < puppet.poses.size() && partIndex >= 0 && partIndex < puppet.parts.size()
        && puppet.poses[pose].lockedParts.contains(puppet.parts[partIndex].name);
}

void PuppetWorkspace::setSelectedPartRigLocked(bool locked) {
    if (!selectedPuppet() || m_selection.part < 0) return;
    QVector<Puppet> after = puppets();
    after[m_selection.puppet].parts[m_selection.part].rigLocked = locked;
    if (after == puppets()) return;
    pushEdit(locked ? tr("Lock Rig") : tr("Unlock Rig"), after, m_selection);
}

void PuppetWorkspace::setSelectedPartPoseLocked(bool locked) {
    const Puppet* puppet = selectedPuppet();
    const int pose = editedPose();
    if (!puppet || m_selection.part < 0 || pose < 0) return;
    // The lock is the editor's: it goes on the pose itself, never on a copy for a step.
    QVector<Puppet> after = puppets();
    QStringList& lockedParts = after[m_selection.puppet].poses[pose].lockedParts;
    const QString name = puppet->parts[m_selection.part].name;
    lockedParts.removeAll(name);
    if (locked) lockedParts.append(name);
    if (after == puppets()) return;
    pushEdit(locked ? tr("Lock Part in Pose") : tr("Unlock Part in Pose"), after, m_selection);
}

void PuppetWorkspace::setSelectedPartHiddenMarkers(const QStringList& hiddenMarkers) {
    if (!selectedPuppet() || m_selection.part < 0 || selectedPuppet()->parts[m_selection.part].rigLocked) return;
    QVector<Puppet> after = puppets();
    after[m_selection.puppet].parts[m_selection.part].hiddenMarkers = hiddenMarkers;
    if (after == puppets()) return;
    pushEdit(tr("Show Markers"), after, m_selection);
}

void PuppetWorkspace::selectPose(int poseIndex) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet) return;
    m_pose = poseIndex >= 0 && poseIndex < PuppetEditing::sharedPoseCount(*puppet) ? poseIndex : -1;
    if (m_poseStrip->selectedFrame() != m_pose || !m_poseStrip->currentItem()) {
        const QSignalBlocker blocker(m_poseStrip);
        m_poseStrip->setCurrentRow(m_pose);
    }
    rebuildTree();  // marks the variants the pose shows
    updateCanvas();
    updateProperties();
    updatePoseParts();
    updatePoseButtons();
}

void PuppetWorkspace::addPose(int poseIndex) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet) return;
    QVector<Puppet> after = puppets();
    Puppet& posed = after[m_selection.puppet];
    PuppetPose pose;
    pose.name = PuppetEditing::uniqueName(tr("pose"), PuppetEditing::poseNames(posed));
    // Nothing goes before Rest.
    const int first = puppetIsRestPose(posed, 0) ? 1 : 0;
    const int index = qBound(first, poseIndex, PuppetEditing::sharedPoseCount(posed));
    posed.poses.insert(index, pose);
    pushEdit(tr("New Pose"), after, m_selection);
    selectPose(index);
    updatePoseStrip();
}

void PuppetWorkspace::duplicatePose(int poseIndex) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || poseIndex < 0 || poseIndex >= puppet->poses.size()) return;
    QVector<Puppet> after = puppets();
    Puppet& posed = after[m_selection.puppet];
    PuppetPose copy = posed.poses[poseIndex];
    copy.name = PuppetEditing::uniqueName(copy.name, PuppetEditing::poseNames(posed));
    const int index = poseIndex + 1;
    posed.poses.insert(index, copy);
    pushEdit(tr("Duplicate Pose"), after, m_selection);
    selectPose(index);
    updatePoseStrip();
}

void PuppetWorkspace::removePose(int poseIndex) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || poseIndex < 0 || poseIndex >= puppet->poses.size()) return;
    if (puppetIsRestPose(*puppet, poseIndex)) {
        m_poseStatus->setText(tr("The Rest pose can't be deleted: animations go back to it"));
        return;
    }
    QVector<Puppet> after = puppets();
    PuppetEditing::removePose(after[m_selection.puppet], poseIndex);
    pushEdit(tr("Remove Pose"), after, m_selection);
    // The frame before it is shown instead.
    selectPose(poseIndex - 1);
    updatePoseStrip();
}

void PuppetWorkspace::renamePose(int poseIndex, const QString& name) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || poseIndex < 0 || poseIndex >= puppet->poses.size()) return;
    const QString problem = PuppetEditing::poseNameProblem(*puppet, poseIndex, name);
    if (!problem.isEmpty()) {
        updatePoseStrip();  // puts the name back
        m_poseStatus->setText(problem);
        return;
    }
    QVector<Puppet> after = puppets();
    PuppetEditing::renamePose(after[m_selection.puppet], poseIndex, name);
    pushEdit(tr("Rename Pose"), after, m_selection);
}

void PuppetWorkspace::movePose(int from, int to) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet) return;
    QVector<Puppet> after = puppets();
    PuppetEditing::movePose(after[m_selection.puppet], from, to);
    if (after == puppets()) return;
    if (m_pose == from) m_pose = to;
    pushEdit(tr("Move Pose"), after, m_selection);
}

void PuppetWorkspace::updatePoseButtons() {
    const Puppet* puppet = selectedPuppet();
    const bool posing = puppet && m_pose >= 0 && m_pose < puppet->poses.size();
    const bool rest = posing && puppetIsRestPose(*puppet, m_pose);
    m_newPoseButton->setEnabled(puppet != nullptr);
    m_duplicatePoseButton->setEnabled(posing);
    m_removePoseButton->setEnabled(posing && !rest);
    m_poseStatus->setText(rest ? tr("Rest pose: animations return to it. Moving a positioned part here moves the rig")
                               : QString());
}

void PuppetWorkspace::updatePoseParts() {
    const Puppet* puppet = selectedPuppet();
    const int edited = puppet ? editedPose() : -1;
    m_posePartsTitle->setText(edited >= 0 ? tr("<b>Pose \"%1\"</b>").arg(puppet->poses[edited].name.toHtmlEscaped())
                                          : tr("<b>Pose</b>"));
    const QSignalBlocker blocker(m_poseParts);
    m_poseParts->clear();
    if (edited < 0) {
        m_posePartsHint->setText(m_animating ? tr("Click a step of the animation to see the parts its pose sets.")
                                             : tr("Select a pose below the canvas to see the parts it sets."));
        m_posePartsStack->setCurrentWidget(m_posePartsHint);
        return;
    }
    m_posePartsStack->setCurrentWidget(m_poseParts);
    const PuppetPose& pose = puppet->poses[edited];
    for (int i = 0; i < puppet->parts.size(); ++i) {
        const PuppetPart& part = puppet->parts[i];
        const PuppetPoseSetting* setting = nullptr;
        for (const PuppetPoseSetting& candidate : pose.settings) {
            if (candidate.part == part.name) setting = &candidate;
        }
        QStringList sets;
        if (setting) {
            if (setting->angle) sets.append(QStringLiteral("%1°").arg(*setting->angle));
            if (setting->offset) sets.append(tr("joint %1,%2").arg(setting->offset->x()).arg(setting->offset->y()));
            if (setting->position) sets.append(tr("at %1,%2").arg(setting->position->x()).arg(setting->position->y()));
            if (setting->scale) {
                const QPointF& scale = *setting->scale;
                sets.append(scale.x() == scale.y() ? QStringLiteral("×%1").arg(scale.x())
                                                   : QStringLiteral("×%1,%2").arg(scale.x()).arg(scale.y()));
            }
            if (setting->opacity) sets.append(QStringLiteral("%1 %").arg(std::lround(*setting->opacity * 100)));
            if (setting->tint) sets.append(setting->tint->name());
            if (!setting->variant.isEmpty()) sets.append(setting->variant);
        }
        auto* item = new QListWidgetItem(sets.isEmpty() ? part.name
                                                        : QStringLiteral("%1 · %2").arg(part.name, sets.join(QStringLiteral(" · "))),
                                         m_poseParts);
        item->setData(Qt::UserRole, i);
        const bool locked = pose.lockedParts.contains(part.name);
        // A part locked in the pose can't be ticked in or out of it.
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | (locked ? Qt::NoItemFlags : Qt::ItemIsUserCheckable));
        if (locked) item->setIcon(AppIcons::icon(QStringLiteral("lock")));
        item->setCheckState(setting ? Qt::Checked : Qt::Unchecked);
        if (!setting) item->setForeground(palette().color(QPalette::PlaceholderText));
        item->setToolTip(setting ? tr("This pose sets it. Untick to leave it as other poses have it")
                                 : tr("This pose leaves it alone. Change it on the canvas, or tick it, to set it"));
        if (i == m_selection.part) m_poseParts->setCurrentItem(item);
    }
}

void PuppetWorkspace::updatePoseStrip() {
    const Puppet* puppet = selectedPuppet();
    if (!puppet) {
        m_pose = -1;
        m_poseStrip->showFrames({}, {}, -1);
        m_poseStrip->setEnabled(false);
        m_poseThumbnails.clear();
        updatePoseButtons();
        updatePoseParts();
        updateSequences();
        return;
    }
    m_poseStrip->setEnabled(true);
    // The strip lists the shared poses, which come first; animations keep theirs.
    const int shared = PuppetEditing::sharedPoseCount(*puppet);
    if (m_pose < 0 || m_pose >= shared) m_pose = shared == 0 ? -1 : 0;
    // Thumbnails show the poses as they look, not where the animation preview's clock is.
    m_stillFrames = true;
    // One frame for all, so the thumbnails compare: what every pose and the rest pose cover.
    QVector<QVector<PuppetPartState>> poseStates;
    for (const PuppetPose& pose : puppet->poses) poseStates.append(PuppetPosing::statesOf(*puppet, pose));
    QRectF frame;
    const auto cover = [&](const QVector<PuppetPartState>& states) {
        const QVector<PuppetPartPlacement> placements = PuppetEvaluator::evaluate(*puppet, states, spriteLookup());
        for (int i = 0; i < puppet->parts.size(); ++i) {
            if (!placements[i].placed || puppet->parts[i].variants.isEmpty()) continue;
            const int variant = i < states.size() ? states[i].variant : 0;
            const QPixmap image = imageOf(spriteFor(puppet->parts[i].variants.value(variant)));
            frame |= placements[i].transform.mapRect(QRectF(image.rect()));
        }
    };
    cover({});
    for (const QVector<PuppetPartState>& states : poseStates) cover(states);
    QVector<QPixmap> thumbnails;
    for (const QVector<PuppetPartState>& states : poseStates) thumbnails.append(renderThumbnail(*puppet, states, frame));
    m_poseThumbnails = thumbnails;
    m_stillFrames = false;
    m_poseStrip->showFrames(PuppetEditing::poseNames(*puppet).mid(0, shared), thumbnails.mid(0, shared), m_pose);
    updatePoseButtons();
    updatePoseParts();
    updateSequences();
}

QPixmap PuppetWorkspace::renderThumbnail(const Puppet& puppet, const QVector<PuppetPartState>& states,
                                         const QRectF& frame) const {
    const int size = PuppetPoseStrip::THUMBNAIL_SIZE;
    QPixmap thumbnail(size, size);
    thumbnail.fill(Qt::transparent);
    if (frame.isEmpty()) return thumbnail;
    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const qreal scale = (size - 4) / qMax(frame.width(), frame.height());
    QTransform fit;
    fit.translate(size / 2.0, size / 2.0);
    fit.scale(scale, scale);
    fit.translate(-frame.center().x(), -frame.center().y());
    const QVector<PuppetPartPlacement> placements = PuppetEvaluator::evaluate(puppet, states, spriteLookup());
    for (int i = 0; i < puppet.parts.size(); ++i) {
        if (!placements[i].placed || puppet.parts[i].variants.isEmpty()) continue;
        const int variant = i < states.size() ? states[i].variant : 0;
        painter.setTransform(placements[i].transform * fit);
        painter.setOpacity(placements[i].opacity);
        painter.drawPixmap(0, 0, PuppetCanvas::tinted(imageOf(spriteFor(puppet.parts[i].variants.value(variant))),
                                                      placements[i].tint));
    }
    return thumbnail;
}


void PuppetWorkspace::fitBottomTabs() {
    for (int i = 0; i < m_bottomPages.size(); ++i) {
        m_bottomPages[i]->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        m_bottomPages[i]->setVisible(i == m_bottomTabs->currentIndex());
    }
}

// === Animations: the puppet's sequences of poses ===

QWidget* PuppetWorkspace::buildAnimationsTab(QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);

    // Left: the animations, ticked to play along with the selected one. The column is as wide
    // as the list, so the buttons under it line up with it.
    auto* listWidget = new QWidget(page);
    listWidget->setFixedWidth(SEQUENCE_LIST_WIDTH);
    auto* listColumn = new QVBoxLayout(listWidget);
    listColumn->setContentsMargins(0, 0, 0, 0);
    auto* listCaption = new QLabel(tr("Tick to play along"), page);
    listCaption->setForegroundRole(QPalette::PlaceholderText);
    listCaption->setToolTip(tr("Ticked animations play with the selected one, as a game would combine them"));
    listColumn->addWidget(listCaption);
    m_sequenceList = new QListWidget(page);
    m_sequenceList->setObjectName(QStringLiteral("puppetAnimations"));
    m_sequenceList->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_sequenceList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_sequenceList->setToolTip(tr("Double-click to rename. Tick others to play them along with the selected one, "
                                  "as a game would combine them"));
    listColumn->addWidget(m_sequenceList, 1);
    auto* listButtons = new QHBoxLayout;
    m_addSequenceButton = new QPushButton(AppIcons::icon(QStringLiteral("add")), tr("Add"), page);
    m_addSequenceButton->setObjectName(QStringLiteral("puppetAddAnimation"));
    m_addSequenceButton->setToolTip(tr("Add an animation after the selected one, then add its poses as steps"));
    UiStyle::setRole(m_addSequenceButton, UiStyle::Role::Add);
    m_removeSequenceButton = new QPushButton(AppIcons::icon(QStringLiteral("delete")), tr("Delete"), page);
    m_removeSequenceButton->setObjectName(QStringLiteral("puppetRemoveAnimation"));
    m_removeSequenceButton->setToolTip(tr("Its own frames go with it; shared poses stay"));
    UiStyle::setRole(m_removeSequenceButton, UiStyle::Role::Destructive);
    for (QPushButton* button : {m_addSequenceButton, m_removeSequenceButton}) {
        // Narrower than their text and icon would ask for, so both fit the list's width.
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        listButtons->addWidget(button);
    }
    listColumn->addLayout(listButtons);
    layout->addWidget(listWidget);

    // Right: the selected animation's settings, its steps and the selected step.
    auto* right = new QVBoxLayout;
    auto* header = new QHBoxLayout;
    m_playButton = new QToolButton(page);
    m_playButton->setObjectName(QStringLiteral("puppetPlay"));
    m_playButton->setCheckable(true);
    m_playButton->setIcon(AppIcons::icon(QStringLiteral("play")));
    m_playButton->setToolTip(tr("Play the animation on the canvas"));
    header->addWidget(m_playButton);
    m_sequenceFps = new QSpinBox(page);
    m_sequenceFps->setObjectName(QStringLiteral("puppetAnimationFps"));
    m_sequenceFps->setRange(1, MAX_ANIMATION_FPS);
    m_sequenceFps->setSuffix(tr(" fps"));
    m_sequenceFps->setKeyboardTracking(false);
    header->addWidget(m_sequenceFps);
    m_sequenceLoop = new QComboBox(page);
    m_sequenceLoop->setObjectName(QStringLiteral("puppetAnimationLoop"));
    m_sequenceLoop->addItem(tr("Loop"), int(AnimationLoopMode::Loop));
    m_sequenceLoop->addItem(tr("Once"), int(AnimationLoopMode::Once));
    m_sequenceLoop->addItem(tr("Ping-pong"), int(AnimationLoopMode::PingPong));
    m_sequenceLoop->addItem(tr("Once ping-pong"), int(AnimationLoopMode::OncePingPong));
    header->addWidget(m_sequenceLoop);
    m_onionSkin = new QCheckBox(tr("Onion skin"), page);
    m_onionSkin->setObjectName(QStringLiteral("puppetOnionSkin"));
    m_onionSkin->setToolTip(tr("Show the steps before (red) and after (green) the edited one, faintly"));
    header->addWidget(m_onionSkin);
    m_sequenceInfo = new QLabel(page);
    m_sequenceInfo->setObjectName(QStringLiteral("puppetAnimationInfo"));
    m_sequenceInfo->setForegroundRole(QPalette::PlaceholderText);
    header->addWidget(m_sequenceInfo, 1);
    right->addLayout(header);

    m_track = new PuppetSequenceTrack(page);
    m_track->setObjectName(QStringLiteral("puppetAnimationSteps"));
    auto* trackScroll = new QScrollArea(page);
    trackScroll->setWidget(m_track);
    trackScroll->setWidgetResizable(true);
    trackScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    trackScroll->setFixedHeight(m_track->sizeHint().height() + trackScroll->horizontalScrollBar()->sizeHint().height()
                                + 2 * trackScroll->frameWidth());
    right->addWidget(trackScroll);

    auto* stepRow = new QHBoxLayout;
    m_stepLabel = new QLabel(page);
    stepRow->addWidget(m_stepLabel);
    m_stepPose = new QComboBox(page);
    m_stepPose->setObjectName(QStringLiteral("puppetStepPose"));
    stepRow->addWidget(m_stepPose, 1);
    const auto makeFrames = [page](const QString& objectName, const QString& prefix, const QString& tip) {
        auto* spin = new QSpinBox(page);
        spin->setObjectName(objectName);
        spin->setRange(0, PUPPET_MAX_STEP_FRAMES);
        spin->setPrefix(prefix);
        spin->setSuffix(tr(" frames"));
        spin->setKeyboardTracking(false);
        spin->setToolTip(tip);
        return spin;
    };
    m_stepIn = makeFrames(QStringLiteral("puppetStepIn"), tr("In: "), tr("Frames to reach the pose from the step before"));
    m_stepHold = makeFrames(QStringLiteral("puppetStepHold"), tr("Hold: "), tr("Frames the pose is held once reached"));
    stepRow->addWidget(m_stepIn);
    stepRow->addWidget(m_stepHold);
    m_stepEase = new QComboBox(page);
    m_stepEase->setObjectName(QStringLiteral("puppetStepEase"));
    m_stepEase->addItem(tr("Linear"));
    m_stepEase->addItem(tr("Step"));
    m_stepEase->setToolTip(tr("Linear moves to the pose over In frames; Step keeps the pose before, then snaps"));
    stepRow->addWidget(m_stepEase);
    right->addLayout(stepRow);
    m_stepNote = new QLabel(page);
    m_stepNote->setObjectName(QStringLiteral("puppetStepNote"));
    m_stepNote->setForegroundRole(QPalette::PlaceholderText);
    m_stepNote->setWordWrap(true);
    right->addWidget(m_stepNote);
    layout->addLayout(right, 1);

    m_playTimer = new QTimer(this);
    connect(m_playTimer, &QTimer::timeout, this, &PuppetWorkspace::advancePlayback);
    connect(m_playButton, &QToolButton::toggled, this, [this](bool playing) {
        setPlaying(playing);
        if (!playing) {
            // Stopped, the playhead goes back into the first pass, where the step under it has
            // arrived: the canvas edits that step. (Playing counts frames on past the loops.)
            const Puppet* puppet = selectedPuppet();
            if (puppet && m_sequence >= 0 && m_sequence < puppet->sequences.size()) {
                const PuppetSequence& sequence = puppet->sequences[m_sequence];
                const int step = PuppetSequencing::stepAt(sequence, PuppetSequencing::passFrameOf(sequence, m_playFrame));
                if (step >= 0) {
                    selectStep(step);
                    return;
                }
            }
        }
        rebuildTree();
        updateCanvas();
        updateProperties();
        updatePoseParts();
    });
    connect(m_onionSkin, &QCheckBox::toggled, this, [this]() { updateCanvas(); });
    connect(m_addSequenceButton, &QPushButton::clicked, this, [this]() { addSequence(m_sequence + 1); });
    connect(m_removeSequenceButton, &QPushButton::clicked, this, [this]() { removeSequence(m_sequence); });
    connect(m_sequenceList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_rebuildingSequences && row >= 0) selectSequence(row);
    });
    connect(m_sequenceList, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (m_rebuildingSequences) return;
        // Both may rebuild the list: they act once Qt is done with the item.
        const int row = m_sequenceList->row(item);
        const QString name = item->text().trimmed();
        const bool along = item->checkState() == Qt::Checked;
        QMetaObject::invokeMethod(this, [this, row, name, along]() {
            const Puppet* puppet = selectedPuppet();
            if (!puppet || row < 0 || row >= puppet->sequences.size()) return;
            if (name != puppet->sequences[row].name) {
                renameSequence(row, name);
            } else if (along != m_playAlong.contains(name)) {
                if (along) m_playAlong.insert(name); else m_playAlong.remove(name);
                updateCanvas();
            }
        }, Qt::QueuedConnection);
    });
    connect(m_sequenceList, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QListWidgetItem* item = m_sequenceList->itemAt(pos);
        const int row = item ? m_sequenceList->row(item) : -1;
        auto* menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->addAction(AppIcons::icon(QStringLiteral("add")), tr("Add Animation"), this,
                        [this, row]() { addSequence(row >= 0 ? row + 1 : int(selectedPuppet()->sequences.size())); });
        if (row >= 0) {
            menu->addAction(AppIcons::icon(QStringLiteral("duplicate")), tr("Duplicate"), this,
                            [this, row]() { duplicateSequence(row); });
            menu->addSeparator();
            menu->addAction(AppIcons::icon(QStringLiteral("delete")), tr("Delete"), this,
                            [this, row]() { removeSequence(row); });
        }
        menu->popup(m_sequenceList->viewport()->mapToGlobal(pos));
    });
    connect(m_sequenceFps, &QSpinBox::valueChanged, this, [this](int fps) {
        editSequence(tr("Animation Speed"), QStringLiteral("fps %1").arg(m_sequence),
                     [fps](PuppetSequence& sequence) { sequence.fps = fps; });
    });
    connect(m_sequenceLoop, &QComboBox::currentIndexChanged, this, [this]() {
        const auto mode = AnimationLoopMode(m_sequenceLoop->currentData().toInt());
        editSequence(tr("Animation Loop"), QString(), [mode](PuppetSequence& sequence) { sequence.loopMode = mode; });
    });
    connect(m_track, &PuppetSequenceTrack::stepSelected, this, &PuppetWorkspace::selectStep);
    connect(m_track, &PuppetSequenceTrack::playheadMoved, this, [this](int frame) {
        setPlaying(false);
        m_playFrame = frame;
        // The step under the playhead is selected: once its pose has arrived, it is edited.
        const Puppet* puppet = selectedPuppet();
        if (puppet && m_sequence >= 0 && m_sequence < puppet->sequences.size()) {
            const int step = PuppetSequencing::stepAt(puppet->sequences[m_sequence], frame);
            if (step != m_step) m_copyNote.clear();
            m_step = step;
        }
        updateSequences();
        rebuildTree();
        updateCanvas();
        updateProperties();
        updatePoseParts();
    });
    connect(m_track, &PuppetSequenceTrack::stepActivated, this, [this](int step) {
        // A shared pose is opened in the Poses tab; the animation's own are edited here.
        const Puppet* puppet = selectedPuppet();
        if (!puppet || m_sequence < 0) return;
        const int pose = PuppetEditing::poseNames(*puppet).indexOf(puppet->sequences[m_sequence].steps[step].pose);
        if (pose < 0 || !puppet->poses[pose].owner.isEmpty()) return;
        m_bottomTabs->setCurrentIndex(POSES_TAB);
        selectPose(pose);
    });
    connect(m_track, &PuppetSequenceTrack::addRequested, this, &PuppetWorkspace::addStep);
    connect(m_track, &PuppetSequenceTrack::newFrameRequested, this, &PuppetWorkspace::addFrame);
    connect(m_track, &PuppetSequenceTrack::savePoseRequested, this, &PuppetWorkspace::askSavePoseFromStep);
    connect(m_track, &PuppetSequenceTrack::moveRequested, this, &PuppetWorkspace::moveStep);
    connect(m_track, &PuppetSequenceTrack::duplicateRequested, this, &PuppetWorkspace::duplicateStep);
    connect(m_track, &PuppetSequenceTrack::removeRequested, this, &PuppetWorkspace::removeStep);
    connect(m_stepPose, &QComboBox::currentIndexChanged, this, [this]() {
        const QString pose = m_stepPose->currentData().toString();
        editStep(tr("Step Pose"), QString(), [pose](PuppetSequenceStep& step) { step.pose = pose; });
    });
    connect(m_stepIn, &QSpinBox::valueChanged, this, [this](int in) {
        editStep(tr("Step Frames"), QStringLiteral("in %1 %2").arg(m_sequence).arg(m_step), [in](PuppetSequenceStep& step) {
            step.in = in;
            if (step.in + step.hold < 1) step.hold = 1;  // a step lasts a frame at least
        });
    });
    connect(m_stepHold, &QSpinBox::valueChanged, this, [this](int hold) {
        editStep(tr("Step Frames"), QStringLiteral("hold %1 %2").arg(m_sequence).arg(m_step), [hold](PuppetSequenceStep& step) {
            step.hold = hold;
            if (step.in + step.hold < 1) step.in = 1;
        });
    });
    connect(m_stepEase, &QComboBox::currentIndexChanged, this, [this](int ease) {
        editStep(tr("Step Ease"), QString(), [ease](PuppetSequenceStep& step) {
            step.snap = ease == 1;
            // Parts that now ease as the whole step does are no longer exceptions.
            step.partSnaps.removeIf([&step](const auto& part) { return part.value() == step.snap; });
        });
    });
    return page;
}

void PuppetWorkspace::selectSequence(int sequenceIndex) {
    const Puppet* puppet = selectedPuppet();
    m_sequence = puppet && sequenceIndex >= 0 && sequenceIndex < puppet->sequences.size() ? sequenceIndex : -1;
    m_step = -1;
    m_playFrame = 0;
    m_copyNote.clear();
    updateSequences();
    rebuildTree();
    updateCanvas();
    updateProperties();
    updatePoseParts();
}

void PuppetWorkspace::selectStep(int stepIndex) {
    if (stepIndex != m_step) m_copyNote.clear();
    m_step = stepIndex;
    const Puppet* puppet = selectedPuppet();
    if (puppet && m_sequence >= 0 && stepIndex >= 0) {
        // The playhead goes where the step's pose has arrived.
        const PuppetSequence& sequence = puppet->sequences[m_sequence];
        const PuppetSequenceStep& step = sequence.steps.value(stepIndex);
        m_playFrame = PuppetSequencing::stepStart(sequence, stepIndex) + std::max(0, step.in - 1);
    }
    updateSequences();
    rebuildTree();
    updateCanvas();
    updateProperties();
    updatePoseParts();
}

void PuppetWorkspace::editSequence(const QString& text, const QString& mergeKey,
                                   const std::function<void(PuppetSequence&)>& change) {
    const Puppet* puppet = selectedPuppet();
    if (m_rebuildingSequences || !puppet || m_sequence < 0 || m_sequence >= puppet->sequences.size()) return;
    QVector<Puppet> after = puppets();
    change(after[m_selection.puppet].sequences[m_sequence]);
    // A frame no step plays any more goes with it.
    PuppetEditing::removeUnusedAnimationPoses(after[m_selection.puppet]);
    if (after == puppets()) return;
    pushEdit(text, after, m_selection, mergeKey);
}

void PuppetWorkspace::setSelectedPartStepEase(std::optional<bool> snaps) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || m_selection.part < 0) return;
    const QString part = puppet->parts[m_selection.part].name;
    editStep(tr("Part Ease"), QString(), [&part, snaps](PuppetSequenceStep& step) {
        // Kept only when it differs from the step's, as sprat-cli writes it.
        if (snaps && *snaps != step.snap) step.partSnaps.insert(part, *snaps);
        else step.partSnaps.remove(part);
    });
}

void PuppetWorkspace::editStep(const QString& text, const QString& mergeKey,
                               const std::function<void(PuppetSequenceStep&)>& change) {
    const int step = m_step;
    editSequence(text, mergeKey, [&](PuppetSequence& sequence) {
        if (step >= 0 && step < sequence.steps.size()) change(sequence.steps[step]);
    });
}

void PuppetWorkspace::addSequence(int sequenceIndex) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet) return;
    QVector<Puppet> after = puppets();
    Puppet& animated = after[m_selection.puppet];
    PuppetSequence sequence;
    sequence.name = PuppetEditing::uniqueName(tr("animation"), PuppetEditing::sequenceNames(animated));
    sequence.fps = m_newAnimationFps;
    const int index = std::clamp(sequenceIndex, 0, int(animated.sequences.size()));
    animated.sequences.insert(index, sequence);
    pushEdit(tr("New Animation"), after, m_selection);
    selectSequence(index);
}

void PuppetWorkspace::duplicateSequence(int sequenceIndex) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || sequenceIndex < 0 || sequenceIndex >= puppet->sequences.size()) return;
    QVector<Puppet> after = puppets();
    PuppetEditing::duplicateSequence(after[m_selection.puppet], sequenceIndex);
    pushEdit(tr("Duplicate Animation"), after, m_selection);
    selectSequence(sequenceIndex + 1);
}

void PuppetWorkspace::removeSequence(int sequenceIndex) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || sequenceIndex < 0 || sequenceIndex >= puppet->sequences.size()) return;
    QVector<Puppet> after = puppets();
    PuppetEditing::removeSequence(after[m_selection.puppet], sequenceIndex);
    pushEdit(tr("Remove Animation"), after, m_selection);
    selectSequence(std::min(sequenceIndex, int(after[m_selection.puppet].sequences.size()) - 1));
}

void PuppetWorkspace::renameSequence(int sequenceIndex, const QString& name) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || sequenceIndex < 0 || sequenceIndex >= puppet->sequences.size()) return;
    const QString problem = PuppetEditing::sequenceNameProblem(*puppet, sequenceIndex, name);
    if (!problem.isEmpty()) {
        updateSequences();  // puts the name back
        m_sequenceInfo->setText(problem);
        return;
    }
    const QString old = puppet->sequences[sequenceIndex].name;
    QVector<Puppet> after = puppets();
    PuppetEditing::renameSequence(after[m_selection.puppet], sequenceIndex, name);
    if (m_playAlong.remove(old)) m_playAlong.insert(name);
    pushEdit(tr("Rename Animation"), after, m_selection);
}

void PuppetWorkspace::addStep(int stepIndex, const QString& pose) {
    const int sequence = m_sequence;
    int added = -1;
    editSequence(tr("Add Pose to Animation"), QString(), [&](PuppetSequence& edited) {
        PuppetSequenceStep step;
        step.pose = pose;
        step.in = m_newStep.in;
        step.hold = m_newStep.hold;
        added = std::clamp(stepIndex, 0, int(edited.steps.size()));
        edited.steps.insert(added, step);
    });
    if (added >= 0 && sequence == m_sequence) selectStep(added);
}

void PuppetWorkspace::moveStep(int from, int to) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || m_sequence < 0) return;
    QVector<Puppet> after = puppets();
    PuppetEditing::moveStep(after[m_selection.puppet], m_sequence, from, to);
    if (after == puppets()) return;
    pushEdit(tr("Move Pose in Animation"), after, m_selection);
    if (m_step == from) selectStep(to);
}

void PuppetWorkspace::duplicateStep(int stepIndex) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || m_sequence < 0 || m_sequence >= puppet->sequences.size()) return;
    if (stepIndex < 0 || stepIndex >= puppet->sequences[m_sequence].steps.size()) return;
    QVector<Puppet> after = puppets();
    // A frame of this animation's own is copied with its pose, to be changed apart.
    PuppetEditing::duplicateStep(after[m_selection.puppet], m_sequence, stepIndex);
    pushEdit(tr("Duplicate Pose in Animation"), after, m_selection);
    selectStep(stepIndex + 1);
}

void PuppetWorkspace::addFrame(int stepIndex, int copyOf) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || m_sequence < 0 || m_sequence >= puppet->sequences.size()) return;
    QVector<Puppet> after = puppets();
    Puppet& animated = after[m_selection.puppet];
    const QVector<PuppetSequenceStep>& steps = animated.sequences[m_sequence].steps;
    PuppetSequenceStep step;
    QVector<PuppetPoseSetting> settings;
    if (copyOf >= 0 && copyOf < steps.size()) {
        // From the neighbour, so the new frame starts where it is and is changed from there;
        // its timing is a new step's, from the settings.
        step = steps[copyOf];
        const int pose = PuppetEditing::poseNames(animated).indexOf(step.pose);
        if (pose >= 0) settings = animated.poses[pose].settings;
    }
    step.in = m_newStep.in;
    step.hold = m_newStep.hold;
    step.pose = PuppetEditing::addAnimationPose(animated, m_sequence, settings);
    const int index = std::clamp(stepIndex, 0, int(steps.size()));
    animated.sequences[m_sequence].steps.insert(index, step);
    pushEdit(tr("New Frame"), after, m_selection);
    selectStep(index);
}

void PuppetWorkspace::askSavePoseFromStep(int stepIndex) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || m_sequence < 0 || m_sequence >= puppet->sequences.size()) return;
    QString name = PuppetEditing::uniqueName(tr("pose"), PuppetEditing::poseNames(*puppet));
    for (;;) {
        bool accepted = false;
        name = QInputDialog::getText(this, tr("Save as Pose"),
                                     tr("The pose is copied to the Poses tab, where any animation can play it.\nName:"),
                                     QLineEdit::Normal, name, &accepted).trimmed();
        if (!accepted) return;
        const QString problem = savePoseFromStep(stepIndex, name);
        if (problem.isEmpty()) return;
        MessageDialog::warning(this, tr("Can't Save the Pose"), problem);
    }
}

QString PuppetWorkspace::savePoseFromStep(int stepIndex, const QString& name) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || m_sequence < 0 || m_sequence >= puppet->sequences.size()) return tr("Select an animation.");
    const QVector<PuppetSequenceStep>& steps = puppet->sequences[m_sequence].steps;
    if (stepIndex < 0 || stepIndex >= steps.size()) return tr("Select a step.");
    const int pose = PuppetEditing::poseNames(*puppet).indexOf(steps[stepIndex].pose);
    if (pose < 0) return tr("The step plays no pose this puppet has.");
    const QString problem = PuppetEditing::poseNameProblem(*puppet, -1, name);
    if (!problem.isEmpty()) return problem;
    QVector<Puppet> after = puppets();
    PuppetEditing::addSharedCopy(after[m_selection.puppet], pose, name);
    pushEdit(tr("Save as Pose"), after, m_selection);
    m_copyNote = tr("Saved as pose \"%1\" in the Poses tab").arg(name);
    updateSequences();
    return {};
}

void PuppetWorkspace::removeStep(int stepIndex) {
    bool removed = false;
    editSequence(tr("Remove Pose from Animation"), QString(), [&](PuppetSequence& sequence) {
        if (stepIndex < 0 || stepIndex >= sequence.steps.size()) return;
        sequence.steps.removeAt(stepIndex);
        removed = true;
    });
    if (!removed) return;
    const int remaining = selectedPuppet()->sequences[m_sequence].steps.size();
    selectStep(remaining == 0 ? -1 : std::min(stepIndex, remaining - 1));
}

void PuppetWorkspace::setPlaying(bool playing) {
    const Puppet* puppet = selectedPuppet();
    const bool playable = puppet && m_sequence >= 0 && m_sequence < puppet->sequences.size()
                       && !puppet->sequences[m_sequence].steps.isEmpty();
    playing = playing && playable;
    if (playing) {
        const PuppetSequence& sequence = puppet->sequences[m_sequence];
        const bool once = sequence.loopMode == AnimationLoopMode::Once
                       || sequence.loopMode == AnimationLoopMode::OncePingPong;
        // A finished animation that plays once starts again.
        if (once && m_playFrame >= PuppetSequencing::cycleFrames(sequence) - 1) m_playFrame = 0;
        m_playTimer->start(1000 / sanitizedAnimationFps(sequence.fps));
    } else {
        m_playTimer->stop();
    }
    const QSignalBlocker blocker(m_playButton);
    m_playButton->setChecked(playing);
    m_playButton->setIcon(AppIcons::icon(playing ? QStringLiteral("pause") : QStringLiteral("play")));
}

int PuppetWorkspace::playInterval() const {
    return m_playTimer->isActive() ? m_playTimer->interval() : -1;
}

void PuppetWorkspace::advancePlayback() {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || m_sequence < 0 || m_sequence >= puppet->sequences.size()) {
        setPlaying(false);
        return;
    }
    const PuppetSequence& sequence = puppet->sequences[m_sequence];
    const int cycle = PuppetSequencing::cycleFrames(sequence);
    const bool once = sequence.loopMode == AnimationLoopMode::Once || sequence.loopMode == AnimationLoopMode::OncePingPong;
    ++m_playFrame;
    if (once && m_playFrame >= cycle - 1) {
        m_playFrame = cycle - 1;
        setPlaying(false);
    }
    updatePlayhead();
    updateCanvas();
}

void PuppetWorkspace::updatePlayhead() {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || m_sequence < 0 || m_sequence >= puppet->sequences.size()) {
        m_track->setPlayhead(-1);
        return;
    }
    const PuppetSequence& sequence = puppet->sequences[m_sequence];
    const int cycle = std::max(1, PuppetSequencing::cycleFrames(sequence));
    m_track->setPlayhead(PuppetSequencing::passFrameOf(sequence, m_playFrame));
    m_sequenceInfo->setText(tr("Frame %1 of %2 · %3 s").arg(m_playFrame % cycle + 1).arg(cycle)
                                .arg(QString::number(double(cycle) / sanitizedAnimationFps(sequence.fps), 'g', 3)));
}

bool PuppetWorkspace::editingStep() const {
    const Puppet* puppet = selectedPuppet();
    if (!m_animating || !puppet || m_playTimer->isActive() || m_sequence < 0 || m_sequence >= puppet->sequences.size())
        return false;
    const PuppetSequence& sequence = puppet->sequences[m_sequence];
    if (m_step < 0 || m_step >= sequence.steps.size()) return false;
    if (!PuppetEditing::poseNames(*puppet).contains(sequence.steps[m_step].pose)) return false;
    // From where the pose arrives to the step's end, in the first pass: there the canvas shows it.
    const PuppetSequenceStep& step = sequence.steps[m_step];
    const int start = PuppetSequencing::stepStart(sequence, m_step);
    return m_playFrame >= start + std::max(0, step.in - 1) && m_playFrame < start + PuppetSequencing::stepFrames(step);
}

int PuppetWorkspace::editedPose() const {
    const Puppet* puppet = selectedPuppet();
    if (!puppet) return -1;
    if (!m_animating) return m_pose >= 0 && m_pose < puppet->poses.size() ? m_pose : -1;
    if (!editingStep()) return -1;
    return PuppetEditing::poseNames(*puppet).indexOf(puppet->sequences[m_sequence].steps[m_step].pose);
}

QString PuppetWorkspace::partEditKey(const QString& field) const {
    if (m_animating)
        return QStringLiteral("step %1 %2 %3 %4 %5").arg(field).arg(m_selection.puppet).arg(m_sequence).arg(m_step)
            .arg(m_selection.part);
    return QStringLiteral("%1 %2 %3 %4").arg(field).arg(m_selection.puppet).arg(m_pose).arg(m_selection.part);
}

QVector<PuppetPartState> PuppetWorkspace::arrivalStates(const Puppet& puppet, int stepIndex) const {
    const PuppetSequence& sequence = puppet.sequences[m_sequence];
    const int frame = PuppetSequencing::stepStart(sequence, stepIndex) + std::max(0, sequence.steps[stepIndex].in - 1);
    return limitedStates(puppet, PuppetSequencing::statesAt(puppet, sequence, frame, {}));
}

QVector<PuppetCanvas::OnionSkin> PuppetWorkspace::onionSkins(const Puppet& puppet) const {
    QVector<PuppetCanvas::OnionSkin> skins;
    if (!m_onionSkin || !m_onionSkin->isChecked() || !editingStep()) return skins;
    const QVector<PuppetSequenceStep>& steps = puppet.sequences[m_sequence].steps;
    const PuppetSpriteLookup lookup = spriteLookup();
    // The step before in red, the one after in green (by default), as animators' light tables tint them.
    const PuppetCanvas::CanvasSettings& look = m_canvas->canvasSettings();
    const QVector<QPair<int, QColor>> neighbours{{m_step - 1, look.onionBeforeColor}, {m_step + 1, look.onionAfterColor}};
    for (const auto& [step, tint] : neighbours) {
        if (step < 0 || step >= steps.size()) continue;
        const QVector<PuppetPartState> states = arrivalStates(puppet, step);
        const QVector<PuppetPartPlacement> placements = PuppetEvaluator::evaluate(puppet, states, lookup);
        PuppetCanvas::OnionSkin skin;
        skin.tint = tint;
        for (int i = 0; i < puppet.parts.size(); ++i) {
            if (!placements[i].placed || puppet.parts[i].variants.isEmpty()) continue;
            const SpritePtr sprite = spriteFor(puppet.parts[i].variants.value(states[i].variant));
            skin.parts.append({imageOf(sprite), placements[i].transform, placements[i].opacity});
        }
        skins.append(skin);
    }
    return skins;
}

QVector<PuppetPartState> PuppetWorkspace::animationStates(const Puppet& puppet) const {
    QVector<PuppetPartState> states(puppet.parts.size());
    if (m_sequence < 0 || m_sequence >= puppet.sequences.size()) return states;
    // The others play along on the same clock, each at its own fps, in list order: the
    // later one wins where two set the same part.
    const double seconds = double(m_playFrame) / sanitizedAnimationFps(puppet.sequences[m_sequence].fps);
    for (int i = 0; i < puppet.sequences.size(); ++i) {
        const PuppetSequence& sequence = puppet.sequences[i];
        if (i != m_sequence && !m_playAlong.contains(sequence.name)) continue;
        const int frame = i == m_sequence ? m_playFrame : int(seconds * sanitizedAnimationFps(sequence.fps));
        states = PuppetSequencing::statesAt(puppet, sequence, frame, states);
    }
    return limitedStates(puppet, states);
}

void PuppetWorkspace::updateSequences() {
    const Puppet* puppet = selectedPuppet();
    m_rebuildingSequences = true;
    m_sequenceList->clear();
    if (puppet && m_sequence >= puppet->sequences.size()) m_sequence = puppet->sequences.size() - 1;
    if (puppet && m_sequence < 0 && !puppet->sequences.isEmpty()) m_sequence = 0;
    if (!puppet) m_sequence = -1;
    if (puppet) {
        for (const PuppetSequence& sequence : puppet->sequences) {
            auto* item = new QListWidgetItem(sequence.name, m_sequenceList);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
            item->setCheckState(m_playAlong.contains(sequence.name) ? Qt::Checked : Qt::Unchecked);
        }
        m_sequenceList->setCurrentRow(m_sequence);
    }
    const bool hasSequence = m_sequence >= 0;
    const PuppetSequence* sequence = hasSequence ? &puppet->sequences[m_sequence] : nullptr;
    if (sequence && m_step >= sequence->steps.size()) m_step = sequence->steps.size() - 1;
    m_removeSequenceButton->setEnabled(hasSequence);
    m_addSequenceButton->setEnabled(puppet != nullptr);
    m_playButton->setEnabled(sequence && !sequence->steps.isEmpty());
    // Playing on, at the speed the animation has now; nothing left to play stops it.
    if (m_playTimer->isActive()) {
        if (sequence && !sequence->steps.isEmpty()) m_playTimer->setInterval(1000 / sanitizedAnimationFps(sequence->fps));
        else setPlaying(false);
    }
    m_sequenceFps->setEnabled(hasSequence);
    m_sequenceLoop->setEnabled(hasSequence);
    m_sequenceFps->setValue(sequence ? sanitizedAnimationFps(sequence->fps) : DEFAULT_ANIMATION_FPS);
    m_sequenceLoop->setCurrentIndex(std::max(0, m_sequenceLoop->findData(int(sequence ? sequence->loopMode
                                                                                        : AnimationLoopMode::Loop))));

    // The steps, with the poses' thumbnails. Steps may play the shared poses and the
    // animation's own.
    QVector<PuppetSequenceTrack::Block> blocks;
    const QStringList poses = puppet ? PuppetEditing::poseNames(*puppet) : QStringList();
    const QStringList playable = puppet ? PuppetEditing::posesFor(*puppet, m_sequence) : QStringList();
    if (sequence) {
        for (const PuppetSequenceStep& step : sequence->steps) {
            const int pose = poses.indexOf(step.pose);
            blocks.append({step.pose, step.in, step.hold, step.snap, m_poseThumbnails.value(pose), pose < 0,
                           pose >= 0 && puppet->poses[pose].owner != sequence->name});
        }
    }
    m_track->setEnabled(hasSequence);
    m_track->showSteps(blocks, m_step, playable);

    const bool hasStep = sequence && m_step >= 0 && m_step < sequence->steps.size();
    m_stepLabel->setText(hasStep ? tr("Step %1:").arg(m_step + 1) : tr("Select a step to change it."));
    for (QWidget* field : {static_cast<QWidget*>(m_stepPose), static_cast<QWidget*>(m_stepIn),
                           static_cast<QWidget*>(m_stepHold), static_cast<QWidget*>(m_stepEase)})
        field->setVisible(hasStep);
    if (hasStep) {
        const PuppetSequenceStep& step = sequence->steps[m_step];
        m_stepPose->clear();
        for (const QString& pose : playable) m_stepPose->addItem(pose, pose);
        if (m_stepPose->findData(step.pose) < 0) m_stepPose->addItem(QStringLiteral("⚠ %1").arg(step.pose), step.pose);
        m_stepPose->setCurrentIndex(m_stepPose->findData(step.pose));
        m_stepIn->setValue(step.in);
        m_stepHold->setValue(step.hold);
        m_stepEase->setCurrentIndex(step.snap ? 1 : 0);
    }
    QString note;
    if (hasStep) {
        const int pose = poses.indexOf(sequence->steps[m_step].pose);
        if (!m_copyNote.isEmpty()) note = m_copyNote;
        else if (pose < 0) note = tr("This puppet has no pose of that name.");
        else if (puppet->poses[pose].owner == sequence->name)
            note = tr("A frame of this animation: changing it on the canvas changes no other animation.");
        else
            note = tr("A shared pose: changing it here gives this step its own copy; the pose stays as it is.");
    }
    m_stepNote->setText(note);
    m_stepNote->setVisible(hasStep);
    m_rebuildingSequences = false;
    updatePlayhead();
    if (!hasSequence) m_sequenceInfo->setText(puppet ? tr("Add an animation: a sequence of poses.") : QString());
}
