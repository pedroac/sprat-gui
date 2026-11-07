#include "PuppetWorkspaceTests.h"
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
#include <QDialogButtonBox>
#include <QMenu>
#include <QMimeData>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
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
void dropSprites(QTreeWidget* tree, const QPoint& pos, const QStringList& spritePaths) {
    QMimeData mime;
    mime.setData(QStringLiteral("application/x-sprat-sprite"), spritePaths.join(QLatin1Char('\n')).toUtf8());
    QDragEnterEvent enter(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(tree->viewport(), &enter);
    QDragMoveEvent move(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(tree->viewport(), &move);
    QDropEvent drop(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(tree->viewport(), &drop);
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

    // Sprites dropped on empty space make a part each, named after the sprite.
    QTreeWidget* parts = dialog.partsTree();
    dropSprites(parts, parts->viewport()->rect().bottomLeft() - QPoint(-10, 10), {doll.body->path, doll.head->path});
    QCOMPARE(partRows(parts), (QStringList{"body", "  body", "head", "  head"}));
    // Dropped on a part (or one of its sprites), they become its variants.
    dropSprites(parts, parts->visualItemRect(parts->topLevelItem(1)->child(0)).center(), {doll.headSmiling->path});
    QCOMPARE(partRows(parts), (QStringList{"body", "  body", "head", "  head", "  head-smiling"}));

    // An empty part waits for sprites; a taken part name is refused.
    dialog.addPart();
    QCOMPARE(partRows(parts).mid(5), (QStringList{"part", "  (drop sprites here)"}));
    parts->topLevelItem(2)->setText(0, QStringLiteral("head"));
    QCoreApplication::processEvents();
    QCOMPARE(parts->topLevelItem(2)->text(0), QStringLiteral("part"));
    QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("newPuppetPartProblem"))->isVisibleTo(&dialog));
    parts->topLevelItem(2)->setText(0, QStringLiteral("tail"));
    QCoreApplication::processEvents();
    QCOMPARE(parts->topLevelItem(2)->text(0), QStringLiteral("tail"));
    parts->setCurrentItem(parts->topLevelItem(2));
    dialog.findChild<QPushButton*>(QStringLiteral("newPuppetRemove"))->click();
    QCOMPARE(parts->topLevelItemCount(), 2);

    // The sprite list's own button adds its selection to the selected part.
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

namespace {
/// The doll with two poses: "nod" turns the head 20°, "smile" shows the smiling head.
void addDollPoses(Doll& doll) {
    PuppetPoseSetting nod;
    nod.part = QStringLiteral("head");
    nod.angle = 20;
    PuppetPoseSetting smile;
    smile.part = QStringLiteral("head");
    smile.variant = QStringLiteral("head-smiling");
    doll.project.session.activeAtlas().puppets.first().poses = {{QStringLiteral("nod"), {nod}},
                                                                {QStringLiteral("smile"), {smile}}};
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

/// The strip's frame names, "Rest" first.
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

    // The head is placed by its attachment: dragging it selects it but doesn't move it.
    const Puppet beforeHeadDrag = doll.puppet();
    const QPoint head = canvas->mapFromScene(QPointF(12, -41));
    QTest::mousePress(viewport, Qt::LeftButton, {}, head);
    QTest::mouseMove(viewport, head + QPoint(20, 0));
    QTest::mouseRelease(viewport, Qt::LeftButton, {}, head + QPoint(20, 0));
    QCOMPARE(workspace.selection(), (PuppetSelection{0, 1, -1}));
    QVERIFY(doll.puppet() == beforeHeadDrag);
    QCOMPARE(doll.project.undoStack.index(), 1);
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

    // Rest is the rig: dragging the attached head doesn't turn it.
    dragOnCanvas(workspace.canvas(), QPointF(0, -50), QPointF(12, -38));
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(0));
    QCOMPARE(doll.project.undoStack.count(), 0);
    QVERIFY(!workspace.findChild<QSpinBox*>(QStringLiteral("partAngle"))->isEnabled());

    // In a pose, from above the joint to its right: a quarter turn clockwise, saved in the pose.
    startPose(workspace);
    dragOnCanvas(workspace.canvas(), QPointF(0, -50), QPointF(12, -38));
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(90));
    QCOMPARE(doll.puppet().parts, rig.parts);
    QCOMPARE(doll.puppet().attachments, rig.attachments);
    QCOMPARE(doll.puppet().poses[0].settings.size(), 1);
    QCOMPARE(doll.puppet().poses[0].settings[0].part, QStringLiteral("head"));
    QCOMPARE(doll.puppet().poses[0].settings[0].angle, std::optional<int>(90));
    QCOMPARE(doll.project.undoStack.count(), 2);  // the new pose, then the whole drag
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Pull Part"));

    doll.project.undoStack.undo();
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(0));
    QVERIFY(doll.puppet().poses[0].settings.isEmpty());
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
    QCOMPARE(doll.puppet().poses[0].settings[0].offset, std::optional<QPoint>(QPoint(2, 0)));
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

    // At Rest there's no handle: the same drag, starting on the body, moves the rig's body.
    dragOnCanvas(workspace.canvas(), QPointF(0, -36), QPointF(36, 0));
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(36, 36)));
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
    QCOMPARE(doll.puppet().poses[0].settings[0].position, std::optional<QPoint>(QPoint(0, 15)));
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
    for (const PuppetPoseSetting& setting : project.session.activeAtlas().puppets.first().poses[0].settings)
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

    // The head can't hang from it: dropping its pivot there places it there instead.
    workspace.select({0, 1, -1});
    auto* parentMarker = workspace.findChild<QComboBox*>(QStringLiteral("partParentMarkerCombo"));
    QCOMPARE(parentMarker->findData(QStringLiteral("neck")), -1);
    dragOnCanvas(workspace.canvas(), QPointF(40, 0), QPointF(0, -38));
    QVERIFY(doll.puppet().attachments.isEmpty());
    QCOMPARE(doll.puppet().parts[1].position, std::optional<QPoint>(QPoint(0, -38)));

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
        QCoreApplication::processEvents();
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
    QCOMPARE(frameNames(workspace.poseStrip()), QStringList{"Rest"});
    QVERIFY(!workspace.findChild<QPushButton*>(QStringLiteral("puppetDuplicatePose"))->isEnabled());

    // A new pose sets nothing: every part is faint, and unticked under the tree.
    startPose(workspace);
    QCOMPARE(frameNames(workspace.poseStrip()), (QStringList{"Rest", "pose"}));
    QCOMPARE(workspace.selectedPose(), 0);
    QVERIFY(doll.puppet().poses[0].settings.isEmpty());
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("New Pose"));
    QListWidget* parts = workspace.poseParts();
    QCOMPARE(parts->count(), 2);
    QCOMPARE(parts->item(0)->checkState(), Qt::Unchecked);
    QVERIFY(partOpacity(workspace.canvas(), 1) < 1);

    // Turning the head adds it to the pose; the body stays out of it.
    workspace.select({0, 1, -1});
    workspace.findChild<QSpinBox*>(QStringLiteral("partAngle"))->setValue(30);
    QCOMPARE(doll.puppet().poses[0].settings.size(), 1);
    QCOMPARE(doll.puppet().poses[0].settings[0].part, QStringLiteral("head"));
    QCOMPARE(doll.puppet().poses[0].settings[0].angle, std::optional<int>(30));
    QCOMPARE(parts->item(1)->checkState(), Qt::Checked);
    QCOMPARE(parts->item(1)->text(), QStringLiteral("head · 30°"));
    QCOMPARE(parts->item(0)->checkState(), Qt::Unchecked);
    QCOMPARE(partOpacity(workspace.canvas(), 1), 1.0);
    QVERIFY(partOpacity(workspace.canvas(), 0) < 1);

    // The next pose goes after the selected one, with its own name.
    startPose(workspace);
    QCOMPARE(frameNames(workspace.poseStrip()), (QStringList{"Rest", "pose", "pose 2"}));
    doll.project.undoStack.undo();
    QCOMPARE(doll.puppet().poses.size(), 1);
}

