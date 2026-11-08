#include "PuppetWorkspaceTests.h"
#include "AppSettings.h"
#include "ProjectSession.h"
#include "NavigatorPanel.h"
#include "NewPuppetDialog.h"
#include "PuppetCanvas.h"
#include "PuppetPoseStrip.h"
#include "PuppetSequenceTrack.h"
#include "PuppetEditing.h"
#include "PuppetTreeWidget.h"
#include "PuppetWorkspace.h"
#include "SpriteModels.h"

#include <QDir>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImage>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QMenu>
#include <QMimeData>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabBar>
#include <QToolButton>
#include <QTemporaryDir>
#include <QTreeWidgetItemIterator>
#include <QUndoStack>
#include <QtTest>

#include <algorithm>
#include <memory>

namespace {

/// A project with an atlas of plain sprites, laid out as one page.
struct PuppetProject {
    QTemporaryDir dir;
    ProjectSession session;
    QUndoStack undoStack;

    PuppetProject() {
        session.currentFolder = dir.path();
        session.activeAtlas().layoutModels = {LayoutModel()};
    }

    SpritePtr addSprite(const QString& name, int width, int height, QPoint pivot,
                        const QVector<NamedPoint>& markers = {}) {
        const QString path = QDir::cleanPath(dir.filePath(name + QStringLiteral(".png")));
        QImage image(width, height, QImage::Format_ARGB32);
        image.fill(Qt::gray);
        image.save(path);
        auto sprite = std::make_shared<Sprite>();
        sprite->path = path;
        sprite->name = name;
        sprite->pivotX = pivot.x();
        sprite->pivotY = pivot.y();
        sprite->points = markers;
        session.activeAtlas().layoutModels.first().sprites.append(sprite);
        session.activeAtlas().spritePaths.append(path);
        session.activeFramePaths.append(path);
        session.spriteIndex.insert(path, sprite);
        return sprite;
    }
};

NamedPoint pointMarker(const QString& name, int x, int y) {
    NamedPoint marker;
    marker.name = name;
    marker.x = x;
    marker.y = y;
    return marker;
}

PuppetVariant spriteVariant(const SpritePtr& sprite) {
    PuppetVariant variant;
    variant.source = sprite->path;
    return variant;
}

/// A doll: a 20x40 body standing on the origin, and a 16x16 head whose bottom-middle
/// pivot hangs from the body's "neck" marker.
struct Doll {
    PuppetProject project;
    SpritePtr body;
    SpritePtr head;
    SpritePtr headSmiling;

    Doll() {
        body = project.addSprite(QStringLiteral("body"), 20, 40, QPoint(10, 40), {pointMarker("neck", 10, 2)});
        head = project.addSprite(QStringLiteral("head"), 16, 16, QPoint(8, 16));
        headSmiling = project.addSprite(QStringLiteral("head-smiling"), 16, 16, QPoint(8, 16));
        Puppet doll;
        doll.name = QStringLiteral("doll");
        PuppetPart bodyPart{QStringLiteral("body"), {spriteVariant(body)}, QPoint(0, 0)};
        PuppetPart headPart{QStringLiteral("head"), {spriteVariant(head), spriteVariant(headSmiling)}, std::nullopt};
        doll.parts = {bodyPart, headPart};
        doll.attachments = {{QStringLiteral("head"), QStringLiteral("pivot"), QStringLiteral("body"),
                             QStringLiteral("neck")}};
        PuppetEditing::keepRestPose(doll);
        project.session.activeAtlas().puppets = {doll};
    }

    const Puppet& puppet() { return project.session.activeAtlas().puppets.first(); }
};

/// The tree's rows as "name | detail", children indented by two spaces per level.
QStringList treeRows(PuppetTreeWidget* tree) {
    QStringList rows;
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        int depth = 0;
        for (QTreeWidgetItem* parent = (*it)->parent(); parent; parent = parent->parent()) ++depth;
        rows.append(QString(depth * 2, QLatin1Char(' ')) + (*it)->text(0) + QStringLiteral(" | ") + (*it)->text(1));
    }
    return rows;
}

/// The canvas's part images in draw order, with where they cover in the puppet's space.
QVector<QRectF> drawnParts(PuppetCanvas* canvas) {
    QVector<QGraphicsPixmapItem*> items;
    for (QGraphicsItem* item : canvas->scene()->items()) {
        if (auto* pixmap = qgraphicsitem_cast<QGraphicsPixmapItem*>(item)) items.append(pixmap);
    }
    std::sort(items.begin(), items.end(), [](auto* a, auto* b) { return a->zValue() < b->zValue(); });
    QVector<QRectF> rects;
    for (auto* item : items) rects.append(item->sceneBoundingRect());
    return rects;
}

bool showsEmptyState(PuppetWorkspace& workspace) {
    auto* emptyState = workspace.findChild<QLabel*>(QStringLiteral("puppetEmptyState"));
    return emptyState && emptyState->isVisibleTo(&workspace);
}

}  // namespace

void PuppetWorkspaceTests::testBuildingAPuppetIsUndoable() {
    PuppetProject project;
    const SpritePtr face = project.addSprite(QStringLiteral("face"), 16, 16, QPoint(8, 8));
    PuppetWorkspace workspace(&project.session, &project.undoStack);
    workspace.refreshSprites();
    QVERIFY(showsEmptyState(workspace));

    Puppet blank;
    blank.name = QStringLiteral("puppet");
    workspace.addPuppet(blank);
    workspace.addPart();
    workspace.addSpriteVariants({face->path});
    QVERIFY(!showsEmptyState(workspace));
    QCOMPARE(project.undoStack.count(), 3);
    const QStringList built = treeRows(workspace.tree());
    QCOMPARE(built.size(), 3);
    QVERIFY(built[0].startsWith(QStringLiteral("puppet | ")));
    QCOMPARE(built[1], QStringLiteral("  part | at 0,0"));
    QCOMPARE(built[2], QStringLiteral("    face | sprite"));
    QCOMPARE(workspace.selection(), (PuppetSelection{0, 0, 0}));
    // The new part sits on the origin: its pivot (8,8) is at 0,0.
    QCOMPARE(drawnParts(workspace.canvas()), (QVector<QRectF>{QRectF(-8, -8, 16, 16)}));

    // Adding a sprite the part already shows changes nothing.
    workspace.addSpriteVariants({face->path});
    QCOMPARE(project.undoStack.count(), 3);

    while (project.undoStack.canUndo()) project.undoStack.undo();
    QVERIFY(project.session.activeAtlas().puppets.isEmpty());
    QVERIFY(showsEmptyState(workspace));
    while (project.undoStack.canRedo()) project.undoStack.redo();
    QCOMPARE(treeRows(workspace.tree()), built);
}

namespace {
/// Drops @p spritePaths, as the Sprites tree drags them, on @p tree's viewport at @p pos.
void dropSprites(QTreeWidget* tree, const QPoint& pos, const QStringList& spritePaths, const QString& folder = QString()) {
    QMimeData mime;
    mime.setData(QStringLiteral("application/x-sprat-sprite"), spritePaths.join(QLatin1Char('\n')).toUtf8());
    if (!folder.isEmpty()) mime.setData(QStringLiteral("application/x-sprat-group-name"), folder.toUtf8());
    // A QPoint: Qt 6.5's drag enter and move events take no QPointF (later versions take either).
    QDragEnterEvent enter(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(tree->viewport(), &enter);
    QDragMoveEvent move(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(tree->viewport(), &move);
    QDropEvent drop(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(tree->viewport(), &drop);
}

/// The texts of @p menu's actions, separators as "-", disabled ones in brackets.
QStringList menuItems(QMenu* menu) {
    QStringList items;
    for (QAction* action : menu->actions())
        items.append(action->isSeparator() ? QStringLiteral("-")
                     : action->isEnabled() ? action->text()
                                           : QStringLiteral("[%1]").arg(action->text()));
    return items;
}

/// The dialog's parts list as rows, sprites indented by two spaces.
QStringList partRows(QTreeWidget* tree) {
    QStringList rows;
    for (QTreeWidgetItemIterator it(tree); *it; ++it)
        rows.append(((*it)->parent() ? QStringLiteral("  ") : QString()) + (*it)->text(0));
    return rows;
}
}  // namespace

void PuppetWorkspaceTests::testNewPuppetDialogBuildsAPuppetFromDroppedSprites() {
    Doll doll;
    NewPuppetDialog dialog(&doll.project.session, doll.project.session.activeAtlas().puppets,
                           [](const SpritePtr&) { return true; });
    dialog.resize(820, 560);
    dialog.show();
    QPushButton* create = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);

    // The name: a free one to start with; a taken one can't be used.
    QCOMPARE(dialog.nameEdit()->text(), QStringLiteral("puppet"));
    dialog.nameEdit()->setText(QStringLiteral("doll"));
    QVERIFY(!create->isEnabled());
    dialog.nameEdit()->setText(QStringLiteral("robot"));
    QVERIFY(create->isEnabled());

    // Sprites dropped on empty space make one part showing them, named after what they share.
    QTreeWidget* parts = dialog.partsTree();
    const QPoint empty = parts->viewport()->rect().bottomLeft() - QPoint(-10, 10);
    dropSprites(parts, empty, {doll.body->path});
    QCOMPARE(partRows(parts), (QStringList{"body", "  body"}));
    dropSprites(parts, empty, {doll.head->path, doll.headSmiling->path});
    QCOMPARE(partRows(parts), (QStringList{"body", "  body", "head", "  head", "  head-smiling"}));
    // Dropped on a part (or one of its sprites), they become its variants.
    dropSprites(parts, parts->visualItemRect(parts->topLevelItem(0)->child(0)).center(), {doll.head->path});
    QCOMPARE(partRows(parts), (QStringList{"body", "  body", "  head", "head", "  head", "  head-smiling"}));
    parts->clearSelection();
    parts->topLevelItem(0)->child(1)->setSelected(true);
    dialog.removeSelected();
    QCOMPARE(partRows(parts), (QStringList{"body", "  body", "head", "  head", "  head-smiling"}));

    // An empty part waits for sprites; a taken part name is refused.
    parts->clearSelection();
    dialog.addPart();
    QCOMPARE(partRows(parts).mid(5), (QStringList{"part", "  (drop sprites here)"}));
    parts->topLevelItem(2)->setText(0, QStringLiteral("head"));
    QCoreApplication::processEvents();
    QCOMPARE(parts->topLevelItem(2)->text(0), QStringLiteral("part"));
    QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("newPuppetPartProblem"))->isVisibleTo(&dialog));
    parts->topLevelItem(2)->setText(0, QStringLiteral("tail"));
    QCoreApplication::processEvents();
    QCOMPARE(parts->topLevelItem(2)->text(0), QStringLiteral("tail"));
    parts->clearSelection();
    parts->setCurrentItem(parts->topLevelItem(2));
    dialog.findChild<QPushButton*>(QStringLiteral("newPuppetRemove"))->click();
    QCOMPARE(parts->topLevelItemCount(), 2);

    // The sprite list's own button adds its selection to the selected part.
    parts->clearSelection();
    parts->setCurrentItem(parts->topLevelItem(0));
    auto* addToPart = dialog.findChild<QPushButton*>(QStringLiteral("newPuppetAddToPart"));
    QCOMPARE(addToPart->text(), QStringLiteral("Add to \"body\""));
    dialog.sprites()->findSpriteItem(doll.headSmiling->path)->setSelected(true);
    addToPart->click();
    QCOMPARE(partRows(parts).mid(0, 3), (QStringList{"body", "  body", "  head-smiling"}));

    const Puppet built = dialog.puppet();
    QCOMPARE(built.name, QStringLiteral("robot"));
    QCOMPARE(built.parts.size(), 2);
    QCOMPARE(built.parts[1].variants.size(), 2);
    QCOMPARE(built.parts[1].position, std::optional<QPoint>(QPoint(0, 0)));

    // Created as one undo step, and selected.
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    workspace.refreshSprites();
    workspace.addPuppet(built);
    QCOMPARE(doll.project.session.activeAtlas().puppets.size(), 2);
    QCOMPARE(doll.project.undoStack.count(), 1);
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("New Puppet"));
    QCOMPARE(workspace.selection(), (PuppetSelection{1, -1, -1}));
}

void PuppetWorkspaceTests::testNewPuppetDialogPartsMenu() {
    // The new part's name: the dragged folder's, else what the sprites' names start with.
    QCOMPARE(NewPuppetDialog::partNameFor({"/s/walk_01.png", "/s/walk_02.png", "/s/walk_10.png"}),
             QStringLiteral("walk"));
    QCOMPARE(NewPuppetDialog::partNameFor({"/s/arm.png", "/s/leg.png"}), QStringLiteral("arm"));
    QCOMPARE(NewPuppetDialog::partNameFor({"/s/a1.png", "/s/a2.png"}, QStringLiteral("hands")), QStringLiteral("hands"));

    Doll doll;
    NewPuppetDialog dialog(&doll.project.session, {}, [](const SpritePtr&) { return true; });
    dialog.resize(820, 560);
    dialog.show();
    QTreeWidget* parts = dialog.partsTree();
    const QPoint empty = parts->viewport()->rect().bottomLeft() - QPoint(-10, 10);
    dropSprites(parts, empty, {doll.head->path, doll.headSmiling->path, doll.body->path}, QStringLiteral("faces"));
    dropSprites(parts, empty, {doll.body->path});
    QCOMPARE(partRows(parts), (QStringList{"faces", "  head", "  head-smiling", "  body", "body", "  body"}));

    // A part: rename or delete it. Sprites: take them out of their part, several at once.
    parts->clearSelection();
    parts->setCurrentItem(parts->topLevelItem(0));
    std::unique_ptr<QMenu> partMenu(dialog.createPartsMenu());
    QCOMPARE(menuItems(partMenu.get()), (QStringList{"Rename Part", "-", "Delete Part"}));
    parts->clearSelection();
    parts->topLevelItem(0)->child(1)->setSelected(true);
    parts->topLevelItem(0)->child(2)->setSelected(true);
    std::unique_ptr<QMenu> spriteMenu(dialog.createPartsMenu());
    QCOMPARE(menuItems(spriteMenu.get()), QStringList{"Exclude 2 Sprites"});
    spriteMenu->actions().first()->trigger();
    QCOMPARE(partRows(parts), (QStringList{"faces", "  head", "body", "  body"}));

    // A selected part takes its selected sprites with it; Delete deletes the selection.
    parts->clearSelection();
    parts->topLevelItem(1)->setSelected(true);
    parts->topLevelItem(1)->child(0)->setSelected(true);
    parts->topLevelItem(0)->setSelected(true);
    std::unique_ptr<QMenu> bothMenu(dialog.createPartsMenu());
    QCOMPARE(menuItems(bothMenu.get()), QStringList{"Delete 2 Parts"});
    parts->setFocus();
    QTest::keyClick(parts, Qt::Key_Delete);
    QCOMPARE(parts->topLevelItemCount(), 0);
}

