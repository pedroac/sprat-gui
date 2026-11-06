#include "PuppetWorkspace.h"
#include "MessageDialog.h"
#include "ProjectSession.h"
#include "PuppetCanvas.h"
#include "NewPuppetDialog.h"
#include "PuppetEditing.h"
#include "PuppetMotion.h"
#include "PuppetPropertiesPanel.h"
#include "PuppetTreeWidget.h"
#include "SpritePickerDialog.h"

#include <cmath>
#include <QAction>
#include <QtMath>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
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

constexpr int NAME_COLUMN = 0;
constexpr int DETAIL_COLUMN = 1;

/// A star icon for the rest variant, drawn from the font so it needs no resource.
QIcon restVariantIcon(const QPalette& palette) {
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

    m_tree = new PuppetTreeWidget(left);
    m_tree->setObjectName(QStringLiteral("puppetTree"));
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Name"), tr("Placement")});
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
    m_treeStack = new QStackedWidget(left);
    m_treeStack->addWidget(m_tree);
    m_treeStack->addWidget(emptyState);
    leftLayout->addWidget(m_treeStack, 1);

    m_addPuppetAction = new QAction(tr("New Puppet"), this);
    m_addPuppetAction->setToolTip(tr("Add a puppet to this atlas"));
    connect(m_addPuppetAction, &QAction::triggered, this, &PuppetWorkspace::newPuppet);
    m_addPartAction = new QAction(tr("New Part"), this);
    m_addPartAction->setToolTip(tr("Add a part to the selected puppet, in front of the others"));
    connect(m_addPartAction, &QAction::triggered, this, &PuppetWorkspace::addPart);
    m_addSpritesAction = new QAction(tr("Add Sprites..."), this);
    m_addSpritesAction->setToolTip(tr("Pick sprites the selected part can show"));
    connect(m_addSpritesAction, &QAction::triggered, this, &PuppetWorkspace::pickSpriteVariants);
    m_removeAction = new QAction(QApplication::style()->standardIcon(QStyle::SP_DialogDiscardButton),
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
    m_addButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    m_addButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_addButton->setPopupMode(QToolButton::InstantPopup);
    m_addButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* addMenu = new QMenu(m_addButton);
    addMenu->addAction(m_addPuppetAction);
    addMenu->addAction(m_addPartAction);
    addMenu->addAction(m_addSpritesAction);
    m_addButton->setMenu(addMenu);
    m_removeButton = new QPushButton(m_removeAction->icon(), tr("Remove"), left);
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
        auto* fitButton = new QPushButton(tr("Fit"), middle);
        fitButton->setObjectName(QStringLiteral("puppetFit"));
        fitButton->setToolTip(tr("Zoom to show the whole puppet"));
        row->addWidget(fitButton);
        row->addStretch();
        m_resetPoseButton = new QPushButton(tr("Reset"), middle);
        m_resetPoseButton->setObjectName(QStringLiteral("puppetResetPose"));
        m_resetPoseButton->setToolTip(tr("Put every part back at rest. Turning attached parts on the canvas "
                                         "tries their joints; it isn't saved, as a rig is its rest pose"));
        m_resetPoseButton->setEnabled(false);
        row->addWidget(m_resetPoseButton);
        middleLayout->addLayout(row);
        connect(fitButton, &QPushButton::clicked, this, [this]() { m_canvas->fitPuppet(); });
        connect(m_resetPoseButton, &QPushButton::clicked, this, &PuppetWorkspace::resetTriedPose);
    }
    m_canvas = new PuppetCanvas(middle);
    m_canvas->setObjectName(QStringLiteral("puppetCanvas"));
    middleLayout->addWidget(m_canvas, 1);
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
    connect(m_canvas, &PuppetCanvas::partDragStarted, this, [this](int part) {
        const Puppet* puppet = selectedPuppet();
        if (!puppet || part < 0 || part >= puppet->parts.size()) return;
        m_dragStartPosition = puppet->parts[part].position.value_or(QPoint());
        ++m_dragCount;
    });
    connect(m_canvas, &PuppetCanvas::partDragged, this, [this](int part, const QPoint& offset) {
        if (part != m_selection.part) return;
        setSelectedPartPosition(m_dragStartPosition + offset, QStringLiteral("drag %1").arg(m_dragCount));
    });
    connect(m_canvas, &PuppetCanvas::attachRequested, this, [this](int part, int parent, const QString& marker) {
        const Puppet* puppet = selectedPuppet();
        if (!puppet || part != m_selection.part) return;
        // A part already hanging keeps the marker it hangs by; a new joint uses its pivot.
        const PuppetAttachment* joint = puppetParentAttachment(*puppet, puppet->parts[part].name);
        attachSelectedPart(parent, marker, joint ? joint->childMarker : PIVOT_MARKER);
    });
    connect(m_canvas, &PuppetCanvas::detachRequested, this, [this](int part, const QPointF& scenePos) {
        if (part != m_selection.part) return;
        placeSelectedPartJointAt(scenePos);
    });
    connect(m_canvas, &PuppetCanvas::partTurnStarted, this, [this](int part, bool) {
        const Puppet* puppet = selectedPuppet();
        if (!puppet || part < 0 || part >= puppet->parts.size()) return;
        m_turnStart = m_tried.value(puppet->parts[part].name);
    });
    connect(m_canvas, &PuppetCanvas::partTurned, this, [this](int part, double degrees) {
        tryTurning(part, m_turnStart.angle + degrees);
    });
    connect(m_canvas, &PuppetCanvas::partDrifted, this, [this](int part, const QPointF& offset) {
        tryDrifting(part, offset);
    });
    connect(m_canvas, &PuppetCanvas::partNudged, this, [this](int part, const QPoint& offset) {
        const Puppet* puppet = selectedPuppet();
        if (!puppet || part != m_selection.part || !puppet->parts[part].position) return;
        setSelectedPartPosition(*puppet->parts[part].position + offset,
                                QStringLiteral("nudge %1 %2").arg(m_selection.puppet).arg(part));
    });

    // === Right: properties ===
    m_properties = new PuppetPropertiesPanel(splitter);
    m_properties->setMinimumWidth(260);
    splitter->addWidget(m_properties);
    connect(m_properties, &PuppetPropertiesPanel::renameRequested, this, &PuppetWorkspace::renameSelected);
    connect(m_properties, &PuppetPropertiesPanel::positionChanged, this,
            [this](const QPoint& position) { setSelectedPartPosition(position); });
    connect(m_properties, &PuppetPropertiesPanel::makeRestVariantRequested, this, [this]() { moveSelected(0); });
    connect(m_properties, &PuppetPropertiesPanel::attachRequested, this,
            [this](const QString& parent, const QString& parentMarker, const QString& childMarker) {
        const Puppet* puppet = selectedPuppet();
        if (!puppet) return;
        attachSelectedPart(PuppetEditing::partNames(*puppet).indexOf(parent), parentMarker, childMarker);
    });
    connect(m_properties, &PuppetPropertiesPanel::detachRequested, this, &PuppetWorkspace::detachSelectedPart);
    connect(m_properties, &PuppetPropertiesPanel::angleChanged, this, [this](int angle) {
        tryTurning(m_selection.part, angle);
    });
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
    });
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (m_rebuilding || column != NAME_COLUMN) return;
        m_selection = PuppetTreeWidget::selectionOf(item);
        renameSelected(item->text(NAME_COLUMN));
    });
    connect(m_tree, &PuppetTreeWidget::moveRequested, this, [this](const PuppetSelection& moved, int to) {
        m_selection = moved;
        moveSelected(to);
    });
    connect(m_tree, &QWidget::customContextMenuRequested, this, &PuppetWorkspace::showContextMenu);

    updateButtons();
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
    m_selection = clampedSelection(puppets(), m_selection);
    rebuildTree();
    updateCanvas();
    updateProperties();
    updateButtons();
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

