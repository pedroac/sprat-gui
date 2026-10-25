#include "NineSliceWorkspaceTests.h"
#include "NineSliceEditorCanvas.h"
#include "NineSliceWorkspace.h"
#include "ProjectSession.h"
#include "SpriteModels.h"

#include <QDir>
#include <QImage>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

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