namespace {
/// The doll with Rest and two poses: "nod" turns the head 20°, "smile" shows the smiling head.
void addDollPoses(Doll& doll) {
    PuppetPoseSetting nod;
    nod.part = QStringLiteral("head");
    nod.angle = 20;
    PuppetPoseSetting smile;
    smile.part = QStringLiteral("head");
    smile.variant = QStringLiteral("head-smiling");
    Puppet& puppet = doll.project.session.activeAtlas().puppets.first();
    puppet.poses = {{QStringLiteral("nod"), {nod}}, {QStringLiteral("smile"), {smile}}};
    PuppetEditing::keepRestPose(puppet);
}

/// Adds an empty pose with the strip's New Pose button; it is then the selected frame.
void startPose(PuppetWorkspace& workspace) {
    workspace.findChild<QPushButton*>(QStringLiteral("puppetNewPose"))->click();
}

/// How opaque part @p index is drawn: faint when the shown pose doesn't set it.
qreal partOpacity(PuppetCanvas* canvas, int index) {
    for (QGraphicsItem* item : canvas->scene()->items()) {
        if (item->data(0).isValid() && item->data(0).toInt() == index) return item->opacity();
    }
    return -1;
}

/// The strip's frame names, "rest" first.
QStringList frameNames(PuppetPoseStrip* strip) {
    QStringList names;
    for (int i = 0; i < strip->count(); ++i) names.append(strip->item(i)->text());
    return names;
}
}  // namespace

void PuppetWorkspaceTests::testCanvasPlacesPartsByTheirPivot() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    workspace.refreshSprites();
    workspace.select({0, -1, -1});
    // The body's bottom-middle pivot is on the origin; the head's pivot is on the body's
    // neck, 2 px below the body's top.
    QCOMPARE(drawnParts(workspace.canvas()),
             (QVector<QRectF>{QRectF(-10, -40, 20, 40), QRectF(-8, -54, 16, 16)}));
    QCOMPARE(treeRows(workspace.tree()).mid(1),
             (QStringList{QStringLiteral("  body | at 0,0"), QStringLiteral("    body | sprite"),
                          QStringLiteral("  head | ↳ body:neck"), QStringLiteral("    head | sprite"),
                          QStringLiteral("    head-smiling | sprite")}));
}

void PuppetWorkspaceTests::testClickingAPartSelectsIt() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    workspace.resize(1000, 600);
    workspace.show();
    workspace.refreshSprites();
    workspace.select({0, -1, -1});
    PuppetCanvas* canvas = workspace.canvas();

    QTest::mouseClick(canvas->viewport(), Qt::LeftButton, {}, canvas->mapFromScene(QPointF(0, -46)));
    QCOMPARE(workspace.selection(), (PuppetSelection{0, 1, -1}));
    QCOMPARE(workspace.tree()->currentItem()->text(0), QStringLiteral("head"));

    // Empty space selects the puppet itself.
    QTest::mouseClick(canvas->viewport(), Qt::LeftButton, {}, canvas->mapFromScene(QPointF(30, 20)));
    QCOMPARE(workspace.selection(), (PuppetSelection{0, -1, -1}));
}

void PuppetWorkspaceTests::testDraggingAPartMovesIt() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    workspace.resize(1000, 600);
    workspace.show();
    workspace.refreshSprites();
    workspace.select({0, -1, -1});
    PuppetCanvas* canvas = workspace.canvas();
    workspace.zoomSpin()->setValue(100.0);  // one view pixel per sprite pixel
    QWidget* viewport = canvas->viewport();

    // Drag the body 12 px right and 5 px down, in two moves.
    const QPoint start = canvas->mapFromScene(QPointF(0, -20));
    QTest::mousePress(viewport, Qt::LeftButton, {}, start);
    QTest::mouseMove(viewport, start + QPoint(6, 2));
    QTest::mouseMove(viewport, start + QPoint(12, 5));
    QTest::mouseRelease(viewport, Qt::LeftButton, {}, start + QPoint(12, 5));
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(12, 5)));
    QCOMPARE(doll.project.undoStack.count(), 1);
    QCOMPARE(workspace.selection(), (PuppetSelection{0, 0, -1}));
    // The head hangs from the body, so it moved with it.
    QCOMPARE(drawnParts(canvas), (QVector<QRectF>{QRectF(2, -35, 20, 40), QRectF(4, -49, 16, 16)}));

    // A second drag is its own undo step.
    const QPoint again = canvas->mapFromScene(QPointF(12, -15));
    QTest::mousePress(viewport, Qt::LeftButton, {}, again);
    QTest::mouseMove(viewport, again + QPoint(0, 10));
    QTest::mouseRelease(viewport, Qt::LeftButton, {}, again + QPoint(0, 10));
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(12, 15)));
    QCOMPARE(doll.project.undoStack.count(), 2);
    doll.project.undoStack.undo();
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(12, 5)));

    // The head is placed by its attachment: dragging it pulls it in the Rest pose, the rig stays.
    const Puppet beforeHeadDrag = doll.puppet();
    const QPoint head = canvas->mapFromScene(QPointF(12, -41));
    QTest::mousePress(viewport, Qt::LeftButton, {}, head);
    QTest::mouseMove(viewport, head + QPoint(20, 0));
    QTest::mouseRelease(viewport, Qt::LeftButton, {}, head + QPoint(20, 0));
    QCOMPARE(workspace.selection(), (PuppetSelection{0, 1, -1}));
    QCOMPARE(doll.puppet().parts, beforeHeadDrag.parts);
    QCOMPARE(doll.puppet().attachments, beforeHeadDrag.attachments);
    QVERIFY(doll.puppet().poses.first().settings[1].angle != std::optional<int>(0));
    QCOMPARE(doll.project.undoStack.index(), 2);
}

void PuppetWorkspaceTests::testArrowKeysNudgeTheSelectedPart() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    workspace.refreshSprites();
    workspace.select({0, 0, -1});
    PuppetCanvas* canvas = workspace.canvas();

    QTest::keyClick(canvas, Qt::Key_Right);
    QTest::keyClick(canvas, Qt::Key_Right);
    QTest::keyClick(canvas, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(2, -10)));
    QCOMPARE(doll.project.undoStack.count(), 1);

    // The head hangs from the body: the arrows leave it alone.
    workspace.select({0, 1, -1});
    QTest::keyClick(canvas, Qt::Key_Left);
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(2, -10)));
    QCOMPARE(doll.project.undoStack.count(), 1);
}

namespace {
/// Drags with the left button from @p from to @p to, in scene coordinates.
void dragOnCanvas(PuppetCanvas* canvas, const QPointF& from, const QPointF& to) {
    QWidget* viewport = canvas->viewport();
    const QPoint start = canvas->mapFromScene(from);
    const QPoint end = canvas->mapFromScene(to);
    QTest::mousePress(viewport, Qt::LeftButton, {}, start);
    QTest::mouseMove(viewport, start + (end - start) / 2);
    QTest::mouseMove(viewport, end);
    QTest::mouseRelease(viewport, Qt::LeftButton, {}, end);
}

/// Shows @p workspace at 100% zoom, one view pixel per sprite pixel.
void showAtFullSize(PuppetWorkspace& workspace) {
    workspace.resize(1000, 600);
    workspace.show();
    workspace.refreshSprites();
    workspace.select({0, -1, -1});
    workspace.zoomSpin()->setValue(100.0);
}
}  // namespace

void PuppetWorkspaceTests::testRigLockKeepsThePlacement() {
    Doll doll;
    addDollPoses(doll);
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.select({0, 0, -1});  // the body, placed by position
    auto* lock = workspace.findChild<QCheckBox*>(QStringLiteral("partRigLock"));
    auto* byPosition = workspace.findChild<QRadioButton*>(QStringLiteral("partByPosition"));
    auto* x = workspace.findChild<QSpinBox*>(QStringLiteral("partPositionX"));
    QVERIFY(lock && byPosition && x);
    QVERIFY(!lock->isChecked() && byPosition->isEnabled() && x->isEnabled());

    lock->setChecked(true);
    QVERIFY(doll.puppet().parts[0].rigLocked);
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Lock Rig"));
    QVERIFY(!byPosition->isEnabled());
    // On Rest, moving the body moves the rig: not while locked, on the canvas or in the panel.
    QVERIFY(!x->isEnabled());
    const int edits = doll.project.undoStack.count();
    dragOnCanvas(workspace.canvas(), QPointF(0, -20), QPointF(30, -20));
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(0, 0)));
    QCOMPARE(doll.project.undoStack.count(), edits);
    // Nor does its pivot ring, hidden while locked, attach it or place it elsewhere.
    dragOnCanvas(workspace.canvas(), QPointF(0, 0), QPointF(60, 10));
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(0, 0)));
    QCOMPARE(doll.project.undoStack.count(), edits);

    // Another pose still moves it, there only.
    workspace.select({0, 0, -1});
    workspace.selectPose(1);
    QVERIFY(x->isEnabled());
    dragOnCanvas(workspace.canvas(), QPointF(0, -20), QPointF(30, -20));
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(0, 0)));
    QCOMPARE(doll.project.undoStack.count(), edits + 1);

    // Unlocked, the rig can change again; the lock is kept in the project file only.
    lock->setChecked(false);
    QVERIFY(!doll.puppet().parts[0].rigLocked);
    QVERIFY(byPosition->isEnabled());
}

void PuppetWorkspaceTests::testPoseLockKeepsThePartInThatPose() {
    Doll doll;
    addDollPoses(doll);
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.select({0, 1, -1});  // the head
    workspace.selectPose(1);       // "nod" turns it 20°
    auto* lock = workspace.findChild<QCheckBox*>(QStringLiteral("partPoseLock"));
    auto* label = workspace.findChild<QLabel*>(QStringLiteral("partPoseLabel"));
    auto* angle = workspace.findChild<QSpinBox*>(QStringLiteral("partAngle"));
    auto* shows = workspace.findChild<QComboBox*>(QStringLiteral("partShownVariant"));
    QVERIFY(lock && label && angle && shows);
    QCOMPARE(label->text(), QStringLiteral("In pose \"nod\"."));
    QVERIFY(lock->isEnabled() && !lock->isChecked() && angle->isEnabled());

    lock->setChecked(true);
    QCOMPARE(doll.puppet().poses[1].lockedParts, QStringList{"head"});
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Lock Part in Pose"));
    QVERIFY(!angle->isEnabled() && !shows->isEnabled());
    // Pulled on the canvas, or ticked out of the pose, it stays as it is.
    const int edits = doll.project.undoStack.count();
    dragOnCanvas(workspace.canvas(), QPointF(0, -46), QPointF(20, -46));
    workspace.setPartInPose(1, false);
    QCOMPARE(doll.project.undoStack.count(), edits);
    QCOMPARE(doll.puppet().poses[1].settings.size(), 1);
    QCOMPARE(workspace.poseParts()->item(1)->flags() & Qt::ItemIsUserCheckable, Qt::NoItemFlags);
    // Its tree menu can't show another variant in the pose.
    workspace.select({0, 1, 1});
    std::unique_ptr<QMenu> menu(workspace.createContextMenu());
    QVERIFY(menuItems(menu.get()).contains(QStringLiteral("[Show in Pose \"nod\"]")));

    // Other poses aren't locked.
    workspace.select({0, 1, -1});
    workspace.selectPose(0);
    QCOMPARE(label->text(), QStringLiteral("In pose \"rest\"."));
    QVERIFY(!lock->isChecked() && angle->isEnabled());
    // The lock follows a renamed part.
    workspace.renameSelected(QStringLiteral("noggin"));
    QCOMPARE(doll.puppet().poses[1].lockedParts, QStringList{"noggin"});
}

void PuppetWorkspaceTests::testGridButtonShowsTheGrid() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    auto* button = workspace.findChild<QPushButton*>(QStringLiteral("puppetGrid"));
    QVERIFY(button && button->isCheckable());
    QVERIFY(!button->isChecked());
    QVERIFY(!workspace.canvas()->grid().visible);

    // Settings give the cells and colour, and whether the grid starts shown.
    AppSettings settings;
    settings.showGrid = true;
    settings.gridCellWidth = 8;
    settings.gridCellHeight = 4;
    settings.gridOffsetX = 2;
    settings.gridColor = QColor(255, 0, 0);
    workspace.setSettings(settings);
    QVERIFY(button->isChecked());
    const PuppetCanvas::Grid& grid = workspace.canvas()->grid();
    QVERIFY(grid.visible);
    QCOMPARE(grid.cell, QSize(8, 4));
    QCOMPARE(grid.offset, QPoint(2, 0));

    // The button only shows or hides it, and changes nothing in the project.
    button->click();
    QVERIFY(!workspace.canvas()->grid().visible);
    QCOMPARE(workspace.canvas()->grid().cell, QSize(8, 4));
    button->click();
    QVERIFY(workspace.canvas()->grid().visible);
    QCOMPARE(doll.project.undoStack.count(), 0);
}