SpritePtr PuppetWorkspace::spriteFor(const PuppetVariant& variant) const {
    if (!m_session) return nullptr;
    QString spriteSource = variant.source;
    if (variant.kind == PuppetVariant::Kind::Animation) {
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
        if (!timeline || timeline->frames.isEmpty()) return nullptr;
        spriteSource = timeline->frames.first();
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
    const QIcon restIcon = restVariantIcon(palette());
    const QIcon warningIcon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning);
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

            for (int v = 0; v < part.variants.size(); ++v) {
                const PuppetVariant& variant = part.variants[v];
                auto* variantItem = new QTreeWidgetItem(partItem);
                variantItem->setText(NAME_COLUMN, puppetVariantName(variant));
                variantItem->setText(DETAIL_COLUMN, variant.kind == PuppetVariant::Kind::Animation
                                                        ? tr("animation") : tr("sprite"));
                if (!spriteFor(variant)) {
                    variantItem->setIcon(NAME_COLUMN, warningIcon);
                    variantItem->setToolTip(NAME_COLUMN, tr("%1 isn't in this atlas").arg(describeSource(variant)));
                } else {
                    if (v == 0) variantItem->setIcon(NAME_COLUMN, restIcon);
                    variantItem->setToolTip(NAME_COLUMN, v == 0 ? tr("%1 (rest variant)").arg(describeSource(variant))
                                                                : describeSource(variant));
                }
                variantItem->setFlags(baseFlags | Qt::ItemIsDragEnabled);
                PuppetTreeWidget::setSelectionOf(variantItem, {p, i, v});
            }
        }
    }
    m_tree->expandAll();
    m_treeStack->setCurrentIndex(all.isEmpty() ? 1 : 0);
    m_tree->setCurrentItem(itemFor(m_selection));
    m_rebuilding = false;
}

