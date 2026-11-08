#include "NineSliceWorkspaceTests.h"
#include "NineSliceEditorCanvas.h"
#include "NineSliceWorkspace.h"
#include "NavigatorPanel.h"
#include "NavigatorTreeWidget.h"
#include "ProjectSession.h"
#include "SpriteModels.h"
#include "SpritePickerDialog.h"
#include "SpriteTreeUtils.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QTreeWidgetItemIterator>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace {
/// A project whose sprites are square gray images, one per entry of `sizes`, named by `names`.
struct SpriteProject {
    QTemporaryDir dir;
    ProjectSession session;
    QVector<SpritePtr> sprites;

    SpriteProject(const QStringList& names, const QVector<int>& sizes) {
        session.currentFolder = dir.path();
        for (int i = 0; i < names.size(); ++i) {
            const QString path = QDir::cleanPath(dir.filePath(names[i] + QStringLiteral(".png")));
            QDir().mkpath(QFileInfo(path).absolutePath());
            QImage image(sizes[i], sizes[i], QImage::Format_ARGB32);
            image.fill(Qt::gray);
            image.save(path);
            auto sprite = std::make_shared<Sprite>();
            sprite->path = path;
            sprite->name = names[i];
            sprites.append(sprite);
            session.activeFramePaths.append(path);
            session.activeAtlas().spritePaths.append(path);
            session.spriteIndex.insert(path, sprite);
        }
    }
};

QList<int> spinInsets(const NineSliceWorkspace& workspace) {
    QList<int> values;
    for (const char* name : {"nineSliceLeftInset", "nineSliceTopInset", "nineSliceRightInset",
                             "nineSliceBottomInset"}) {
        auto* spin = workspace.findChild<QSpinBox*>(QLatin1String(name));
        values.append(spin ? spin->value() : -1);
    }
    return values;
}

QList<int> spriteInsets(const Sprite& sprite) {
    return {sprite.nsLeft, sprite.nsTop, sprite.nsRight, sprite.nsBottom};
}

bool showsEmptyState(const NineSliceWorkspace& workspace) {
    auto* stack = workspace.findChild<QStackedWidget*>(QStringLiteral("nineSliceNavigatorStack"));
    return stack && stack->currentWidget()
        && stack->currentWidget()->objectName() == QLatin1String("nineSliceEmptyState");
}
}

void NineSliceWorkspaceTests::testInsetsThatDontFitAreFittedOnLoad() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = QDir::cleanPath(dir.filePath(QStringLiteral("panel.png")));
    QImage image(32, 32, QImage::Format_ARGB32);
    image.fill(Qt::gray);
    QVERIFY(image.save(path));

    // Insets saved before the editor kept them fitted: 20 + 20 > 32.
    auto sprite = std::make_shared<Sprite>();
    sprite->path = path;
    sprite->name = QStringLiteral("panel");
    sprite->isNineSliced = true;
    sprite->nsLeft = 20;
    sprite->nsTop = 8;
    sprite->nsRight = 20;
    sprite->nsBottom = 30;

    ProjectSession session;
    session.currentFolder = dir.path();
    session.activeFramePaths = {path};
    session.activeAtlas().spritePaths = {path};
    session.spriteIndex.insert(path, sprite);

    NineSliceWorkspace workspace(&session);
    QSignalSpy changes(&workspace, &NineSliceWorkspace::definitionsChanged);
    workspace.enter();

    auto spinValue = [&](const char* name) {
        auto* spin = workspace.findChild<QSpinBox*>(QLatin1String(name));
        return spin ? spin->value() : -1;
    };
    // Right and bottom give up the excess, as in the export.
    const QList<int> expected{20, 8, 12, 24};
    QCOMPARE((QList<int>{spinValue("nineSliceLeftInset"), spinValue("nineSliceTopInset"),
                         spinValue("nineSliceRightInset"), spinValue("nineSliceBottomInset")}),
             expected);
    const NineSliceEditorCanvas* canvas = workspace.canvas();
    QCOMPARE((QList<int>{canvas->leftInset(), canvas->topInset(),
                         canvas->rightInset(), canvas->bottomInset()}),
             expected);
    QCOMPARE((QList<int>{sprite->nsLeft, sprite->nsTop, sprite->nsRight, sprite->nsBottom}),
             expected);
    // Fitting changed the project, so it must be saved again.
    QCOMPARE(changes.count(), 1);
}

