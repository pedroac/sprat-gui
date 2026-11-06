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
#include <QLineEdit>
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