void PuppetWorkspaceTests::testSelectingAPoseShowsIt() {
    Doll doll;
    addDollPoses(doll);
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.select({0, 1, -1});
    PuppetPoseStrip* strip = workspace.poseStrip();
    QCOMPARE(frameNames(strip), (QStringList{"Rest", "nod", "smile"}));
    auto* angle = workspace.findChild<QSpinBox*>(QStringLiteral("partAngle"));
    auto* shows = workspace.findChild<QComboBox*>(QStringLiteral("partShownVariant"));

    strip->setCurrentRow(1);
    QCOMPARE(workspace.selectedPose(), 0);
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(20));
    QCOMPARE(angle->value(), 20);
    strip->setCurrentRow(2);
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(0));
    QCOMPARE(shows->currentIndex(), 1);  // head-smiling
    strip->setCurrentRow(0);
    QCOMPARE(workspace.selectedPose(), -1);
    QCOMPARE(shows->currentIndex(), 0);
    QCOMPARE(doll.project.undoStack.count(), 0);
}

void PuppetWorkspaceTests::testTickingPartsInAndOutOfThePose() {
    Doll doll;
    addDollPoses(doll);
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.selectPose(0);  // "nod" sets the head only
    QListWidget* parts = workspace.poseParts();
    QCOMPARE(parts->item(0)->checkState(), Qt::Unchecked);
    QCOMPARE(parts->item(1)->checkState(), Qt::Checked);

    // Unticking the head leaves it out: it goes back to rest in this pose.
    parts->item(1)->setCheckState(Qt::Unchecked);
    QCoreApplication::processEvents();
    QVERIFY(doll.puppet().poses[0].settings.isEmpty());
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(0));
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Remove Part from Pose"));

    // Ticking the body sets it as it stands: at its rest angle.
    workspace.poseParts()->item(0)->setCheckState(Qt::Checked);
    QCoreApplication::processEvents();
    QCOMPARE(doll.puppet().poses[0].settings.size(), 1);
    QCOMPARE(doll.puppet().poses[0].settings[0].part, QStringLiteral("body"));
    QCOMPARE(doll.puppet().poses[0].settings[0].angle, std::optional<int>(0));
    QCOMPARE(partOpacity(workspace.canvas(), 0), 1.0);

    doll.project.undoStack.undo();
    doll.project.undoStack.undo();
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(20));
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
    QVERIFY(doll.puppet().poses[0].settings.isEmpty());
    QCOMPARE(parts->item(1)->checkState(), Qt::Unchecked);
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(0));
    QCOMPARE(workspace.selection(), (PuppetSelection{0, 1, -1}));
}