void PuppetWorkspaceTests::testEditingKeepsTheViewStill() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    workspace.resize(1000, 600);
    workspace.show();
    workspace.refreshSprites();
    workspace.select({0, -1, -1});
    PuppetCanvas* canvas = workspace.canvas();
    QVERIFY(QTest::qWaitForWindowExposed(&workspace));
    const auto originOnScreen = [canvas]() { return canvas->mapFromScene(QPointF(0, 0)); };
    const QPoint origin = originOnScreen();
    const double zoom = canvas->zoom();

    // Dragging the body far past the puppet's bounds, as fitted, moves the body, not the view.
    const QPoint start = canvas->mapFromScene(QPointF(0, -20));
    const QPoint end = start + QPoint(300, 120);
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, start);
    QTest::mouseMove(canvas->viewport(), start + QPoint(150, 60));
    QTest::mouseMove(canvas->viewport(), end);
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, end);
    QVERIFY(doll.puppet().parts[0].position->x() > 0);
    QCOMPARE(originOnScreen(), origin);
    QCOMPARE(canvas->zoom(), zoom);

    // Nor does undoing it, selecting another part or turning one.
    doll.project.undoStack.undo();
    QCOMPARE(originOnScreen(), origin);
    workspace.select({0, 1, -1});
    QCOMPARE(originOnScreen(), origin);
    startPose(workspace);
    QCOMPARE(originOnScreen(), origin);
    dragOnCanvas(canvas, QPointF(4, -50), QPointF(60, -40));
    QCOMPARE(originOnScreen(), origin);
    QCOMPARE(canvas->zoom(), zoom);

    // Resizing the window after editing keeps the zoom and what is in the middle of the view.
    const QPointF middle = canvas->mapToScene(canvas->viewport()->rect().center());
    workspace.resize(1200, 700);
    QCoreApplication::processEvents();
    QCOMPARE(canvas->zoom(), zoom);
    QVERIFY(QLineF(canvas->mapToScene(canvas->viewport()->rect().center()), middle).length() < 2 / zoom);

    // Fit still fits the puppet as it is now.
    workspace.findChild<QPushButton*>(QStringLiteral("puppetFit"))->click();
    QVERIFY(canvas->zoom() != zoom);
}

void PuppetWorkspaceTests::testDraggingThePivotOntoAMarkerAttaches() {
    Doll doll;
    Puppet& puppet = doll.project.session.activeAtlas().puppets.first();
    puppet.attachments.clear();
    puppet.parts[1].position = QPoint(40, 0);  // the head on the ground, beside the body
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.select({0, 1, -1});

    // From the head's pivot to just beside the body's neck: it snaps onto the marker.
    dragOnCanvas(workspace.canvas(), QPointF(40, 0), QPointF(2, -37));
    QVERIFY(!doll.puppet().parts[1].position);
    QCOMPARE(doll.puppet().attachments.size(), 1);
    const PuppetAttachment& joint = doll.puppet().attachments.first();
    QCOMPARE(joint.child, QStringLiteral("head"));
    QCOMPARE(joint.childMarker, QStringLiteral("pivot"));
    QCOMPARE(joint.parent, QStringLiteral("body"));
    QCOMPARE(joint.parentMarker, QStringLiteral("neck"));
    QCOMPARE(drawnParts(workspace.canvas())[1], QRectF(-8, -54, 16, 16));
    QCOMPARE(doll.project.undoStack.count(), 1);
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Attach Part"));
    QCOMPARE(treeRows(workspace.tree())[3], QStringLiteral("  head | ↳ body:neck"));
}

namespace {
/// The point markers of @p sprite named @p name, as "x,y".
QStringList markerPositions(const SpritePtr& sprite, const QString& name) {
    QStringList positions;
    for (const NamedPoint& marker : sprite->points) {
        if (marker.name == name) positions.append(QStringLiteral("%1,%2").arg(marker.x).arg(marker.y));
    }
    return positions;
}
}  // namespace

void PuppetWorkspaceTests::testDroppingThePivotOnAPartAddsAMarker() {
    Doll doll;
    // The body also shows a wider sprite and an animation, whose frame already has a "head".
    const SpritePtr wide = doll.project.addSprite(QStringLiteral("body-wide"), 24, 40, QPoint(12, 40));
    const SpritePtr breath = doll.project.addSprite(QStringLiteral("body-breath"), 20, 20, QPoint(5, 10),
                                                    {pointMarker("head", 1, 1)});
    AnimationTimeline breathe;
    breathe.name = QStringLiteral("breathe");
    breathe.frames = {breath->path};
    doll.project.session.activeAtlas().timelines = {breathe};
    Puppet& puppet = doll.project.session.activeAtlas().puppets.first();
    PuppetVariant animation;
    animation.kind = PuppetVariant::Kind::Animation;
    animation.source = QStringLiteral("breathe");
    puppet.parts[0].variants << spriteVariant(wide) << animation;
    puppet.attachments.clear();
    puppet.parts[1].position = QPoint(40, 0);  // the head on the ground, beside the body
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    QSignalSpy markersChanged(&workspace, &PuppetWorkspace::spriteMarkersChanged);
    showAtFullSize(workspace);
    workspace.select({0, 1, -1});

    // Dropped on the body, away from its markers: 3 px right of and 20 px above its pivot.
    dragOnCanvas(workspace.canvas(), QPointF(40, 0), QPointF(3, -20));
    QCOMPARE(markerPositions(doll.body, QStringLiteral("head 2")), QStringList{"13,20"});
    // The same offset from each sprite's pivot; "head" was taken, so every one is "head 2".
    QCOMPARE(markerPositions(wide, QStringLiteral("head 2")), QStringList{"15,20"});
    QCOMPARE(markerPositions(breath, QStringLiteral("head 2")), QStringList{"8,-10"});
    QCOMPARE(markerPositions(breath, QStringLiteral("head")), QStringList{"1,1"});
    QCOMPARE(doll.puppet().attachments.size(), 1);
    const PuppetAttachment& joint = doll.puppet().attachments.first();
    QCOMPARE(joint.childMarker, QStringLiteral("pivot"));
    QCOMPARE(joint.parent, QStringLiteral("body"));
    QCOMPARE(joint.parentMarker, QStringLiteral("head 2"));
    QVERIFY(!doll.puppet().parts[1].position);
    // The head's pivot is where it was dropped.
    QCOMPARE(drawnParts(workspace.canvas())[1], QRectF(-5, -36, 16, 16));
    QCOMPARE(doll.project.undoStack.count(), 1);
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Attach to New Marker"));
    QVERIFY(markersChanged.count() > 0);

    // One undo takes back the markers and the attachment.
    doll.project.undoStack.undo();
    QVERIFY(markerPositions(doll.body, QStringLiteral("head 2")).isEmpty());
    QVERIFY(markerPositions(breath, QStringLiteral("head 2")).isEmpty());
    QVERIFY(doll.puppet().attachments.isEmpty());
    QCOMPARE(doll.puppet().parts[1].position, std::optional<QPoint>(QPoint(40, 0)));
    doll.project.undoStack.redo();
    QCOMPARE(markerPositions(wide, QStringLiteral("head 2")), QStringList{"15,20"});
    QCOMPARE(doll.puppet().attachments.first().parentMarker, QStringLiteral("head 2"));
    doll.project.undoStack.undo();

    // Shift places it by position instead.
    const QPoint start = workspace.canvas()->mapFromScene(QPointF(40, 0));
    const QPoint end = workspace.canvas()->mapFromScene(QPointF(3, -20));
    QWidget* viewport = workspace.canvas()->viewport();
    QTest::mousePress(viewport, Qt::LeftButton, {}, start);
    QTest::mouseMove(viewport, (start + end) / 2);
    QMouseEvent move(QEvent::MouseMove, QPointF(end), viewport->mapToGlobal(QPointF(end)), Qt::NoButton,
                     Qt::LeftButton, Qt::ShiftModifier);
    QCoreApplication::sendEvent(viewport, &move);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::ShiftModifier, end);
    QCOMPARE(doll.puppet().parts[1].position, std::optional<QPoint>(QPoint(3, -20)));
    QVERIFY(markerPositions(doll.body, QStringLiteral("head 2")).isEmpty());
    doll.project.undoStack.undo();

    // A part showing a sprite that isn't in the atlas can't take one: nothing happens.
    PuppetVariant missing;
    missing.source = doll.project.dir.filePath(QStringLiteral("gone.png"));
    doll.project.session.activeAtlas().puppets.first().parts[0].variants.append(missing);
    workspace.refresh();
    const int edits = doll.project.undoStack.count();
    dragOnCanvas(workspace.canvas(), QPointF(40, 0), QPointF(3, -20));
    QCOMPARE(doll.project.undoStack.count(), edits);
    QVERIFY(markerPositions(doll.body, QStringLiteral("head 2")).isEmpty());
    auto* parentMarker = workspace.findChild<QComboBox*>(QStringLiteral("partParentMarkerCombo"));
    const int item = parentMarker->count() - 1;
    QCOMPARE(parentMarker->itemText(item), QStringLiteral("New marker at its pivot"));
    QVERIFY(parentMarker->itemData(item, Qt::ToolTipRole).toString().contains(QLatin1String("gone")));
}

void PuppetWorkspaceTests::testDoubleClickingThePivotHangsThePartWhereItIs() {
    Doll doll;
    // A hat beside the body, and the head on the ground beside both.
    const SpritePtr hat = doll.project.addSprite(QStringLiteral("hat"), 10, 10, QPoint(5, 5));
    Puppet& puppet = doll.project.session.activeAtlas().puppets.first();
    puppet.parts.append(PuppetPart{QStringLiteral("hat"), {spriteVariant(hat)}, QPoint(30, -30)});
    PuppetEditing::keepRestPose(puppet);
    puppet.attachments.clear();
    puppet.parts[1].position = QPoint(60, 0);
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.select({0, 1, -1});
    PuppetCanvas* canvas = workspace.canvas();
    QSignalSpy doubleClicked(canvas, &PuppetCanvas::pivotDoubleClicked);
    const auto doubleClickPivot = [&](const QPointF& pivot) {
        QTest::mouseDClick(canvas->viewport(), Qt::LeftButton, {}, canvas->mapFromScene(pivot));
    };

    // Over nothing: the user picks from every part it can hang from.
    doubleClickPivot(QPointF(60, 0));
    QCOMPARE(doubleClicked.count(), 1);
    QVERIFY(doubleClicked.last()[1].value<QVector<int>>().isEmpty());
    auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    QVERIFY(popup);
    QCOMPARE(menuItems(popup), (QStringList{"-", "body", "hat"}));
    popup->close();
    QVERIFY(doll.puppet().attachments.isEmpty());

    // Over the body alone: it hangs from a new marker there at once, without moving.
    doll.project.session.activeAtlas().puppets.first().parts[1].position = QPoint(3, -20);
    workspace.refresh();
    doubleClickPivot(QPointF(3, -20));
    QCOMPARE(doubleClicked.last()[1].value<QVector<int>>(), QVector<int>{0});
    QCOMPARE(doll.puppet().attachments.first().parent, QStringLiteral("body"));
    QCOMPARE(doll.puppet().attachments.first().parentMarker, QStringLiteral("head"));
    QCOMPARE(markerPositions(doll.body, QStringLiteral("head")), QStringList{"13,20"});
    QCOMPARE(drawnParts(canvas)[1], QRectF(-5, -36, 16, 16));
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Attach to New Marker"));
    doll.project.undoStack.undo();

    // Over the body and the hat: the user picks one of them, the hat on top first.
    doll.project.session.activeAtlas().puppets.first().parts[2].position = QPoint(3, -20);
    workspace.refresh();
    doubleClickPivot(QPointF(3, -20));
    QCOMPARE(doubleClicked.last()[1].value<QVector<int>>(), (QVector<int>{2, 0}));
    popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    QVERIFY(popup);
    QCOMPARE(menuItems(popup), (QStringList{"-", "hat", "body"}));
    popup->actions()[1]->trigger();
    popup->close();
    QCOMPARE(doll.puppet().attachments.first().parent, QStringLiteral("hat"));
    QCOMPARE(markerPositions(hat, QStringLiteral("head")), QStringList{"5,5"});
}

void PuppetWorkspaceTests::testPanelHangsAPartFromANewMarker() {
    Doll doll;
    Puppet& puppet = doll.project.session.activeAtlas().puppets.first();
    puppet.attachments.clear();
    puppet.parts[1].position = QPoint(40, 0);
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    workspace.refreshSprites();
    workspace.select({0, 1, -1});
    auto* parentMarker = workspace.findChild<QComboBox*>(QStringLiteral("partParentMarkerCombo"));
    QVERIFY(parentMarker);
    // The last item, after the body's markers; picking it hangs the head where its pivot is.
    const int item = parentMarker->count() - 1;
    QCOMPARE(parentMarker->itemText(item), QStringLiteral("New marker at its pivot"));
    QCOMPARE(parentMarker->itemText(item - 1), QStringLiteral("neck"));
    parentMarker->setCurrentIndex(item);
    emit parentMarker->activated(item);
    QCOMPARE(markerPositions(doll.body, QStringLiteral("head")), QStringList{"50,40"});
    QCOMPARE(doll.puppet().attachments.first().parentMarker, QStringLiteral("head"));
    QCOMPARE(drawnParts(workspace.canvas())[1], QRectF(32, -16, 16, 16));
    QCOMPARE(parentMarker->currentText(), QStringLiteral("head"));
    QCOMPARE(doll.project.undoStack.count(), 1);
}

void PuppetWorkspaceTests::testDraggingThePivotAwayDetaches() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.select({0, 1, -1});

    // The head's pivot is on the neck, at 0,-38.
    dragOnCanvas(workspace.canvas(), QPointF(0, -38), QPointF(60, 10));
    QCOMPARE(doll.puppet().parts[1].position, std::optional<QPoint>(QPoint(60, 10)));
    QVERIFY(doll.puppet().attachments.isEmpty());
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Detach Part"));

    // The body can't hang from the head that hangs from it.
    doll.project.undoStack.undo();
    workspace.select({0, 0, -1});
    auto* attached = workspace.findChild<QRadioButton*>(QStringLiteral("partAttached"));
    QVERIFY(attached && !attached->isEnabled());
    dragOnCanvas(workspace.canvas(), QPointF(0, 0), QPointF(0, -38));
    QCOMPARE(doll.puppet().attachments.size(), 1);
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(0, -38)));
}