void PuppetWorkspace::updateCanvas() {
    const Puppet* puppet = selectedPuppet();
    if (!puppet) {
        m_canvas->clearPuppet();
        m_fittedPuppet.clear();
        return;
    }
    // Another puppet starts at rest.
    const QString puppetKey = m_session->activeAtlas().id + QLatin1Char('/') + puppet->name;
    if (puppetKey != m_triedPuppet) {
        m_triedPuppet = puppetKey;
        m_tried.clear();
    }
    const QVector<PuppetPartPlacement> placements =
        PuppetEvaluator::evaluate(*puppet, triedStates(*puppet), spriteLookup());
    QVector<PuppetCanvas::Part> parts;
    parts.reserve(puppet->parts.size());
    for (int i = 0; i < puppet->parts.size(); ++i) {
        const PuppetPart& part = puppet->parts[i];
        PuppetCanvas::Part drawn;
        drawn.placed = placements[i].placed;
        drawn.transform = placements[i].transform;
        // A part placed by its attachment moves with its parent, not on its own.
        drawn.movable = part.position.has_value() && !puppetParentAttachment(*puppet, part.name);
        drawn.attachTarget = m_selection.part >= 0 && PuppetEditing::canAttach(*puppet, m_selection.part, i);
        drawn.turnable = puppetParentAttachment(*puppet, part.name) != nullptr;
        drawn.joint = placements[i].pivot;
        const SpritePtr sprite = part.variants.isEmpty() ? nullptr : spriteFor(part.variants.first());
        drawn.image = imageOf(sprite);
        if (sprite) {
            bool hasPivotMarker = false;
            for (const NamedPoint& marker : sprite->points) {
                hasPivotMarker = hasPivotMarker || marker.name == PIVOT_MARKER;
                drawn.markers.append({marker.name, drawn.transform.map(PuppetMotion::markerPoint(marker)),
                                      drawn.transform.map(PuppetMotion::markerShape(marker))});
            }
            if (!hasPivotMarker)
                drawn.markers.append({PIVOT_MARKER, drawn.transform.map(QPointF(sprite->pivotX, sprite->pivotY)), {}});
        }
        parts.append(drawn);
    }
    QVector<PuppetCanvas::Bone> bones;
    const QStringList names = PuppetEditing::partNames(*puppet);
    for (int i = 0; i < puppet->parts.size(); ++i) {
        const PuppetAttachment* joint = puppetParentAttachment(*puppet, puppet->parts[i].name);
        const int parent = joint ? names.indexOf(joint->parent) : -1;
        if (parent < 0 || !placements[i].placed || !placements[parent].placed) continue;
        bones.append({placements[parent].pivot, placements[i].pivot, i == m_selection.part});
    }
    const int selectedPart = m_selection.part;
    std::optional<QPointF> pivot;
    if (selectedPart >= 0 && selectedPart < placements.size() && placements[selectedPart].placed)
        pivot = placements[selectedPart].pivot;
    m_canvas->showPuppet(parts, bones, selectedPart, pivot, jointRange(*puppet, placements));
    m_resetPoseButton->setEnabled(!m_tried.isEmpty());

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
        if (const PuppetAttachment* joint = puppetParentAttachment(*puppet, part.name)) {
            placement.parent = joint->parent;
            placement.parentMarker = joint->parentMarker;
            placement.childMarker = joint->childMarker;
            placement.leeway = joint->leeway;
            placement.angleMin = joint->angleMin;
            placement.angleMax = joint->angleMax;
        }
        for (int j = 0; j < puppet->parts.size(); ++j) {
            if (!PuppetEditing::canAttach(*puppet, i, j)) continue;
            placement.parents.append(puppet->parts[j].name);
            placement.parentMarkers.insert(puppet->parts[j].name, jointMarkers(puppet->parts[j], false));
        }
        placement.childMarkers = jointMarkers(part, true);
        placement.angle = int(std::lround(triedStates(*puppet)[i].angle));
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
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction(m_addPuppetAction);
    if (m_selection.puppet >= 0) menu->addAction(m_addPartAction);
    if (m_selection.part >= 0) menu->addAction(m_addSpritesAction);
    if (m_selection.puppet >= 0) {
        menu->addSeparator();
        if (m_selection.variant >= 0) {
            QAction* rest = menu->addAction(tr("Make Rest Variant"), this, [this]() { moveSelected(0); });
            rest->setEnabled(m_selection.variant > 0);
        } else if (m_selection.part >= 0) {
            const int count = puppets()[m_selection.puppet].parts.size();
            const int part = m_selection.part;
            QAction* forward = menu->addAction(tr("Bring Forward"), this, [this, part]() { moveSelected(part + 1); });
            forward->setEnabled(part + 1 < count);
            QAction* backward = menu->addAction(tr("Send Backward"), this, [this, part]() { moveSelected(part - 1); });
            backward->setEnabled(part > 0);
        }
        menu->addSeparator();
        menu->addAction(m_renameAction);
        menu->addAction(m_removeAction);
    }
    menu->popup(m_tree->viewport()->mapToGlobal(pos));
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
}