void PuppetWorkspaceTests::testPosingMovesPositionedPartsInThePose() {
    Doll doll;
    addDollPoses(doll);
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    showAtFullSize(workspace);
    workspace.selectPose(0);

    // In a pose, dragging the body moves it in the pose, not in the rig.
    dragOnCanvas(workspace.canvas(), QPointF(0, -20), QPointF(20, -20));
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(0, 0)));
    QCOMPARE(drawnParts(workspace.canvas())[0], QRectF(10, -40, 20, 40));
    QCOMPARE(workspace.findChild<QSpinBox*>(QStringLiteral("partPositionX"))->value(), 20);
    const PuppetPose& nod = doll.puppet().poses[0];
    QCOMPARE(nod.settings.size(), 2);
    QCOMPARE(nod.settings[0].position, std::optional<QPoint>(QPoint(20, 0)));
    QCOMPARE(nod.settings[1].angle, std::optional<int>(20));
    QCOMPARE(doll.project.undoStack.count(), 1);

    // At Rest, it moves the rig again.
    workspace.selectPose(-1);
    dragOnCanvas(workspace.canvas(), QPointF(0, -20), QPointF(0, -5));
    QCOMPARE(doll.puppet().parts[0].position, std::optional<QPoint>(QPoint(0, 15)));
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
    QCOMPARE(frameNames(strip), (QStringList{"Rest", "nod", "smile"}));
    QVERIFY(!workspace.findChild<QLabel*>(QStringLiteral("puppetPoseStatus"))->text().isEmpty());
    strip->item(1)->setText(QStringLiteral("yes"));
    QCoreApplication::processEvents();
    QCOMPARE(PuppetEditing::poseNames(doll.puppet()), (QStringList{"yes", "smile"}));
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Rename Pose"));

    // Reordering keeps the moved pose selected.
    workspace.selectPose(0);
    emit strip->moveRequested(0, 1);
    QCOMPARE(PuppetEditing::poseNames(doll.puppet()), (QStringList{"smile", "yes"}));
    QCOMPARE(workspace.selectedPose(), 1);

    // Duplicating copies the selected pose after it.
    workspace.findChild<QPushButton*>(QStringLiteral("puppetDuplicatePose"))->click();
    QCOMPARE(PuppetEditing::poseNames(doll.puppet()), (QStringList{"smile", "yes", "yes 2"}));
    QCOMPARE(doll.puppet().poses[2].settings, doll.puppet().poses[1].settings);
    QCOMPARE(workspace.selectedPose(), 2);
    doll.project.undoStack.undo();
    workspace.selectPose(1);

    // Removing selects the frame before it.
    workspace.findChild<QPushButton*>(QStringLiteral("puppetRemovePose"))->click();
    QCOMPARE(PuppetEditing::poseNames(doll.puppet()), QStringList{"smile"});
    QCOMPARE(workspace.selectedPose(), 0);
    QCOMPARE(frameNames(strip), (QStringList{"Rest", "smile"}));
    doll.project.undoStack.undo();
    QCOMPARE(PuppetEditing::poseNames(doll.puppet()), (QStringList{"smile", "yes"}));
}