void NineSliceWorkspaceTests::testInsetsThatFitAreKeptOnLoad() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = QDir::cleanPath(dir.filePath(QStringLiteral("panel.png")));
    QImage image(32, 32, QImage::Format_ARGB32);
    image.fill(Qt::gray);
    QVERIFY(image.save(path));

    auto sprite = std::make_shared<Sprite>();
    sprite->path = path;
    sprite->name = QStringLiteral("panel");
    sprite->isNineSliced = true;
    sprite->nsLeft = 4;
    sprite->nsTop = 6;
    sprite->nsRight = 8;
    sprite->nsBottom = 10;
    sprite->nsVMode = QStringLiteral("repeat");

    ProjectSession session;
    session.currentFolder = dir.path();
    session.activeFramePaths = {path};
    session.activeAtlas().spritePaths = {path};
    session.spriteIndex.insert(path, sprite);

    NineSliceWorkspace workspace(&session);
    QSignalSpy changes(&workspace, &NineSliceWorkspace::definitionsChanged);
    workspace.enter();

    // Opening the workspace selects this sprite first; it must not overwrite it.
    QCOMPARE((QList<int>{sprite->nsLeft, sprite->nsTop, sprite->nsRight, sprite->nsBottom}),
             (QList<int>{4, 6, 8, 10}));
    QCOMPARE(sprite->nsVMode, QStringLiteral("repeat"));
    QCOMPARE(changes.count(), 0);
}

void NineSliceWorkspaceTests::testOnlyNineSlicedSpritesAreListed() {
    SpriteProject project({QStringLiteral("panel"), QStringLiteral("hero"), QStringLiteral("coin")},
                          {30, 30, 30});
    QVERIFY(project.dir.isValid());
    project.sprites[0]->isNineSliced = true;

    NineSliceWorkspace workspace(&project.session);
    workspace.enter();

    NavigatorPanel* navigator = workspace.navigatorPanel();
    QVERIFY(navigator->findSpriteItem(project.sprites[0]->path));
    QVERIFY(!navigator->findSpriteItem(project.sprites[1]->path));
    QVERIFY(!navigator->findSpriteItem(project.sprites[2]->path));
    QVERIFY(!showsEmptyState(workspace));
    QCOMPARE(workspace.canvas()->sourceSize(), QSize(30, 30));
    QVERIFY(workspace.canvas()->emptyText().isEmpty());
    QVERIFY(workspace.findChild<QSpinBox*>(QStringLiteral("nineSliceWidth"))->isEnabled());
    // Thumbnails are decoded off the UI thread, and set on the row when ready.
    QTreeWidgetItem* row = navigator->findSpriteItem(project.sprites[0]->path);
    QTRY_VERIFY_WITH_TIMEOUT(!row->icon(0).isNull(), 5000);
}

void NineSliceWorkspaceTests::testEmptyStateWithoutNineSlicedSprites() {
    SpriteProject project({QStringLiteral("hero"), QStringLiteral("coin")}, {30, 30});
    QVERIFY(project.dir.isValid());

    NineSliceWorkspace workspace(&project.session);
    workspace.enter();

    QVERIFY(showsEmptyState(workspace));
    QVERIFY(!workspace.navigatorPanel()->firstSpriteItem());
    QVERIFY(workspace.canvas()->sourceSize().isEmpty());
    // The canvas says what to do, and there is nothing to size.
    QVERIFY(!workspace.canvas()->emptyText().isEmpty());
    QVERIFY(!workspace.findChild<QSpinBox*>(QStringLiteral("nineSliceWidth"))->isEnabled());
    QVERIFY(!workspace.findChild<QSpinBox*>(QStringLiteral("nineSliceHeight"))->isEnabled());
    auto* removeButton = workspace.findChild<QPushButton*>(QStringLiteral("nineSliceRemoveButton"));
    QVERIFY(removeButton);
    QVERIFY(!removeButton->isEnabled());
}

