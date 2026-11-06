#include "PuppetWorkspaceTests.h"
#include "ProjectSession.h"
#include "PuppetCanvas.h"
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
#include <QLineEdit>
#include <QRadioButton>
#include <QSpinBox>
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

    workspace.addPuppet();
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

void PuppetWorkspaceTests::testRenamingAPartUpdatesItsAttachments() {
    Doll doll;
    PuppetWorkspace workspace(&doll.project.session, &doll.project.undoStack);
    workspace.refreshSprites();
    workspace.select({0, 0, -1});

    workspace.tree()->currentItem()->setText(0, QStringLiteral("torso"));
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