void PuppetWorkspaceTests::testPanelAttachesAndDetaches() {
    Doll doll;
    doll.head->points.append(pointMarker("chin", 8, 14));  // only the first head has a chin
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    workspace.refreshSprites();
    workspace.select({0, 1, -1});
    auto* byPosition = workspace.findChild<QRadioButton*>(QStringLiteral("partByPosition"));
    auto* attached = workspace.findChild<QRadioButton*>(QStringLiteral("partAttached"));
    auto* parentMarker = workspace.findChild<QComboBox*>(QStringLiteral("partParentMarkerCombo"));
    auto* childMarker = workspace.findChild<QComboBox*>(QStringLiteral("partChildMarkerCombo"));
    QVERIFY(byPosition && attached && parentMarker && childMarker);
    QVERIFY(attached->isChecked());
    QCOMPARE(parentMarker->currentText(), QStringLiteral("neck"));
    QCOMPARE(childMarker->count(), 2);
    QCOMPARE(childMarker->itemText(1), QStringLiteral("⚠ chin"));
    QCOMPARE(childMarker->itemData(1, Qt::ToolTipRole).toString(), QStringLiteral("Missing in: head-smiling"));

    // Hanging by the chin moves the head so its chin, 2 px above its pivot, is on the neck.
    childMarker->setCurrentIndex(1);
    QCOMPARE(doll.puppet().attachments.first().childMarker, QStringLiteral("chin"));
    QCOMPARE(drawnParts(workspace.canvas())[1], QRectF(-8, -52, 16, 16));

    // By position keeps the head where it is.
    byPosition->setChecked(true);
    QVERIFY(doll.puppet().attachments.isEmpty());
    QCOMPARE(doll.puppet().parts[1].position, std::optional<QPoint>(QPoint(0, -36)));
    QCOMPARE(drawnParts(workspace.canvas())[1], QRectF(-8, -52, 16, 16));

    // Attached hangs it from the first part it can, by its pivot, at that part's pivot.
    attached->setChecked(true);
    const PuppetAttachment& joint = doll.puppet().attachments.first();
    QCOMPARE(joint.parent, QStringLiteral("body"));
    QCOMPARE(joint.parentMarker, QStringLiteral("pivot"));
    QCOMPARE(joint.childMarker, QStringLiteral("pivot"));
    QCOMPARE(doll.project.undoStack.count(), 3);
}

void PuppetWorkspaceTests::testPanelSetsJointLimits() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    workspace.refreshSprites();
    workspace.select({0, 1, -1});
    auto* leeway = workspace.findChild<QSpinBox*>(QStringLiteral("partLeeway"));
    auto* limit = workspace.findChild<QCheckBox*>(QStringLiteral("partLimitAngle"));
    auto* angleMin = workspace.findChild<QSpinBox*>(QStringLiteral("partAngleMin"));
    auto* angleMax = workspace.findChild<QSpinBox*>(QStringLiteral("partAngleMax"));
    QVERIFY(leeway && limit && angleMin && angleMax);

    leeway->setValue(3);
    limit->setChecked(true);
    angleMin->setValue(-10);
    angleMax->setValue(20);
    const PuppetAttachment& joint = doll.puppet().attachments.first();
    QCOMPARE(joint.leeway, 3);
    QCOMPARE(joint.angleMin, std::optional<int>(-10));
    QCOMPARE(joint.angleMax, std::optional<int>(20));
    QCOMPARE(doll.project.undoStack.count(), 1);

    limit->setChecked(false);
    QVERIFY(!doll.puppet().attachments.first().angleMin);
}

namespace {
/// Where the doll's 16x16 head is drawn when turned @p degrees around its joint on the neck.
QRectF turnedHead(qreal degrees, const QPointF& joint = QPointF(0, -38)) {
    QTransform transform;
    transform.translate(joint.x(), joint.y());
    transform.rotate(degrees);
    transform.translate(-8, -16);
    return transform.mapRect(QRectF(0, 0, 16, 16));
}
}  // namespace

void PuppetWorkspaceTests::testTurningAPartEditsTheSelectedPose() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.select({0, 1, -1});
    const Puppet rig = doll.puppet();

    // Rest is a pose like the others: from above the joint to its right, a quarter turn
    // clockwise, saved in the Rest pose and not in the rig.
    QVERIFY(workspace.findChild<QSpinBox*>(QStringLiteral("partAngle"))->isEnabled());
    dragOnCanvas(workspace.canvas(), QPointF(0, -50), QPointF(12, -38));
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(90));
    QCOMPARE(doll.puppet().parts, rig.parts);
    QCOMPARE(doll.puppet().attachments, rig.attachments);
    QCOMPARE(doll.puppet().poses[0].name, PUPPET_REST_POSE);
    QCOMPARE(doll.puppet().poses[0].settings[1].angle, std::optional<int>(90));
    QCOMPARE(doll.project.undoStack.count(), 1);
    doll.project.undoStack.undo();
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(0));

    // In a new pose, the same turn is saved in that pose, which sets only the head.
    startPose(workspace);
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(0));
    dragOnCanvas(workspace.canvas(), QPointF(0, -50), QPointF(12, -38));
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(90));
    QCOMPARE(doll.puppet().parts, rig.parts);
    QCOMPARE(doll.puppet().attachments, rig.attachments);
    QCOMPARE(doll.puppet().poses[1].settings.size(), 1);
    QCOMPARE(doll.puppet().poses[1].settings[0].part, QStringLiteral("head"));
    QCOMPARE(doll.puppet().poses[1].settings[0].angle, std::optional<int>(90));
    QCOMPARE(doll.project.undoStack.count(), 2);  // the new pose, then the whole drag
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Pull Part"));

    doll.project.undoStack.undo();
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(0));
    QVERIFY(doll.puppet().poses[1].settings.isEmpty());
}

void PuppetWorkspaceTests::testTurningStopsAtTheAngleLimits() {
    Doll doll;
    doll.project.session.activeAtlas().puppets.first().attachments.first().angleMin = -15;
    doll.project.session.activeAtlas().puppets.first().attachments.first().angleMax = 15;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.select({0, 1, -1});
    startPose(workspace);

    dragOnCanvas(workspace.canvas(), QPointF(0, -50), QPointF(12, -38));
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(15));
    dragOnCanvas(workspace.canvas(), QPointF(2, -50), QPointF(-12, -38));
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(-15));
}

void PuppetWorkspaceTests::testDriftingStaysWithinTheLeeway() {
    Doll doll;
    doll.project.session.activeAtlas().puppets.first().attachments.first().leeway = 2;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.select({0, 1, -1});
    startPose(workspace);
    PuppetCanvas* canvas = workspace.canvas();
    const QPoint start = canvas->mapFromScene(QPointF(0, -46));
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::ShiftModifier, start);
    QTest::mouseMove(canvas->viewport(), start + QPoint(5, 0));
    QTest::mouseMove(canvas->viewport(), start + QPoint(10, 0));
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::ShiftModifier, start + QPoint(10, 0));
    // 10 px asked, 2 px allowed from the neck, a point marker.
    QCOMPARE(drawnParts(canvas)[1], turnedHead(0, QPointF(2, -38)));
    QCOMPARE(doll.puppet().poses[1].settings[0].offset, std::optional<QPoint>(QPoint(2, 0)));
    QCOMPARE(doll.project.undoStack.count(), 2);
}

void PuppetWorkspaceTests::testTurningAPositionedPartByItsHandle() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.select({0, 0, -1});
    const Puppet rig = doll.puppet();
    auto* angle = workspace.findChild<QSpinBox*>(QStringLiteral("partAngle"));
    QVERIFY(angle);

    // At Rest, dragging the body (away from its handle) moves the rig's body, and the Rest
    // pose places it there too.
    dragOnCanvas(workspace.canvas(), QPointF(0, -20), QPointF(36, 16));
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(36, 36)));
    QCOMPARE(doll.puppet().poses[0].settings[0].position, std::optional<QPoint>(QPoint(36, 36)));
    doll.project.undoStack.undo();
    QVERIFY(doll.puppet() == rig);

    // The body points up from its pivot at the origin; its handle is 36 view pixels along.
    startPose(workspace);
    dragOnCanvas(workspace.canvas(), QPointF(0, -36), QPointF(36, 0));
    QTransform quarterTurn;
    quarterTurn.rotate(90);
    QCOMPARE(drawnParts(workspace.canvas())[0], quarterTurn.mapRect(QRectF(-10, -40, 20, 40)));
    // The head hangs from the neck, so it turned with the body.
    QCOMPARE(drawnParts(workspace.canvas())[1], quarterTurn.mapRect(turnedHead(0)));
    QCOMPARE(angle->value(), 90);
    QCOMPARE(doll.puppet().parts, rig.parts);

    // Dragging the body itself moves it, in the pose.
    dragOnCanvas(workspace.canvas(), QPointF(20, 0), QPointF(20, 15));
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(0, 0)));
    QCOMPARE(doll.puppet().poses[1].settings[0].position, std::optional<QPoint>(QPoint(0, 15)));
    QCOMPARE(angle->value(), 90);
}

void PuppetWorkspaceTests::testPullingAHandTurnsTheArm() {
    // A shoulder at the origin and an arm pointing right: upper arm, forearm and hand, 10 px
    // each, each hanging by its pivot from the previous one's "end".
    PuppetProject project;
    const SpritePtr body = project.addSprite(QStringLiteral("body"), 4, 4, QPoint(2, 2), {pointMarker("end", 2, 2)});
    Puppet puppet;
    puppet.name = QStringLiteral("arm");
    puppet.parts.append({QStringLiteral("body"), {spriteVariant(body)}, QPoint(0, 0)});
    QString previous = QStringLiteral("body");
    for (const QString& name : {QStringLiteral("upper"), QStringLiteral("fore"), QStringLiteral("hand")}) {
        const SpritePtr bone = project.addSprite(name, 10, 2, QPoint(0, 1), {pointMarker("end", 10, 1)});
        puppet.parts.append({name, {spriteVariant(bone)}, std::nullopt});
        puppet.attachments.append({name, QStringLiteral("pivot"), previous, QStringLiteral("end")});
        previous = name;
    }
    project.session.activeAtlas().puppets = {puppet};
    PuppetWorkspace workspace(&project.session, &project.undoStack);
    showAtFullSize(workspace);
    startPose(workspace);
    PuppetCanvas* canvas = workspace.canvas();

    // Pull the hand by its middle, from 25,0 to below the shoulder: the arm bends to get it there.
    const QPointF target(5, 15);
    dragOnCanvas(canvas, QPointF(25, 0), target);
    bool handThere = false;
    for (QGraphicsItem* item : canvas->scene()->items(target))
        handThere = handThere || item->data(0).toInt() == 3;
    QVERIFY(handThere);
    QCOMPARE(workspace.selection(), (PuppetSelection{0, 3, -1}));
    QCOMPARE(project.undoStack.count(), 2);
    QCOMPARE(project.session.activeAtlas().puppets.first().parts, puppet.parts);
    // The pose sets what the pull turned, and only that: the body, placed by position, stays.
    for (const PuppetPoseSetting& setting : project.session.activeAtlas().puppets.first().poses[1].settings)
        QVERIFY(setting.part != QStringLiteral("body"));

    // The upper arm turned: its Angle field says so.
    workspace.select({0, 1, -1});
    QVERIFY(workspace.findChild<QSpinBox*>(QStringLiteral("partAngle"))->value() != 0);
}

void PuppetWorkspaceTests::testPullingSlidesWithinTheLeeway() {
    Doll doll;
    doll.project.session.activeAtlas().puppets.first().attachments.first().leeway = 12;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    startPose(workspace);

    // Pulled 12 px right by its middle, the head slides off the neck instead of turning...
    dragOnCanvas(workspace.canvas(), QPointF(0, -46), QPointF(12, -46));
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(0, QPointF(12, -38)));
    // ...and beyond the leeway, it turns too.
    dragOnCanvas(workspace.canvas(), QPointF(12, -46), QPointF(60, -38));
    QCOMPARE(workspace.findChild<QSpinBox*>(QStringLiteral("partAngle"))->value() != 0, true);
    QCOMPARE(doll.project.undoStack.count(), 3);
}

void PuppetWorkspaceTests::testHiddenMarkersAreNotOffered() {
    Doll doll;
    Puppet& puppet = doll.project.session.activeAtlas().puppets.first();
    puppet.attachments.clear();
    puppet.parts[1].position = QPoint(40, 0);  // the head on the ground, beside the body
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);

    // Every marker is ticked to start with; unticking one hides it, as one undo step.
    workspace.select({0, 0, -1});
    auto* markers = workspace.findChild<QListWidget*>(QStringLiteral("partMarkers"));
    QVERIFY(markers);
    QCOMPARE(markers->count(), 1);
    QCOMPARE(markers->item(0)->text(), QStringLiteral("neck"));
    QCOMPARE(markers->item(0)->checkState(), Qt::Checked);
    markers->item(0)->setCheckState(Qt::Unchecked);
    QCOMPARE(doll.puppet().parts[0].hiddenMarkers, QStringList{QStringLiteral("neck")});
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Show Markers"));

    // The head can't hang from it: dropping its pivot there hangs it from a new marker instead.
    workspace.select({0, 1, -1});
    auto* parentMarker = workspace.findChild<QComboBox*>(QStringLiteral("partParentMarkerCombo"));
    QCOMPARE(parentMarker->findData(QStringLiteral("neck")), -1);
    dragOnCanvas(workspace.canvas(), QPointF(40, 0), QPointF(0, -38));
    QCOMPARE(doll.puppet().attachments.size(), 1);
    QCOMPARE(doll.puppet().attachments.first().parentMarker, QStringLiteral("head"));

    // Ticked again, it is offered again.
    doll.project.undoStack.undo();
    doll.project.undoStack.undo();
    workspace.select({0, 1, -1});
    QVERIFY(parentMarker->findData(QStringLiteral("neck")) >= 0);
}

void PuppetWorkspaceTests::testMarkerListDoesNotSpreadThePanelRows() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    workspace.resize(1000, 900);  // a tall panel, with room to spare
    workspace.show();
    workspace.refreshSprites();
    auto* byPosition = workspace.findChild<QRadioButton*>(QStringLiteral("partByPosition"));
    auto* attached = workspace.findChild<QRadioButton*>(QStringLiteral("partAttached"));
    auto* markers = workspace.findChild<QListWidget*>(QStringLiteral("partMarkers"));
    const auto gap = [&]() {
        // The rows sit in a group box in the panel: each round of events settles one layout level.
        for (int round = 0; round < 3; ++round) QCoreApplication::processEvents();
        return attached->mapTo(&workspace, QPoint()).y() - byPosition->mapTo(&workspace, QPoint()).y();
    };

    // The head's sprite has no markers; the body's has one, listed.
    workspace.select({0, 1, -1});
    const int withoutMarkers = gap();
    QVERIFY(!markers->isVisibleTo(&workspace));
    workspace.select({0, 0, -1});
    QVERIFY(markers->isVisibleTo(&workspace));
    QCOMPARE(gap(), withoutMarkers);
    // The list is as tall as its one marker.
    QVERIFY(markers->height() < 2 * markers->sizeHintForRow(0) + 2 * markers->frameWidth());
}