void NineSliceWorkspaceTests::testAddingSpritesGivesEachOneThirds() {
    SpriteProject project({QStringLiteral("panel"), QStringLiteral("button"), QStringLiteral("hero")},
                          {30, 60, 30});
    QVERIFY(project.dir.isValid());

    NineSliceWorkspace workspace(&project.session);
    workspace.enter();
    QVERIFY(showsEmptyState(workspace));

    QSignalSpy changes(&workspace, &NineSliceWorkspace::definitionsChanged);
    workspace.addSprites({project.sprites[1]->path, project.sprites[0]->path});

    QVERIFY(project.sprites[0]->isNineSliced);
    QVERIFY(project.sprites[1]->isNineSliced);
    QVERIFY(!project.sprites[2]->isNineSliced);
    // Both get insets, not only the one shown, so both export sliced.
    QCOMPARE(spriteInsets(*project.sprites[0]), (QList<int>{10, 10, 10, 10}));
    QCOMPARE(spriteInsets(*project.sprites[1]), (QList<int>{20, 20, 20, 20}));
    QVERIFY(!showsEmptyState(workspace));
    // The first picked sprite is shown.
    QCOMPARE(workspace.canvas()->sourceSize(), QSize(60, 60));
    QCOMPARE(spinInsets(workspace), (QList<int>{20, 20, 20, 20}));
    QVERIFY(changes.count() >= 1);
}

void NineSliceWorkspaceTests::testRemovingKeepsInsetsAndShowsAnotherSprite() {
    SpriteProject project({QStringLiteral("a_panel"), QStringLiteral("b_button")}, {30, 60});
    QVERIFY(project.dir.isValid());
    for (const SpritePtr& sprite : project.sprites) sprite->isNineSliced = true;
    project.sprites[0]->nsLeft = 4;
    project.sprites[0]->nsTop = 5;
    project.sprites[0]->nsRight = 6;
    project.sprites[0]->nsBottom = 7;
    project.sprites[1]->nsLeft = 11;
    project.sprites[1]->nsTop = 12;
    project.sprites[1]->nsRight = 13;
    project.sprites[1]->nsBottom = 14;

    NineSliceWorkspace workspace(&project.session);
    workspace.enter();
    NavigatorPanel* navigator = workspace.navigatorPanel();
    auto* tree = navigator->tree();
    tree->setCurrentItem(navigator->findSpriteItem(project.sprites[0]->path));
    QCOMPARE(spinInsets(workspace), spriteInsets(*project.sprites[0]));

    QSignalSpy changes(&workspace, &NineSliceWorkspace::definitionsChanged);
    workspace.removeSprites({project.sprites[0]->path});

    QVERIFY(!project.sprites[0]->isNineSliced);
    QCOMPARE(spriteInsets(*project.sprites[0]), (QList<int>{4, 5, 6, 7}));
    QVERIFY(!navigator->findSpriteItem(project.sprites[0]->path));
    QCOMPARE(tree->currentItem(), navigator->findSpriteItem(project.sprites[1]->path));
    QCOMPARE(spinInsets(workspace), (QList<int>{11, 12, 13, 14}));
    QCOMPARE(changes.count(), 1);

    // Adding it again brings its insets back.
    workspace.addSprites({project.sprites[0]->path});
    QCOMPARE(spriteInsets(*project.sprites[0]), (QList<int>{4, 5, 6, 7}));
    QCOMPARE(spinInsets(workspace), (QList<int>{4, 5, 6, 7}));
}

void NineSliceWorkspaceTests::testRemovingTheLastSpriteShowsTheEmptyState() {
    SpriteProject project({QStringLiteral("panel")}, {30});
    QVERIFY(project.dir.isValid());
    project.sprites[0]->isNineSliced = true;

    NineSliceWorkspace workspace(&project.session);
    workspace.enter();
    workspace.navigatorPanel()->tree()->selectAll();
    auto* removeButton = workspace.findChild<QPushButton*>(QStringLiteral("nineSliceRemoveButton"));
    QVERIFY(removeButton);
    QVERIFY(removeButton->isEnabled());
    removeButton->click();

    QVERIFY(!project.sprites[0]->isNineSliced);
    QVERIFY(showsEmptyState(workspace));
    QVERIFY(workspace.canvas()->sourceSize().isEmpty());
    QVERIFY(!removeButton->isEnabled());
}