void PuppetWorkspace::pushEdit(const QString& text, const QVector<Puppet>& after,
                               const PuppetSelection& selectionAfter, const QString& mergeKey) {
    if (!m_session || !m_undoStack) return;
    m_undoStack->push(new PuppetsEditCommand(
        text, m_session->activeAtlas().id, puppets(), after, m_selection, selectionAfter,
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
        MessageDialog::warning(this, tr("Can't rename"), problem);
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
    if (!selectedPuppet() || m_selection.part < 0) return;
    QVector<Puppet> after = puppets();
    const PuppetSelection s = m_selection;
    if (s.variant >= 0) {
        PuppetEditing::moveVariant(after[s.puppet], s.part, s.variant, to);
        if (after == puppets()) return;
        pushEdit(to == 0 ? tr("Make Rest Variant") : tr("Move Variant"), after, {s.puppet, s.part, to});
    } else {
        PuppetEditing::movePart(after[s.puppet], s.part, to);
        if (after == puppets()) return;
        pushEdit(tr("Change Draw Order"), after, {s.puppet, to, -1});
    }
}

void PuppetWorkspace::setSelectedPartPosition(const QPoint& position, const QString& mergeKey) {
    if (!selectedPuppet() || m_selection.part < 0) return;
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
    if (!puppet || m_selection.part < 0) return;
    QVector<Puppet> after = puppets();
    PuppetEditing::attachPart(after[m_selection.puppet], m_selection.part, parentIndex, parentMarker, childMarker);
    if (after == puppets()) return;
    pushEdit(tr("Attach Part"), after, {m_selection.puppet, m_selection.part, -1});
}

void PuppetWorkspace::detachSelectedPart() {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || m_selection.part < 0) return;
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
    if (!puppet || m_selection.part < 0) return;
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
    if (!selectedPuppet() || m_selection.part < 0) return;
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

QVector<PuppetPartState> PuppetWorkspace::triedStates(const Puppet& puppet) const {
    QVector<PuppetPartState> states(puppet.parts.size());
    for (int i = 0; i < puppet.parts.size(); ++i) {
        const auto it = m_tried.constFind(puppet.parts[i].name);
        if (it == m_tried.constEnd()) continue;
        // The rig may have changed since: keep within its limits as they are now.
        states[i].angle = triedAngle(puppet, i, it->angle);
        const PuppetAttachment* joint = puppetParentAttachment(puppet, puppet.parts[i].name);
        // A positioned part turns around its pivot; only an attached one drifts.
        if (!joint) continue;
        const std::optional<NamedPoint> marker = parentMarkerOf(puppet, i);
        states[i].offset = marker ? QPointF(PuppetMotion::clampOffset(*marker, joint->leeway, it->offset))
                                  : QPointF();
    }
    return states;
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
    range.angle = triedStates(puppet)[i].angle;
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

void PuppetWorkspace::tryTurning(int partIndex, double angle) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || partIndex < 0 || partIndex >= puppet->parts.size()) return;
    const int tried = triedAngle(*puppet, partIndex, angle);
    m_tried[puppet->parts[partIndex].name].angle = tried;
    updateCanvas();
    if (partIndex == m_selection.part) m_properties->setAngle(tried);
}

int PuppetWorkspace::triedAngle(const Puppet& puppet, int partIndex, double angle) const {
    const PuppetAttachment* joint = puppetParentAttachment(puppet, puppet.parts[partIndex].name);
    return joint ? PuppetMotion::clampAngle(*joint, angle) : PuppetMotion::freeAngle(angle);
}

void PuppetWorkspace::tryDrifting(int partIndex, const QPointF& sceneOffset) {
    const Puppet* puppet = selectedPuppet();
    if (!puppet || partIndex < 0 || partIndex >= puppet->parts.size()) return;
    const PuppetAttachment* joint = puppetParentAttachment(*puppet, puppet->parts[partIndex].name);
    const std::optional<NamedPoint> marker = parentMarkerOf(*puppet, partIndex);
    const int parent = joint ? PuppetEditing::partNames(*puppet).indexOf(joint->parent) : -1;
    if (!marker || parent < 0) return;
    // The offset is in the parent's sprite pixels, so it turns with the parent.
    const QVector<PuppetPartPlacement> placements =
        PuppetEvaluator::evaluate(*puppet, triedStates(*puppet), spriteLookup());
    const QTransform toParent = placements[parent].transform.inverted();
    const QPointF delta = toParent.map(sceneOffset) - toParent.map(QPointF(0, 0));
    m_tried[puppet->parts[partIndex].name].offset =
        QPointF(PuppetMotion::clampOffset(*marker, joint->leeway, m_turnStart.offset + delta));
    updateCanvas();
}

void PuppetWorkspace::resetTriedPose() {
    m_tried.clear();
    updateCanvas();
    m_properties->setAngle(0);
}