void PuppetWorkspaceTests::testNewPoseSetsOnlyThePartsChanged() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    // Rest is shown first, and sets every part; it can be duplicated but not removed.
    QCOMPARE(frameNames(workspace.poseStrip()), QStringList{"rest"});
    QCOMPARE(workspace.selectedPose(), 0);
    QCOMPARE(workspace.poseParts()->item(0)->checkState(), Qt::Checked);
    QCOMPARE(workspace.poseParts()->item(1)->checkState(), Qt::Checked);
    QVERIFY(workspace.findChild<QPushButton*>(QStringLiteral("puppetDuplicatePose"))->isEnabled());
    QVERIFY(!workspace.findChild<QPushButton*>(QStringLiteral("puppetRemovePose"))->isEnabled());

    // A new pose sets nothing: every part is faint, and unticked under the tree.
    startPose(workspace);
    QCOMPARE(frameNames(workspace.poseStrip()), (QStringList{"rest", "pose"}));
    QCOMPARE(workspace.selectedPose(), 1);
    QVERIFY(doll.puppet().poses[1].settings.isEmpty());
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("New Pose"));
    QListWidget* parts = workspace.poseParts();
    QCOMPARE(parts->count(), 2);
    QCOMPARE(parts->item(0)->checkState(), Qt::Unchecked);
    QVERIFY(partOpacity(workspace.canvas(), 1) < 1);

    // Turning the head adds it to the pose; the body stays out of it.
    workspace.select({0, 1, -1});
    workspace.findChild<QSpinBox*>(QStringLiteral("partAngle"))->setValue(30);
    QCOMPARE(doll.puppet().poses[1].settings.size(), 1);
    QCOMPARE(doll.puppet().poses[1].settings[0].part, QStringLiteral("head"));
    QCOMPARE(doll.puppet().poses[1].settings[0].angle, std::optional<int>(30));
    QCOMPARE(parts->item(1)->checkState(), Qt::Checked);
    QCOMPARE(parts->item(1)->text(), QStringLiteral("head · 30°"));
    QCOMPARE(parts->item(0)->checkState(), Qt::Unchecked);
    QCOMPARE(partOpacity(workspace.canvas(), 1), 1.0);
    QVERIFY(partOpacity(workspace.canvas(), 0) < 1);

    // The next pose goes after the selected one, with its own name.
    startPose(workspace);
    QCOMPARE(frameNames(workspace.poseStrip()), (QStringList{"rest", "pose", "pose 2"}));
    doll.project.undoStack.undo();
    QCOMPARE(doll.puppet().poses.size(), 2);
}

void PuppetWorkspaceTests::testSelectingAPoseShowsIt() {
    Doll doll;
    addDollPoses(doll);
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.select({0, 1, -1});
    PuppetPoseStrip* strip = workspace.poseStrip();
    QCOMPARE(frameNames(strip), (QStringList{"rest", "nod", "smile"}));
    auto* angle = workspace.findChild<QSpinBox*>(QStringLiteral("partAngle"));
    auto* shows = workspace.findChild<QComboBox*>(QStringLiteral("partShownVariant"));

    strip->setCurrentRow(1);
    QCOMPARE(workspace.selectedPose(), 1);
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(20));
    QCOMPARE(angle->value(), 20);
    strip->setCurrentRow(2);
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(0));
    QCOMPARE(shows->currentIndex(), 1);  // head-smiling
    strip->setCurrentRow(0);
    QCOMPARE(workspace.selectedPose(), 0);
    QCOMPARE(shows->currentIndex(), 0);
    QCOMPARE(doll.project.undoStack.count(), 0);
}

void PuppetWorkspaceTests::testTickingPartsInAndOutOfThePose() {
    Doll doll;
    addDollPoses(doll);
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.selectPose(1);  // "nod" sets the head only
    QListWidget* parts = workspace.poseParts();
    QCOMPARE(parts->item(0)->checkState(), Qt::Unchecked);
    QCOMPARE(parts->item(1)->checkState(), Qt::Checked);

    // Unticking the head leaves it out: it goes back to rest in this pose.
    parts->item(1)->setCheckState(Qt::Unchecked);
    QCoreApplication::processEvents();
    QVERIFY(doll.puppet().poses[1].settings.isEmpty());
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(0));
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Remove Part from Pose"));

    // Ticking the body sets it as it stands: at its rest angle.
    workspace.poseParts()->item(0)->setCheckState(Qt::Checked);
    QCoreApplication::processEvents();
    QCOMPARE(doll.puppet().poses[1].settings.size(), 1);
    QCOMPARE(doll.puppet().poses[1].settings[0].part, QStringLiteral("body"));
    QCOMPARE(doll.puppet().poses[1].settings[0].angle, std::optional<int>(0));
    QCOMPARE(partOpacity(workspace.canvas(), 0), 1.0);

    doll.project.undoStack.undo();
    doll.project.undoStack.undo();
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(20));

    // The Rest pose's parts are ticked in and out the same way.
    workspace.selectPose(0);
    QCOMPARE(workspace.poseParts()->item(1)->checkState(), Qt::Checked);
    workspace.poseParts()->item(1)->setCheckState(Qt::Unchecked);
    QCoreApplication::processEvents();
    QCOMPARE(doll.puppet().poses[0].settings.size(), 1);
    QCOMPARE(doll.puppet().poses[0].settings[0].part, QStringLiteral("body"));
    QVERIFY(partOpacity(workspace.canvas(), 1) < 1);
}

void PuppetWorkspaceTests::testClickingAPartOutOfThePose() {
    // As a user does it: a new pose, the head turned, then a click on its checkbox.
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.select({0, 1, -1});
    startPose(workspace);
    workspace.findChild<QSpinBox*>(QStringLiteral("partAngle"))->setValue(30);
    QListWidget* parts = workspace.poseParts();
    QCOMPARE(parts->item(1)->checkState(), Qt::Checked);

    // The checkbox sits at the row's left edge.
    const QRect row = parts->visualItemRect(parts->item(1));
    QTest::mouseClick(parts->viewport(), Qt::LeftButton, {}, QPoint(row.left() + row.height() / 2, row.center().y()));
    QCoreApplication::processEvents();
    QVERIFY(doll.puppet().poses[1].settings.isEmpty());
    QCOMPARE(parts->item(1)->checkState(), Qt::Unchecked);
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(0));
    QCOMPARE(workspace.selection(), (PuppetSelection{0, 1, -1}));
}

void PuppetWorkspaceTests::testPosingMovesPositionedPartsInThePose() {
    Doll doll;
    addDollPoses(doll);
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.selectPose(1);

    // In a pose, dragging the body moves it in the pose, not in the rig.
    dragOnCanvas(workspace.canvas(), QPointF(0, -20), QPointF(20, -20));
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(0, 0)));
    QCOMPARE(drawnParts(workspace.canvas())[0], QRectF(10, -40, 20, 40));
    QCOMPARE(workspace.findChild<QSpinBox*>(QStringLiteral("partPositionX"))->value(), 20);
    const PuppetPose& nod = doll.puppet().poses[1];
    QCOMPARE(nod.settings.size(), 2);
    QCOMPARE(nod.settings[0].position, std::optional<QPoint>(QPoint(20, 0)));
    QCOMPARE(nod.settings[1].angle, std::optional<int>(20));
    QCOMPARE(doll.project.undoStack.count(), 1);

    // At Rest, it moves the rig again, and the Rest pose with it.
    workspace.selectPose(0);
    dragOnCanvas(workspace.canvas(), QPointF(0, -20), QPointF(0, -5));
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(0, 15)));
    QCOMPARE(doll.puppet().poses[0].settings[0].position, std::optional<QPoint>(QPoint(0, 15)));
}

void PuppetWorkspaceTests::testPoseStripRenamesReordersAndRemoves() {
    Doll doll;
    addDollPoses(doll);
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    PuppetPoseStrip* strip = workspace.poseStrip();

    // Renaming in place; a taken name is put back.
    strip->item(1)->setText(QStringLiteral("smile"));
    QCoreApplication::processEvents();
    QCOMPARE(frameNames(strip), (QStringList{"rest", "nod", "smile"}));
    QVERIFY(!workspace.findChild<QLabel*>(QStringLiteral("puppetPoseStatus"))->text().isEmpty());
    strip->item(1)->setText(QStringLiteral("yes"));
    QCoreApplication::processEvents();
    QCOMPARE(PuppetEditing::poseNames(doll.puppet()), (QStringList{"rest", "yes", "smile"}));
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Rename Pose"));
    // Rest can't be renamed: it isn't editable in place, and a rename is refused.
    QVERIFY(!(strip->item(0)->flags() & Qt::ItemIsEditable));
    emit strip->renameRequested(0, QStringLiteral("idle"));
    QCOMPARE(PuppetEditing::poseNames(doll.puppet()).first(), PUPPET_REST_POSE);

    // Reordering keeps the moved pose selected; Rest stays first.
    workspace.selectPose(1);
    emit strip->moveRequested(1, 2);
    QCOMPARE(PuppetEditing::poseNames(doll.puppet()), (QStringList{"rest", "smile", "yes"}));
    QCOMPARE(workspace.selectedPose(), 2);
    emit strip->moveRequested(2, 0);
    QCOMPARE(PuppetEditing::poseNames(doll.puppet()), (QStringList{"rest", "smile", "yes"}));

    // Duplicating copies the selected pose after it.
    workspace.findChild<QPushButton*>(QStringLiteral("puppetDuplicatePose"))->click();
    QCOMPARE(PuppetEditing::poseNames(doll.puppet()), (QStringList{"rest", "smile", "yes", "yes 2"}));
    QCOMPARE(doll.puppet().poses[3].settings, doll.puppet().poses[2].settings);
    QCOMPARE(workspace.selectedPose(), 3);
    doll.project.undoStack.undo();
    workspace.selectPose(2);

    // Removing selects the frame before it.
    workspace.findChild<QPushButton*>(QStringLiteral("puppetRemovePose"))->click();
    QCOMPARE(PuppetEditing::poseNames(doll.puppet()), (QStringList{"rest", "smile"}));
    QCOMPARE(workspace.selectedPose(), 1);
    QCOMPARE(frameNames(strip), (QStringList{"rest", "smile"}));
    doll.project.undoStack.undo();
    QCOMPARE(PuppetEditing::poseNames(doll.puppet()), (QStringList{"rest", "smile", "yes"}));

    // Rest can't be removed.
    emit strip->removeRequested(0);
    QCOMPARE(PuppetEditing::poseNames(doll.puppet()), (QStringList{"rest", "smile", "yes"}));
}

namespace {
/// Triggers the action called @p text in frame @p frame's context menu.
void triggerFrameAction(PuppetPoseStrip* strip, int frame, const QString& text) {
    std::unique_ptr<QMenu> menu(strip->createContextMenu(frame));
    for (QAction* action : menu->actions()) {
        if (action->text() == text) {
            action->trigger();
            return;
        }
    }
    QFAIL(qPrintable(QStringLiteral("No \"%1\" for frame %2").arg(text).arg(frame)));
}
}  // namespace

void PuppetWorkspaceTests::testPoseStripContextMenuWorksLikeTheFrameList() {
    Doll doll;
    addDollPoses(doll);
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    PuppetPoseStrip* strip = workspace.poseStrip();

    // The same actions as an animation's frames; Rest stays first and can't go.
    std::unique_ptr<QMenu> poseMenu(strip->createContextMenu(1));
    QCOMPARE(menuItems(poseMenu.get()), (QStringList{"Add Pose Before", "Add Pose After", "-", "[Move to the left]",
                                                     "Move to the right", "Duplicate", "-", "Delete"}));
    std::unique_ptr<QMenu> lastMenu(strip->createContextMenu(2));
    QVERIFY(menuItems(lastMenu.get()).contains(QStringLiteral("[Move to the right]")));
    std::unique_ptr<QMenu> restMenu(strip->createContextMenu(0));
    QCOMPARE(menuItems(restMenu.get()), (QStringList{"[Add Pose Before]", "Add Pose After", "-", "[Move to the left]",
                                                     "[Move to the right]", "Duplicate", "-", "[Delete]"}));
    std::unique_ptr<QMenu> emptyMenu(strip->createContextMenu(-1));
    QCOMPARE(menuItems(emptyMenu.get()), QStringList{"Add Pose"});

    triggerFrameAction(strip, 1, QStringLiteral("Add Pose Before"));
    QCOMPARE(frameNames(strip), (QStringList{"rest", "pose", "nod", "smile"}));
    QCOMPARE(workspace.selectedPose(), 1);
    triggerFrameAction(strip, 0, QStringLiteral("Add Pose After"));
    QCOMPARE(frameNames(strip), (QStringList{"rest", "pose 2", "pose", "nod", "smile"}));
    triggerFrameAction(strip, 3, QStringLiteral("Move to the left"));  // nod
    QCOMPARE(frameNames(strip), (QStringList{"rest", "pose 2", "nod", "pose", "smile"}));
    triggerFrameAction(strip, 2, QStringLiteral("Move to the right"));  // nod again
    QCOMPARE(frameNames(strip), (QStringList{"rest", "pose 2", "pose", "nod", "smile"}));
    triggerFrameAction(strip, 3, QStringLiteral("Duplicate"));
    QCOMPARE(frameNames(strip), (QStringList{"rest", "pose 2", "pose", "nod", "nod 2", "smile"}));
    QCOMPARE(doll.puppet().poses[4].settings, doll.puppet().poses[3].settings);
    QCOMPARE(workspace.selectedPose(), 4);
    triggerFrameAction(strip, 1, QStringLiteral("Delete"));
    QCOMPARE(frameNames(strip), (QStringList{"rest", "pose", "nod", "nod 2", "smile"}));
    triggerFrameAction(strip, -1, QStringLiteral("Add Pose"));
    QCOMPARE(frameNames(strip).last(), QStringLiteral("pose 2"));
    triggerFrameAction(strip, 0, QStringLiteral("Duplicate"));
    QCOMPARE(frameNames(strip)[1], QStringLiteral("rest 2"));
    QCOMPARE(doll.puppet().poses[1].settings, doll.puppet().poses[0].settings);
    doll.project.undoStack.undo();

    // Backspace deletes the selected pose, as Delete does, but not Rest.
    workspace.selectPose(1);
    strip->setFocus();
    QTest::keyClick(strip, Qt::Key_Backspace);
    QCOMPARE(frameNames(strip), (QStringList{"rest", "nod", "nod 2", "smile", "pose 2"}));
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Remove Pose"));
    workspace.selectPose(0);
    QTest::keyClick(strip, Qt::Key_Backspace);
    QCOMPARE(frameNames(strip), (QStringList{"rest", "nod", "nod 2", "smile", "pose 2"}));
}