void NineSliceWorkspaceTests::testPickerListsOnlyFilteredSprites() {
    SpriteProject project({QStringLiteral("panel"), QStringLiteral("hero"), QStringLiteral("coin")},
                          {30, 30, 30});
    QVERIFY(project.dir.isValid());
    project.sprites[0]->isNineSliced = true;

    SpritePickerDialog::Options options;
    options.filter = [](const SpritePtr& sprite) { return !sprite->isNineSliced; };
    SpritePickerDialog dialog(&project.session, options);
    auto* navigator = dialog.findChild<NavigatorPanel*>();
    QVERIFY(navigator);
    QVERIFY(!navigator->findSpriteItem(project.sprites[0]->path));
    QVERIFY(navigator->findSpriteItem(project.sprites[1]->path));
    QVERIFY(navigator->findSpriteItem(project.sprites[2]->path));
    QVERIFY(dialog.selectedSpritePaths().isEmpty());

    // Ticked sprites are picked, each once.
    auto* tree = navigator->tree();
    QVERIFY(tree->topLevelItemCount() > 0);
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        tree->topLevelItem(i)->setCheckState(0, Qt::Checked);
    QStringList picked = dialog.selectedSpritePaths();
    picked.sort();
    QStringList expected{project.sprites[1]->path, project.sprites[2]->path};
    expected.sort();
    QCOMPARE(picked, expected);
}

void NineSliceWorkspaceTests::testPickerPicksGroups() {
    // A "hero" group holding two groups of two sprites, plus one sprite outside any group.
    SpriteProject project({QStringLiteral("hero/walk/walk1"), QStringLiteral("hero/walk/walk2"),
                           QStringLiteral("hero/idle/idle1"), QStringLiteral("hero/idle/idle2"),
                           QStringLiteral("coin")},
                          {16, 16, 16, 16, 16});
    QVERIFY(project.dir.isValid());

    SpritePickerDialog::Options options;
    options.pickGroups = true;
    SpritePickerDialog dialog(&project.session, options);
    auto* navigator = dialog.findChild<NavigatorPanel*>();
    auto* tree = navigator->tree();
    auto* createButton = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
    QVERIFY(dialog.selectedGroups().isEmpty());
    QVERIFY(!createButton->isEnabled());

    QTreeWidgetItem* hero = nullptr;
    QTreeWidgetItem* walk = nullptr;
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        if ((*it)->text(0) == QLatin1String("hero")) hero = *it;
        if ((*it)->text(0) == QLatin1String("walk")) walk = *it;
    }
    QVERIFY(hero && walk);

    // Clicking a sprite doesn't select it; only groups can be picked.
    QTreeWidgetItem* coin = navigator->findSpriteItem(project.sprites[4]->path);
    QVERIFY(coin);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    tree->expandAll();
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tree->visualItemRect(coin).center());
    QVERIFY(!coin->isSelected());
    QVERIFY(dialog.selectedGroups().isEmpty());

    // Selecting "hero" (and "walk" inside it) gives each group that holds sprites once,
    // with its own sprites and its path from the top of the tree.
    hero->setSelected(true);
    walk->setSelected(true);
    const QVector<SpritePickerDialog::Group> groups = dialog.selectedGroups();
    QCOMPARE(groups.size(), 2);
    QHash<QString, SpritePickerDialog::Group> byName;
    for (const auto& group : groups) byName.insert(group.name, group);
    QVERIFY(byName.value("walk").path.endsWith(QLatin1String("hero/walk")));
    QVERIFY(byName.value("idle").path.endsWith(QLatin1String("hero/idle")));
    QStringList walkSprites = byName.value("walk").spritePaths;
    walkSprites.sort();
    QCOMPARE(walkSprites, (QStringList{project.sprites[0]->path, project.sprites[1]->path}));
    QStringList idleSprites = byName.value("idle").spritePaths;
    idleSprites.sort();
    QCOMPARE(idleSprites, (QStringList{project.sprites[2]->path, project.sprites[3]->path}));
    QVERIFY(createButton->isEnabled());
    QCOMPARE(createButton->text(), QStringLiteral("OK (2)"));
}