namespace {
/// The texts of @p menu's actions, separators as "-", disabled ones in brackets.
QStringList menuItems(QMenu* menu) {
    QStringList items;
    for (QAction* action : menu->actions())
        items.append(action->isSeparator() ? QStringLiteral("-")
                     : action->isEnabled() ? action->text()
                                           : QStringLiteral("[%1]").arg(action->text()));
    return items;
}

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

    // The same actions as an animation's frames; nothing goes before Rest.
    std::unique_ptr<QMenu> poseMenu(strip->createContextMenu(0));
    QCOMPARE(menuItems(poseMenu.get()), (QStringList{"Add Pose Before", "Add Pose After", "-", "[Move to the left]",
                                                     "Move to the right", "Duplicate", "-", "Delete"}));
    std::unique_ptr<QMenu> lastMenu(strip->createContextMenu(1));
    QVERIFY(menuItems(lastMenu.get()).contains(QStringLiteral("[Move to the right]")));
    std::unique_ptr<QMenu> restMenu(strip->createContextMenu(-1));
    QCOMPARE(menuItems(restMenu.get()), QStringList{"Add Pose After"});
    std::unique_ptr<QMenu> emptyMenu(strip->createContextMenu(-2));
    QCOMPARE(menuItems(emptyMenu.get()), QStringList{"Add Pose"});

    triggerFrameAction(strip, 0, QStringLiteral("Add Pose Before"));
    QCOMPARE(frameNames(strip), (QStringList{"Rest", "pose", "nod", "smile"}));
    QCOMPARE(workspace.selectedPose(), 0);
    triggerFrameAction(strip, -1, QStringLiteral("Add Pose After"));
    QCOMPARE(frameNames(strip), (QStringList{"Rest", "pose 2", "pose", "nod", "smile"}));
    triggerFrameAction(strip, 2, QStringLiteral("Move to the left"));  // nod
    QCOMPARE(frameNames(strip), (QStringList{"Rest", "pose 2", "nod", "pose", "smile"}));
    triggerFrameAction(strip, 1, QStringLiteral("Move to the right"));  // nod again
    QCOMPARE(frameNames(strip), (QStringList{"Rest", "pose 2", "pose", "nod", "smile"}));
    triggerFrameAction(strip, 2, QStringLiteral("Duplicate"));
    QCOMPARE(frameNames(strip), (QStringList{"Rest", "pose 2", "pose", "nod", "nod 2", "smile"}));
    QCOMPARE(doll.puppet().poses[3].settings, doll.puppet().poses[2].settings);
    QCOMPARE(workspace.selectedPose(), 3);
    triggerFrameAction(strip, 0, QStringLiteral("Delete"));
    QCOMPARE(frameNames(strip), (QStringList{"Rest", "pose", "nod", "nod 2", "smile"}));
    triggerFrameAction(strip, -2, QStringLiteral("Add Pose"));
    QCOMPARE(frameNames(strip).last(), QStringLiteral("pose 2"));

    // Backspace deletes the selected pose, as Delete does.
    workspace.selectPose(0);
    strip->setFocus();
    QTest::keyClick(strip, Qt::Key_Backspace);
    QCOMPARE(frameNames(strip), (QStringList{"Rest", "nod", "nod 2", "smile", "pose 2"}));
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Remove Pose"));
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
    QCOMPARE(menuItems(emptyMenu.get()), QStringList{"Add Pose"});
    addPoseFromMenu(emptyMenu.get(), QStringLiteral("Add Pose"), QStringLiteral("nod"));
    QCOMPARE(stepRows(doll), QStringList{"nod 4/4"});
    QCOMPARE(workspace.selectedStep(), 0);
    std::unique_ptr<QMenu> menu(track->createContextMenu(0));
    QCOMPARE(menuItems(menu.get()), (QStringList{"Add Pose Before", "Add Pose After", "-", "[Move to the left]",
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
    workspace.findChild<QComboBox*>(QStringLiteral("puppetStepPose"))->setCurrentIndex(0);
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

    // Rest shows the rig, whatever the poses do.
    workspace.selectPose(-1);
    QCOMPARE(angle->value(), 0);
    QVERIFY(!angle->isEnabled());
    QCOMPARE(drawnParts(workspace.canvas())[1], turnedHead(0));
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

    // Dropping a variant on its part's row makes it the first, the rest variant.
    emit workspace.tree()->moveRequested({0, 0, 1}, 0);
    QCOMPARE(puppetVariantName(doll.puppet().parts[0].variants[0]), QStringLiteral("head-smiling"));
    QCOMPARE(workspace.selection(), (PuppetSelection{0, 0, 0}));
    QCOMPARE(doll.project.undoStack.count(), 2);
    QCOMPARE(doll.project.undoStack.undoText(), QStringLiteral("Make Rest Variant"));
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