namespace {
/// Triggers @p menu's submenu @p title › @p pose: adding a step showing that pose.
void addPoseFromMenu(QMenu* menu, const QString& title, const QString& pose) {
    for (QAction* action : menu->actions()) {
        if (action->text() != title || !action->menu()) continue;
        for (QAction* poseAction : action->menu()->actions()) {
            if (poseAction->text() == pose) {
                poseAction->trigger();
                return;
            }
        }
    }
    QFAIL(qPrintable(QStringLiteral("No %1 › %2").arg(title, pose)));
}

/// The selected animation's steps as "pose in/hold".
QStringList stepRows(const Doll& doll, int sequence = 0) {
    QStringList rows;
    for (const PuppetSequenceStep& step : doll.project.session.atlases.first().puppets.first().sequences[sequence].steps)
        rows.append(QStringLiteral("%1 %2/%3%4").arg(step.pose).arg(step.in).arg(step.hold).arg(step.snap ? " step" : ""));
    return rows;
}
}  // namespace

void PuppetWorkspaceTests::testStepPartsEaseTheirOwnWay() {
    Doll doll;
    addDollPoses(doll);
    Puppet& puppet = doll.project.session.activeAtlas().puppets.first();
    puppet.sequences = {{QStringLiteral("bob"), 8, AnimationLoopMode::Loop,
                         {{PUPPET_REST_POSE, 4, 0, false}, {QStringLiteral("nod"), 2, 0, false}}}};
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.select({0, 1, -1});  // the head
    auto* group = workspace.findChild<QWidget*>(QStringLiteral("partAnimationGroup"));
    auto* ease = workspace.findChild<QComboBox*>(QStringLiteral("partStepEase"));
    auto* note = workspace.findChild<QLabel*>(QStringLiteral("partStepEaseNote"));
    QVERIFY(group && ease && note);
    // Only with the Animations tab, and only for a selected step.
    QVERIFY(!group->isVisibleTo(&workspace));
    workspace.bottomTabs()->setCurrentIndex(1);
    workspace.selectSequence(0);
    QVERIFY(group->isVisibleTo(&workspace));
    QVERIFY(!ease->isEnabled());

    workspace.selectStep(0);
    QVERIFY(ease->isEnabled());
    QCOMPARE(ease->currentText(), QStringLiteral("As step (Linear)"));
    QCOMPARE(note->text(), QStringLiteral("In step 1 of \"bob\"."));
    ease->setCurrentIndex(ease->findText(QStringLiteral("Step")));
    QCOMPARE(doll.puppet().sequences[0].steps[0].partSnaps, (QMap<QString, bool>{{QStringLiteral("head"), true}}));
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Part Ease"));
    QCOMPARE(ease->currentText(), QStringLiteral("Step"));

    // The whole step snapping: the head is no exception any more.
    workspace.findChild<QComboBox*>(QStringLiteral("puppetStepEase"))->setCurrentIndex(1);
    QVERIFY(doll.puppet().sequences[0].steps[0].partSnaps.isEmpty());
    QCOMPARE(ease->currentText(), QStringLiteral("As step (Step)"));
    // Picking what the step does already keeps nothing.
    ease->setCurrentIndex(ease->findText(QStringLiteral("Step")));
    QVERIFY(doll.puppet().sequences[0].steps[0].partSnaps.isEmpty());
    ease->setCurrentIndex(ease->findText(QStringLiteral("Linear")));
    QCOMPARE(doll.puppet().sequences[0].steps[0].partSnaps, (QMap<QString, bool>{{QStringLiteral("head"), false}}));
    ease->setCurrentIndex(0);
    QVERIFY(doll.puppet().sequences[0].steps[0].partSnaps.isEmpty());
    doll.project.undoStack.undo();
    QCOMPARE(doll.puppet().sequences[0].steps[0].partSnaps, (QMap<QString, bool>{{QStringLiteral("head"), false}}));

    // A step whose pose doesn't move the part: nothing to ease.
    workspace.select({0, 0, -1});  // the body; "nod" turns only the head
    workspace.selectStep(1);
    QVERIFY(!ease->isEnabled());
    QCOMPARE(note->text(), QStringLiteral("Step 2 of \"bob\" doesn't move this part: its pose doesn't set it."));
}

void PuppetWorkspaceTests::testBuildingAnAnimationFromPoses() {
    Doll doll;
    addDollPoses(doll);
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.bottomTabs()->setCurrentIndex(1);
    auto* list = workspace.findChild<QListWidget*>(QStringLiteral("puppetAnimations"));
    PuppetSequenceTrack* track = workspace.sequenceTrack();

    workspace.findChild<QPushButton*>(QStringLiteral("puppetAddAnimation"))->click();
    QCOMPARE(doll.puppet().sequences.size(), 1);
    QCOMPARE(doll.puppet().sequences[0].name, QStringLiteral("animation"));
    QCOMPARE(list->count(), 1);
    QCOMPARE(workspace.selectedSequence(), 0);
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("New Animation"));

    // Poses are added from the steps' context menu: on empty space at the end, or before
    // or after a step.
    std::unique_ptr<QMenu> emptyMenu(track->createContextMenu(-1));
    QCOMPARE(menuItems(emptyMenu.get()), (QStringList{"New Frame", "Add Pose"}));
    addPoseFromMenu(emptyMenu.get(), QStringLiteral("Add Pose"), QStringLiteral("nod"));
    QCOMPARE(stepRows(doll), QStringList{"nod 4/4"});
    QCOMPARE(workspace.selectedStep(), 0);
    std::unique_ptr<QMenu> menu(track->createContextMenu(0));
    QCOMPARE(menuItems(menu.get()), (QStringList{"New Frame Before", "New Frame After", "Add Pose Before",
                                                 "Add Pose After", "-", "Save as Pose...", "-", "[Move to the left]",
                                                 "[Move to the right]", "Duplicate", "-", "Delete"}));
    addPoseFromMenu(menu.get(), QStringLiteral("Add Pose After"), QStringLiteral("smile"));
    QCOMPARE(stepRows(doll), (QStringList{"nod 4/4", "smile 4/4"}));
    std::unique_ptr<QMenu> secondMenu(track->createContextMenu(1));
    addPoseFromMenu(secondMenu.get(), QStringLiteral("Add Pose Before"), QStringLiteral("smile"));
    QCOMPARE(stepRows(doll), (QStringList{"nod 4/4", "smile 4/4", "smile 4/4"}));
    QCOMPARE(workspace.selectedStep(), 1);

    // The selected step's fields; a step lasts a frame at least.
    workspace.findChild<QSpinBox*>(QStringLiteral("puppetStepIn"))->setValue(2);
    workspace.findChild<QComboBox*>(QStringLiteral("puppetStepEase"))->setCurrentIndex(1);
    QCOMPARE(stepRows(doll)[1], QStringLiteral("smile 2/4 step"));
    workspace.findChild<QSpinBox*>(QStringLiteral("puppetStepHold"))->setValue(0);
    workspace.findChild<QSpinBox*>(QStringLiteral("puppetStepIn"))->setValue(0);
    QCOMPARE(stepRows(doll)[1], QStringLiteral("smile 0/1 step"));
    // Rest is a pose a step can go back to.
    auto* stepPose = workspace.findChild<QComboBox*>(QStringLiteral("puppetStepPose"));
    QCOMPARE(stepPose->itemText(0), PUPPET_REST_POSE);
    stepPose->setCurrentIndex(0);
    QCOMPARE(stepRows(doll)[1], QStringLiteral("rest 0/1 step"));
    stepPose->setCurrentIndex(1);
    QCOMPARE(stepRows(doll)[1], QStringLiteral("nod 0/1 step"));

    // Move, duplicate and delete, as frames.
    emit track->moveRequested(1, 0);
    QCOMPARE(stepRows(doll), (QStringList{"nod 0/1 step", "nod 4/4", "smile 4/4"}));
    QCOMPARE(workspace.selectedStep(), 0);
    emit track->duplicateRequested(2);
    QCOMPARE(stepRows(doll).size(), 4);
    QCOMPARE(workspace.selectedStep(), 3);
    track->setFocus();
    QTest::keyClick(track, Qt::Key_Delete);
    QCOMPARE(stepRows(doll).size(), 3);

    // Steps beyond the view are reached by scrolling.
    for (int i = 0; i < 30; ++i) emit track->duplicateRequested(0);
    auto* trackScroll = qobject_cast<QScrollArea*>(track->parentWidget()->parentWidget());
    QVERIFY(trackScroll);
    QCOMPARE(track->width(), track->sizeHint().width());
    QVERIFY(trackScroll->horizontalScrollBar()->maximum() > 0);
    trackScroll->horizontalScrollBar()->setValue(trackScroll->horizontalScrollBar()->maximum());
    const QPoint lastStep = track->mapTo(trackScroll->viewport(), QPoint(track->width() - 10, 40));
    QVERIFY(trackScroll->viewport()->rect().contains(lastStep));

    // The animation's own settings, and its name.
    workspace.findChild<QSpinBox*>(QStringLiteral("puppetAnimationFps"))->setValue(12);
    workspace.findChild<QComboBox*>(QStringLiteral("puppetAnimationLoop"))->setCurrentIndex(2);
    QCOMPARE(doll.puppet().sequences[0].fps, 12);
    QCOMPARE(doll.puppet().sequences[0].loopMode, AnimationLoopMode::PingPong);
    list->item(0)->setText(QStringLiteral("walk"));
    QCoreApplication::processEvents();
    QCOMPARE(doll.puppet().sequences[0].name, QStringLiteral("walk"));
    workspace.findChild<QPushButton*>(QStringLiteral("puppetAddAnimation"))->click();
    list->item(1)->setText(QStringLiteral("walk"));  // taken: put back
    QCoreApplication::processEvents();
    QCOMPARE(doll.puppet().sequences[1].name, QStringLiteral("animation"));

    // Each edit was one step: undoing them all leaves no animation.
    while (doll.project.undoStack.canUndo()) doll.project.undoStack.undo();
    QVERIFY(doll.puppet().sequences.isEmpty());
    QCOMPARE(list->count(), 0);
}

namespace {
/// Triggers @p menu's action @p text.
void triggerMenuItem(QMenu* menu, const QString& text) {
    for (QAction* action : menu->actions()) {
        if (action->text() == text) {
            action->trigger();
            return;
        }
    }
    QFAIL(qPrintable(QStringLiteral("No %1").arg(text)));
}

/// The poses offered by @p menu's submenu @p title.
QStringList submenuItems(QMenu* menu, const QString& title) {
    for (QAction* action : menu->actions()) {
        if (action->text() == title && action->menu()) return menuItems(action->menu());
    }
    return {};
}

/// The angle pose @p name sets for part @p part, if any.
std::optional<int> poseAngle(const Puppet& puppet, const QString& name, const QString& part) {
    for (const PuppetPose& pose : puppet.poses) {
        if (pose.name != name) continue;
        for (const PuppetPoseSetting& setting : pose.settings) {
            if (setting.part == part) return setting.angle;
        }
    }
    return std::nullopt;
}
}  // namespace

void PuppetWorkspaceTests::testAnimationFramesAreEditedApartFromOtherAnimations() {
    Doll doll;
    addDollPoses(doll);
    Puppet& puppet = doll.project.session.activeAtlas().puppets.first();
    puppet.sequences = {{QStringLiteral("walk"), 8, AnimationLoopMode::Loop, {{QStringLiteral("nod"), 2, 2, false}}},
                        {QStringLiteral("talk"), 8, AnimationLoopMode::Loop, {{QStringLiteral("nod"), 2, 2, false}}}};
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.bottomTabs()->setCurrentIndex(1);
    PuppetSequenceTrack* track = workspace.sequenceTrack();
    workspace.select({0, 1, -1});  // the head

    // A new frame after "nod" is the animation's own pose, starting as a copy of it.
    std::unique_ptr<QMenu> menu(track->createContextMenu(0));
    triggerMenuItem(menu.get(), QStringLiteral("New Frame After"));
    QCOMPARE(stepRows(doll), (QStringList{"nod 2/2", "walk frame 1 2/2"}));
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("New Frame"));
    QCOMPARE(doll.puppet().poses.last().owner, QStringLiteral("walk"));
    QCOMPARE(poseAngle(doll.puppet(), QStringLiteral("walk frame 1"), QStringLiteral("head")), std::optional<int>(20));
    // The Poses tab lists only shared poses; steps of another animation can't play it.
    QCOMPARE(workspace.poseStrip()->count(), 3);
    std::unique_ptr<QMenu> walkMenu(track->createContextMenu(-1));
    QCOMPARE(submenuItems(walkMenu.get(), QStringLiteral("Add Pose")), (QStringList{"rest", "nod", "smile", "walk frame 1"}));

    // Stopped on the new frame, the canvas edits it, and only it.
    QCOMPARE(workspace.selectedStep(), 1);
    QVERIFY(workspace.editingStep());
    workspace.setPartAngle(1, 45);
    QCOMPARE(poseAngle(doll.puppet(), QStringLiteral("walk frame 1"), QStringLiteral("head")), std::optional<int>(45));
    QCOMPARE(poseAngle(doll.puppet(), QStringLiteral("nod"), QStringLiteral("head")), std::optional<int>(20));
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(45));

    // Onion skin: the step before shows behind, two more images (body and head).
    const int images = drawnParts(workspace.canvas()).size();
    workspace.findChild<QCheckBox*>(QStringLiteral("puppetOnionSkin"))->setChecked(true);
    QCOMPARE(drawnParts(workspace.canvas()).size(), images + 2);
    QVERIFY(drawnParts(workspace.canvas()).contains(turnedHead(20)));  // the ghost of "nod"
    workspace.findChild<QCheckBox*>(QStringLiteral("puppetOnionSkin"))->setChecked(false);

    // A step playing a shared pose gets its own copy when edited: "talk" still nods by 20°.
    workspace.selectStep(0);
    workspace.setPartAngle(1, 30);
    QCOMPARE(stepRows(doll), (QStringList{"walk frame 2 2/2", "walk frame 1 2/2"}));
    QCOMPARE(poseAngle(doll.puppet(), QStringLiteral("walk frame 2"), QStringLiteral("head")), std::optional<int>(30));
    QCOMPARE(poseAngle(doll.puppet(), QStringLiteral("nod"), QStringLiteral("head")), std::optional<int>(20));
    QCOMPARE(stepRows(doll, 1), QStringList{"nod 2/2"});
    QVERIFY(workspace.findChild<QLabel*>(QStringLiteral("puppetStepNote"))->text().contains(QStringLiteral("walk frame 2")));
    // More turns of that step go on the copy; undoing them puts "nod" back.
    workspace.setPartAngle(1, 35);
    QCOMPARE(doll.puppet().poses.size(), 5);
    doll.project.undoStack.undo();
    doll.project.undoStack.undo();
    QCOMPARE(stepRows(doll), (QStringList{"nod 2/2", "walk frame 1 2/2"}));
    QCOMPARE(doll.puppet().poses.size(), 4);

    // Mid-way to a pose, the canvas only plays.
    emit track->playheadMoved(0);
    QVERIFY(!workspace.editingStep());
    workspace.setPartAngle(1, 60);
    QCOMPARE(poseAngle(doll.puppet(), QStringLiteral("nod"), QStringLiteral("head")), std::optional<int>(20));

    // Saved as a pose, a frame is copied to the Poses tab.
    QVERIFY(!workspace.savePoseFromStep(1, QStringLiteral("nod")).isEmpty());  // taken
    QCOMPARE(workspace.savePoseFromStep(1, QStringLiteral("look up")), QString());
    QCOMPARE(PuppetEditing::poseNames(doll.puppet()), (QStringList{"rest", "nod", "smile", "look up", "walk frame 1"}));
    QCOMPARE(workspace.poseStrip()->count(), 4);

    // Renaming the animation renames its frames; removing the frame's step removes it.
    workspace.findChild<QListWidget*>(QStringLiteral("puppetAnimations"))->item(0)->setText(QStringLiteral("stroll"));
    QCoreApplication::processEvents();
    QCOMPARE(stepRows(doll), (QStringList{"nod 2/2", "stroll frame 1 2/2"}));
    workspace.removeStep(1);
    QCOMPARE(PuppetEditing::poseNames(doll.puppet()), (QStringList{"rest", "nod", "smile", "look up"}));
}