void NineSliceWorkspaceTests::testPickerAsksForAName() {
    SpriteProject project({QStringLiteral("hero")}, {30});
    QVERIFY(project.dir.isValid());

    // Without Options::name there is no Name field.
    {
        SpritePickerDialog dialog(&project.session, SpritePickerDialog::Options{});
        QVERIFY(!dialog.findChild<QLineEdit*>(QStringLiteral("spritePickerName")));
        QVERIFY(dialog.name().isEmpty());
    }

    SpritePickerDialog::Options options;
    options.allowEmpty = true;
    options.name = QStringLiteral("Timeline 1");
    options.nameProblem = [](const QString& name) {
        return name == QStringLiteral("walk") ? QStringLiteral("taken") : QString();
    };
    SpritePickerDialog dialog(&project.session, options);
    auto* nameEdit = dialog.findChild<QLineEdit*>(QStringLiteral("spritePickerName"));
    QVERIFY(nameEdit);
    auto* problem = dialog.findChild<QLabel*>(QStringLiteral("spritePickerNameProblem"));
    QVERIFY(problem);
    QPushButton* accept = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
    QCOMPARE(dialog.name(), QStringLiteral("Timeline 1"));
    QVERIFY(accept->isEnabled());

    // A taken name is refused, and says why; so is no name.
    nameEdit->setText(QStringLiteral("walk"));
    QVERIFY(!accept->isEnabled());
    QCOMPARE(problem->text(), QStringLiteral("taken"));
    nameEdit->setText(QStringLiteral("   "));
    QVERIFY(!accept->isEnabled());
    nameEdit->setText(QStringLiteral(" run "));
    QVERIFY(accept->isEnabled());
    QCOMPARE(dialog.name(), QStringLiteral("run"));
}

void NineSliceWorkspaceTests::testFilteringATreeKeepsAncestorsAndDescendants() {
    QTreeWidget tree;
    auto* doll = new QTreeWidgetItem(&tree, {QStringLiteral("doll")});
    auto* head = new QTreeWidgetItem(doll, {QStringLiteral("head")});
    auto* smile = new QTreeWidgetItem(head, {QStringLiteral("smile")});
    auto* body = new QTreeWidgetItem(doll, {QStringLiteral("body")});
    auto* robot = new QTreeWidgetItem(&tree, {QStringLiteral("robot")});
    auto* arm = new QTreeWidgetItem(robot, {QStringLiteral("arm")});

    // A match shows its ancestors and everything under it, whatever the case.
    QCOMPARE(SpriteTreeUtils::filterTree(&tree, QStringLiteral("HEAD")), 1);
    QVERIFY(!doll->isHidden());
    QVERIFY(!head->isHidden());
    QVERIFY(!smile->isHidden());
    QVERIFY(body->isHidden());
    QVERIFY(robot->isHidden());
    QVERIFY(arm->isHidden());

    QCOMPARE(SpriteTreeUtils::filterTree(&tree, QStringLiteral("robot")), 1);
    QVERIFY(doll->isHidden());
    QVERIFY(!robot->isHidden());
    QVERIFY(!arm->isHidden());

    // Other columns count when asked for.
    head->setText(1, QStringLiteral("neck"));
    QCOMPARE(SpriteTreeUtils::filterTree(&tree, QStringLiteral("neck")), 0);
    QCOMPARE(SpriteTreeUtils::filterTree(&tree, QStringLiteral("neck"), {0, 1}), 1);

    // No text shows everything.
    QCOMPARE(SpriteTreeUtils::filterTree(&tree, QString()), 0);
    for (QTreeWidgetItemIterator it(&tree); *it; ++it) QVERIFY(!(*it)->isHidden());
}