void PuppetWorkspaceTests::testPlayingAnAnimation() {
    Doll doll;
    addDollPoses(doll);
    // "lean" turns the body; "nodding" reaches "nod" (head 20°) in 2 frames and holds 2.
    PuppetPoseSetting lean;
    lean.part = QStringLiteral("body");
    lean.angle = 10;
    Puppet& puppet = doll.project.session.activeAtlas().puppets.first();
    puppet.poses.append({QStringLiteral("lean"), {lean}});
    puppet.sequences = {{QStringLiteral("nodding"), 8, AnimationLoopMode::Loop, {{QStringLiteral("nod"), 2, 2, false}}},
                        {QStringLiteral("leaning"), 8, AnimationLoopMode::Loop, {{QStringLiteral("lean"), 1, 0, false}}}};
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.bottomTabs()->setCurrentIndex(1);
    QCOMPARE(workspace.selectedSequence(), 0);

    // Frame 1 of 4: half way to the pose. Selecting the step shows the pose arrived.
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(10));
    workspace.selectStep(0);
    QCOMPARE(workspace.playFrame(), 1);
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(20));
    QCOMPARE(workspace.findChild<QLabel*>(QStringLiteral("puppetAnimationInfo"))->text(),
             QStringLiteral("Frame 2 of 4 · 0.5 s"));
    emit workspace.sequenceTrack()->playheadMoved(0);
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(10));

    // The canvas only plays: dragging the body changes nothing.
    dragOnCanvas(workspace.canvas(), QPointF(0, -20), QPointF(20, -20));
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(0, 0)));
    QCOMPARE(doll.project.undoStack.count(), 0);

    // Playing advances the playhead on its own.
    auto* play = workspace.findChild<QToolButton*>(QStringLiteral("puppetPlay"));
    play->click();
    QVERIFY(play->isChecked());
    QTRY_VERIFY_WITH_TIMEOUT(workspace.playFrame() >= 2, 2000);
    QCOMPARE(workspace.playInterval(), 125);
    // Changed while playing, it plays on as it is now: at the new speed...
    workspace.findChild<QSpinBox*>(QStringLiteral("puppetAnimationFps"))->setValue(2);
    QCOMPARE(workspace.playInterval(), 500);
    // ...and with the new steps; with none left, it stops.
    workspace.selectStep(0);
    workspace.findChild<QSpinBox*>(QStringLiteral("puppetStepHold"))->setValue(6);
    QCOMPARE(workspace.playInterval(), 500);
    QVERIFY(play->isChecked());
    emit workspace.sequenceTrack()->removeRequested(0);
    QCOMPARE(workspace.playInterval(), -1);
    QVERIFY(!play->isChecked());
    doll.project.undoStack.undo();
    doll.project.undoStack.undo();
    doll.project.undoStack.undo();
    QCOMPARE(doll.puppet().sequences[0].fps, 8);
    play->click();
    QCOMPARE(workspace.playInterval(), 125);
    play->click();
    QVERIFY(!play->isChecked());

    // Ticked, another animation plays along: the body leans while the head nods.
    auto* list = workspace.findChild<QListWidget*>(QStringLiteral("puppetAnimations"));
    list->item(1)->setCheckState(Qt::Checked);
    QCoreApplication::processEvents();
    QTransform leaned;
    leaned.rotate(10);
    QCOMPARE(drawnParts(workspace.canvas())[0], leaned.mapRect(QRectF(-10, -40, 20, 40)));

    // Back to the poses, the canvas shows Rest again and can be edited.
    workspace.bottomTabs()->setCurrentIndex(0);
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(0));
    QCOMPARE(drawnParts(workspace.canvas())[0], QRectF(-10, -40, 20, 40));
}

void PuppetWorkspaceTests::testAnimationVariantsPlayAlong() {
    Doll doll;
    // The head shows "blink": the head, then a wider head, at 4 fps, looping.
    const SpritePtr wide = doll.project.addSprite(QStringLiteral("head-wide"), 24, 16, QPoint(12, 16));
    AnimationTimeline blink;
    blink.name = QStringLiteral("blink");
    blink.fps = 4;
    blink.frames = {doll.head->path, wide->path};
    doll.project.session.activeAtlas().timelines = {blink};
    Puppet& puppet = doll.project.session.activeAtlas().puppets.first();
    PuppetVariant animation;
    animation.kind = PuppetVariant::Kind::Animation;
    animation.source = QStringLiteral("blink");
    puppet.parts[1].variants = {animation};
    // An 8 fps animation 8 frames long: one second, in which "blink" plays 4 frames.
    puppet.sequences = {{QStringLiteral("idle"), 8, AnimationLoopMode::Loop, {{PUPPET_REST_POSE, 1, 7, false}}}};
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    const auto headWidth = [&]() { return drawnParts(workspace.canvas())[1].width(); };

    // Posing, the animation shows its first frame.
    QCOMPARE(headWidth(), 16.0);
    workspace.bottomTabs()->setCurrentIndex(1);
    workspace.selectSequence(0);
    // At 8 fps, frames 0-1 are blink's first frame, 2-3 its second, 4-5 its first again.
    const QVector<QPair<int, double>> expected{{0, 16}, {1, 16}, {2, 24}, {3, 24}, {4, 16}, {7, 24}};
    for (const auto& [frame, width] : expected) {
        emit workspace.sequenceTrack()->playheadMoved(frame);
        QVERIFY2(headWidth() == width, qPrintable(QStringLiteral("frame %1: %2").arg(frame).arg(headWidth())));
    }
    // The wide frame's own pivot places it: centred on the neck.
    emit workspace.sequenceTrack()->playheadMoved(2);
    QCOMPARE(drawnParts(workspace.canvas())[1], QRectF(-12, -54, 24, 16));

    // The pose thumbnails keep showing the first frame.
    const QPixmap thumbnail = workspace.poseStrip()->item(0)->icon().pixmap(PuppetPoseStrip::THUMBNAIL_SIZE).copy();
    workspace.refresh();  // on the wide frame
    QCOMPARE(headWidth(), 24.0);
    QCOMPARE(workspace.poseStrip()->item(0)->icon().pixmap(PuppetPoseStrip::THUMBNAIL_SIZE).toImage(), thumbnail.toImage());

    // Played once, it holds its last frame.
    doll.project.session.activeAtlas().timelines[0].loopMode = AnimationLoopMode::Once;
    emit workspace.sequenceTrack()->playheadMoved(7);
    QCOMPARE(headWidth(), 24.0);

    // Back to the poses: the first frame again.
    workspace.bottomTabs()->setCurrentIndex(0);
    QCOMPARE(headWidth(), 16.0);
}

void PuppetWorkspaceTests::testPosesTabIsAsTallAsItsContent() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    QCoreApplication::processEvents();
    auto* addPose = workspace.findChild<QPushButton*>(QStringLiteral("puppetNewPose"));
    PuppetPoseStrip* strip = workspace.poseStrip();
    // The buttons sit just above the strip: the taller Animations tab lends the Poses tab no room.
    const int gap = strip->mapTo(&workspace, QPoint()).y() - addPose->mapTo(&workspace, QPoint(0, addPose->height())).y();
    QVERIFY2(gap <= 12, qPrintable(QStringLiteral("%1 px between the buttons and the strip").arg(gap)));
    // The canvas gets the room the shorter Poses page leaves.
    const int posesCanvas = workspace.canvas()->height();
    workspace.bottomTabs()->setCurrentIndex(1);
    QCoreApplication::processEvents();
    QVERIFY(workspace.bottomPage(1)->isVisible() && !workspace.bottomPage(0)->isVisible());
    QVERIFY(workspace.canvas()->height() < posesCanvas);
    workspace.bottomTabs()->setCurrentIndex(0);
    QCoreApplication::processEvents();
    QCOMPARE(workspace.canvas()->height(), posesCanvas);
}

void PuppetWorkspaceTests::testAngleFieldTurnsThePart() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.select({0, 1, -1});
    startPose(workspace);
    auto* angle = workspace.findChild<QSpinBox*>(QStringLiteral("partAngle"));
    QVERIFY(angle && angle->isEnabled());
    QCOMPARE(angle->value(), 0);

    angle->setValue(45);
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(45));
    QCOMPARE(doll.project.undoStack.count(), 2);

    // Turning on the canvas shows in the field, and the field keeps it when coming back.
    dragOnCanvas(workspace.canvas(), turnedHead(45).center(), QPointF(-20, -38));
    QCOMPARE(angle->value(), -90);
    workspace.select({0, 0, -1});
    QCOMPARE(angle->value(), 0);
    workspace.select({0, 1, -1});
    QCOMPARE(angle->value(), -90);

    // Limits bound the field; a free turn wraps round.
    QCOMPARE(angle->minimum(), -179);
    QVERIFY(angle->wrapping());
    workspace.findChild<QCheckBox*>(QStringLiteral("partLimitAngle"))->setChecked(true);
    QCOMPARE(angle->minimum(), -45);
    QCOMPARE(angle->maximum(), 45);
    QCOMPARE(angle->value(), -45);
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(-45));

    // Rest shows its own angle, and the field turns the part in it.
    workspace.selectPose(0);
    QCOMPARE(angle->value(), 0);
    QVERIFY(angle->isEnabled());
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(0));
    angle->setValue(10);
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(10));
    QCOMPARE(doll.puppet().poses[0].settings[1].angle, std::optional<int>(10));
}

void PuppetWorkspaceTests::testRenamingAPartUpdatesItsAttachments() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    workspace.refreshSprites();
    workspace.select({0, 0, -1});

    workspace.tree()->currentItem()->setText(0, QStringLiteral("torso"));
    QCoreApplication::processEvents();
    QCOMPARE(doll.puppet().parts[0].name, QStringLiteral("torso"));
    QCOMPARE(doll.puppet().attachments[0].parent, QStringLiteral("torso"));
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Rename Part"));
    QCOMPARE(workspace.selection(), (PuppetSelection{0, 0, -1}));

    doll.project.undoStack.undo();
    QCOMPARE(doll.puppet().parts[0].name, QStringLiteral("body"));
    QCOMPARE(doll.puppet().attachments[0].parent, QStringLiteral("body"));
}

void PuppetWorkspaceTests::testPanelRefusesATakenName() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    workspace.refreshSprites();
    workspace.select({0, 1, -1});
    auto* nameEdit = workspace.findChild<QLineEdit*>(QStringLiteral("partNameEdit"));
    QVERIFY(nameEdit);
    QCOMPARE(nameEdit->text(), QStringLiteral("head"));

    nameEdit->setText(QStringLiteral("body"));
    emit nameEdit->editingFinished();
    QCOMPARE(nameEdit->text(), QStringLiteral("head"));
    QCOMPARE(doll.puppet().parts[1].name, QStringLiteral("head"));
    QCOMPARE(doll.project.undoStack.count(), 0);

    nameEdit->setText(QStringLiteral("face"));
    emit nameEdit->editingFinished();
    QCOMPARE(doll.puppet().parts[1].name, QStringLiteral("face"));
    QCOMPARE(doll.puppet().attachments[0].child, QStringLiteral("face"));
}

void PuppetWorkspaceTests::testPositionEditsAreOneUndoStep() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    workspace.refreshSprites();
    workspace.select({0, 0, -1});
    auto* x = workspace.findChild<QSpinBox*>(QStringLiteral("partPositionX"));
    auto* y = workspace.findChild<QSpinBox*>(QStringLiteral("partPositionY"));
    QVERIFY(x && y);
    QVERIFY(x->isVisibleTo(&workspace));

    x->setValue(5);
    x->setValue(6);
    y->setValue(3);
    QCOMPARE(doll.project.undoStack.count(), 1);
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(6, 3)));
    // The head follows the body it hangs from.
    QCOMPARE(drawnParts(workspace.canvas()),
             (QVector<QRectF>{QRectF(-4, -37, 20, 40), QRectF(-2, -51, 16, 16)}));

    doll.project.undoStack.undo();
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(0, 0)));
    QCOMPARE(x->value(), 0);

    // An attached part is placed by its attachment, so it has no position to edit.
    workspace.select({0, 1, -1});
    QVERIFY(!x->isVisibleTo(&workspace));
}

void PuppetWorkspaceTests::testReorderingPartsAndVariants() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    workspace.refreshSprites();

    workspace.select({0, 1, -1});
    workspace.moveSelected(0);
    QCOMPARE(doll.puppet().parts[0].name, QStringLiteral("head"));
    QCOMPARE(workspace.selection(), (PuppetSelection{0, 0, -1}));
    // Draw order doesn't change where the head hangs, only that it's behind the body.
    QCOMPARE(drawnParts(workspace.canvas()),
             (QVector<QRectF>{QRectF(-8, -54, 16, 16), QRectF(-10, -40, 20, 40)}));

    // Variants are listed by name: they aren't moved.
    workspace.select({0, 0, 1});
    workspace.moveSelected(0);
    QCOMPARE(puppetVariantName(doll.puppet().parts[0].variants[0]), QStringLiteral("head"));
    QCOMPARE(doll.project.undoStack.count(), 1);
    QVERIFY(!(workspace.tree()->currentItem()->flags() & Qt::ItemIsDragEnabled));
}

namespace {
/// Adds a third part, "tail", in front of the doll's body and head.
void addTail(Doll& doll) {
    Puppet& puppet = doll.project.session.activeAtlas().puppets.first();
    puppet.parts.append(PuppetPart{QStringLiteral("tail"), {}, QPoint(0, 0)});
    PuppetEditing::keepRestPose(puppet);
}

QStringList partNames(const Puppet& puppet) {
    QStringList names;
    for (const PuppetPart& part : puppet.parts) names.append(part.name);
    return names;
}

/// Sends a drag of @p part (of the first puppet) to @p tree's viewport, entering at @p enterPos
/// and moving to @p pos; drops it there when @p drop.
void dragPart(QTreeWidget* tree, int part, const QPoint& enterPos, const QPoint& pos, bool drop) {
    QMimeData mime;
    mime.setData(PuppetTreeWidget::PART_MIME_TYPE, "0 " + QByteArray::number(part));
    QDragEnterEvent enter(enterPos, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(tree->viewport(), &enter);
    QDragMoveEvent move(pos, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(tree->viewport(), &move);
    if (!drop) return;
    QDropEvent dropped(QPointF(pos), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(tree->viewport(), &dropped);
}

/// The first puppet's child rows: part names, "[gap]" for the drop gap, hidden rows left out.
QStringList puppetChildRows(QTreeWidget* tree) {
    QStringList rows;
    const QTreeWidgetItem* puppetItem = tree->topLevelItem(0);
    for (int i = 0; i < puppetItem->childCount(); ++i) {
        const QTreeWidgetItem* child = puppetItem->child(i);
        if (child->isHidden()) continue;
        rows.append(PuppetTreeWidget::isDropGap(child) ? QStringLiteral("[gap]") : child->text(0));
    }
    return rows;
}
}  // namespace

void PuppetWorkspaceTests::testDrawOrderMenusOnTheTreeAndCanvas() {
    Doll doll;
    addTail(doll);
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);

    workspace.select({0, 1, -1});
    std::unique_ptr<QMenu> menu(workspace.createContextMenu());
    const QStringList items = menuItems(menu.get());
    QVERIFY(items.contains(QStringLiteral("Bring to Front")));
    QVERIFY(items.contains(QStringLiteral("Send to Back")));
    for (QAction* action : menu->actions()) {
        if (action->text() == QStringLiteral("Bring to Front")) action->trigger();
    }
    QCOMPARE(partNames(doll.puppet()), (QStringList{"body", "tail", "head"}));
    QCOMPARE(workspace.selection(), (PuppetSelection{0, 2, -1}));

    // At the front already: only backwards.
    std::unique_ptr<QMenu> frontMenu(workspace.createContextMenu());
    QVERIFY(menuItems(frontMenu.get()).contains(QStringLiteral("[Bring to Front]")));
    QVERIFY(menuItems(frontMenu.get()).contains(QStringLiteral("[Bring Forward]")));
    for (QAction* action : frontMenu->actions()) {
        if (action->text() == QStringLiteral("Send to Back")) action->trigger();
    }
    QCOMPARE(partNames(doll.puppet()), (QStringList{"head", "body", "tail"}));
    QCOMPARE(doll.project.undoStack.count(), 2);

    // Right-clicking a part on the canvas selects it and opens the same menu.
    PuppetCanvas* canvas = workspace.canvas();
    QSignalSpy spy(canvas, &PuppetCanvas::partContextMenuRequested);
    const QPoint onHead = canvas->mapFromScene(QPointF(0, -46));
    QContextMenuEvent rightClick(QContextMenuEvent::Mouse, onHead, canvas->viewport()->mapToGlobal(onHead));
    QCoreApplication::sendEvent(canvas->viewport(), &rightClick);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(workspace.selection(), (PuppetSelection{0, 0, -1}));
    auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    QVERIFY(popup);
    QVERIFY(menuItems(popup).contains(QStringLiteral("[Send to Back]")));
    QVERIFY(menuItems(popup).contains(QStringLiteral("Bring to Front")));
    popup->close();
}

void PuppetWorkspaceTests::testDraggingAPartOpensAGapWhereItLands() {
    Doll doll;
    addTail(doll);
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    QTreeWidget* tree = workspace.tree();
    const auto rowOf = [tree](int part) { return tree->visualItemRect(tree->topLevelItem(0)->child(part)); };
    const QPoint onBody = rowOf(0).center();

    // While dragged, the part's row is lifted out and a gap shows where it would land:
    // past the last row, anywhere below it, is last.
    const QPoint below(onBody.x(), tree->viewport()->height() - 2);
    dragPart(tree, 0, onBody, below, false);
    QCOMPARE(puppetChildRows(tree), (QStringList{"head", "tail", "[gap]"}));
    // Leaving the tree puts it back.
    QDragLeaveEvent leave;
    QCoreApplication::sendEvent(tree->viewport(), &leave);
    QCOMPARE(puppetChildRows(tree), (QStringList{"body", "head", "tail"}));

    dragPart(tree, 0, onBody, below, true);
    QCOMPARE(partNames(doll.puppet()), (QStringList{"head", "tail", "body"}));
    QCOMPARE(puppetChildRows(tree), (QStringList{"head", "tail", "body"}));

    // Over a part's variants (head has two): after that part.
    const QTreeWidgetItem* headVariant = tree->topLevelItem(0)->child(0)->child(1);
    const QPoint onVariant = tree->visualItemRect(headVariant).center();
    dragPart(tree, 2, rowOf(2).center(), onVariant, true);
    QCOMPARE(partNames(doll.puppet()), (QStringList{"head", "body", "tail"}));

    // On the puppet's row: first.
    const QPoint onPuppet = tree->visualItemRect(tree->topLevelItem(0)).center();
    dragPart(tree, 2, rowOf(2).center(), onPuppet, true);
    QCOMPARE(partNames(doll.puppet()), (QStringList{"tail", "head", "body"}));

    // Dropped where it was: nothing changes.
    const int edits = doll.project.undoStack.count();
    dragPart(tree, 1, rowOf(1).center(), rowOf(1).center(), true);
    QCOMPARE(partNames(doll.puppet()), (QStringList{"tail", "head", "body"}));
    QCOMPARE(doll.project.undoStack.count(), edits);
    QCOMPARE(puppetChildRows(tree), (QStringList{"tail", "head", "body"}));
}

void PuppetWorkspaceTests::testRestPoseVariantIsTheRestVariant() {
    Doll doll;
    addDollPoses(doll);
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.select({0, 1, -1});
    auto* shows = workspace.findChild<QComboBox*>(QStringLiteral("partShownVariant"));
    QVERIFY(shows);

    // Picking a variant on another pose sets it there only.
    workspace.selectPose(1);
    shows->setCurrentIndex(1);
    QCOMPARE(doll.puppet().poses[1].settings[0].variant, QStringLiteral("head-smiling"));
    QCOMPARE(puppetVariantName(doll.puppet().parts[1].variants[0]), QStringLiteral("head"));

    // Picked on Rest, it becomes the rest variant: first.
    workspace.selectPose(0);
    QCOMPARE(shows->currentIndex(), 0);  // the rest variant, starred as Rest shows it
    shows->setCurrentIndex(1);
    QCOMPARE(doll.puppet().poses[0].settings[1].variant, QStringLiteral("head-smiling"));
    QCOMPARE(puppetVariantName(doll.puppet().parts[1].variants[0]), QStringLiteral("head-smiling"));
    QCOMPARE(doll.project.undoStack.count(), 2);
    doll.project.undoStack.undo();
    QCOMPARE(puppetVariantName(doll.puppet().parts[1].variants[0]), QStringLiteral("head"));
    QCOMPARE(doll.puppet().poses[0].settings[1].variant, QStringLiteral("head"));
}

void PuppetWorkspaceTests::testRemovingAPartSelectsItsNeighbour() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    workspace.refreshSprites();
    workspace.select({0, 1, -1});

    workspace.removeSelected();
    QCOMPARE(doll.puppet().parts.size(), 1);
    QVERIFY(doll.puppet().attachments.isEmpty());
    QCOMPARE(workspace.selection(), (PuppetSelection{0, 0, -1}));

    doll.project.undoStack.undo();
    QCOMPARE(doll.puppet().parts.size(), 2);
    QCOMPARE(doll.puppet().attachments.size(), 1);
    QCOMPARE(workspace.selection(), (PuppetSelection{0, 1, -1}));
}

/// Whether a tree row shows the star (unstarred rows have a blank icon, to keep names aligned).
bool starred(QTreeWidgetItem* item) {
    const QImage image = item->icon(0).pixmap(16, 16).toImage();
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(image.pixel(x, y)) > 0) return true;
    return false;
}

void PuppetWorkspaceTests::testShowingAVariantInAPoseFromTheTree() {
    Doll doll;
    addDollPoses(doll);
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.selectPose(1);  // "nod" leaves the head's variant alone
    const auto variantItem = [&workspace](int part, int variant) {
        for (QTreeWidgetItemIterator it(workspace.tree()); *it; ++it) {
            if (PuppetTreeWidget::selectionOf(*it) == PuppetSelection{0, part, variant}) return *it;
        }
        return static_cast<QTreeWidgetItem*>(nullptr);
    };
    // The tree marks the variant the pose shows.
    QCOMPARE(variantItem(1, 0)->text(1), QStringLiteral("sprite · shown"));
    QCOMPARE(variantItem(1, 1)->text(1), QStringLiteral("sprite"));

    // Right-click a variant: Show in Pose, off for the one already shown.
    workspace.select({0, 1, 0});
    std::unique_ptr<QMenu> shownMenu(workspace.createContextMenu());
    QVERIFY(menuItems(shownMenu.get()).contains(QStringLiteral("[Show in Pose \"nod\"]")));
    workspace.select({0, 1, 1});
    std::unique_ptr<QMenu> menu(workspace.createContextMenu());
    QAction* show = nullptr;
    for (QAction* action : menu->actions()) {
        if (action->text() == QStringLiteral("Show in Pose \"nod\"")) show = action;
    }
    QVERIFY(show && show->isEnabled());
    show->trigger();
    QCOMPARE(doll.puppet().poses[1].settings[0].variant, QStringLiteral("head-smiling"));
    QCOMPARE(doll.puppet().poses[1].settings[0].angle, std::optional<int>(20));
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Show Variant"));
    QCOMPARE(variantItem(1, 1)->text(1), QStringLiteral("sprite · shown"));
    // The star is on the variant shown, here and in the panel's Shows.
    QVERIFY(!starred(variantItem(1, 0)));
    QVERIFY(starred(variantItem(1, 1)));
    workspace.select({0, 1, -1});
    auto* showsList = workspace.findChild<QComboBox*>(QStringLiteral("partShownVariant"));
    QCOMPARE(showsList->itemText(0), QStringLiteral("head"));
    QCOMPARE(showsList->itemText(1), QStringLiteral("★ head-smiling"));
    // An animation frame playing "smile": its variant is starred while it is shown.
    doll.project.undoStack.undo();
    doll.project.session.activeAtlas().puppets.first().sequences = {
        {QStringLiteral("grin"), 8, AnimationLoopMode::Loop, {{QStringLiteral("smile"), 1, 1, false}}}};
    workspace.refresh();
    workspace.bottomTabs()->setCurrentIndex(1);
    workspace.selectStep(0);
    QVERIFY(!starred(variantItem(1, 0)));
    QVERIFY(starred(variantItem(1, 1)));
    workspace.bottomTabs()->setCurrentIndex(0);
    QVERIFY(starred(variantItem(1, 0)));
    QVERIFY(!starred(variantItem(1, 1)));
    doll.project.session.activeAtlas().puppets.first().sequences.clear();
    workspace.refresh();

    // Double-clicking a variant does the same.
    emit workspace.tree()->itemDoubleClicked(variantItem(1, 1), 0);
    QCoreApplication::processEvents();
    QCOMPARE(doll.puppet().poses[1].settings[0].variant, QStringLiteral("head-smiling"));
    QCOMPARE(workspace.selection(), (PuppetSelection{0, 1, 1}));

    // On Rest, it becomes the rest variant, first, and stays selected.
    workspace.selectPose(0);
    emit workspace.tree()->itemDoubleClicked(variantItem(1, 1), 0);
    QCoreApplication::processEvents();
    QCOMPARE(puppetVariantName(doll.puppet().parts[1].variants[0]), QStringLiteral("head-smiling"));
    QCOMPARE(workspace.selection(), (PuppetSelection{0, 1, 0}));
    QCOMPARE(variantItem(1, 0)->text(1), QStringLiteral("sprite · shown"));
    // The tree still lists the variants by name, and the panel's Shows too.
    QCOMPARE(treeRows(workspace.tree()).mid(4), (QStringList{"    head | sprite", "    head-smiling | sprite · shown"}));
    workspace.select({0, 1, -1});
    auto* shows = workspace.findChild<QComboBox*>(QStringLiteral("partShownVariant"));
    QCOMPARE(shows->itemText(0), QStringLiteral("head"));
    QCOMPARE(shows->itemText(1), QStringLiteral("★ head-smiling"));
    QCOMPARE(shows->currentIndex(), 1);

    // While an animation plays, the tree doesn't change poses.
    workspace.bottomTabs()->setCurrentIndex(1);
    workspace.select({0, 1, 1});
    std::unique_ptr<QMenu> playingMenu(workspace.createContextMenu());
    QVERIFY(menuItems(playingMenu.get()).contains(QStringLiteral("[Show in Pose]")));
}
