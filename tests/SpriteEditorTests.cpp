#include "SpriteEditorTests.h"
#include "EditorOverlayItem.h"
#include "MarkerUtils.h"

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

namespace {

/// A 40x40 sprite with its pivot in the middle and a "point 1" marker near its top-left
/// corner, in an editor view at 100%.
struct Editor {
    QGraphicsScene scene;
    QGraphicsView view{&scene};
    EditorOverlayItem* overlay = new EditorOverlayItem;
    SpritePtr sprite = std::make_shared<Sprite>();

    Editor() {
        sprite->rect = QRect(0, 0, 40, 40);
        sprite->pivotX = 20;
        sprite->pivotY = 20;
        NamedPoint point;
        point.name = QStringLiteral("point 1");
        point.x = 5;
        point.y = 5;
        sprite->points = {point};
        scene.addItem(overlay);
        overlay->setSceneSize(QSize(40, 40));
        overlay->setSprites({sprite});
        scene.setSceneRect(0, 0, 40, 40);
        view.resize(200, 200);
        view.show();
    }

    /// Clicks the view where the sprite's pixel @p at is.
    void clickAt(const QPointF& at) {
        QTest::mouseClick(view.viewport(), Qt::LeftButton, {}, view.mapFromScene(at));
    }
};

}  // namespace

void SpriteEditorTests::testClickingTheSpriteMovesTheSelectedMarker() {
    Editor editor;
    editor.overlay->setSelectedMarker(QStringLiteral("point 1"));
    QSignalSpy selected(editor.overlay, &EditorOverlayItem::markerSelected);
    QSignalSpy pivot(editor.overlay, &EditorOverlayItem::pivotChanged);

    // Away from every marker and the pivot: the selected marker goes there, still selected,
    // and the pivot stays.
    editor.clickAt(QPointF(32, 32));
    QCOMPARE(editor.overlay->selectedMarkerName(), QStringLiteral("point 1"));
    QCOMPARE(QPoint(editor.sprite->points[0].x, editor.sprite->points[0].y), QPoint(32, 32));
    QCOMPARE(QPoint(editor.sprite->pivotX, editor.sprite->pivotY), QPoint(20, 20));
    QVERIFY(selected.isEmpty());
    QVERIFY(pivot.isEmpty());

    // Clicking the pivot itself still selects it.
    editor.clickAt(QPointF(20, 20));
    QCOMPARE(editor.overlay->selectedMarkerName(), QString());
    QCOMPARE(QPoint(editor.sprite->pivotX, editor.sprite->pivotY), QPoint(20, 20));
}

void SpriteEditorTests::testClickingTheSpritePlacesTheSelectedPivot() {
    Editor editor;
    QCOMPARE(editor.overlay->selectedMarkerName(), QString());  // the pivot

    editor.clickAt(QPointF(32, 32));
    QCOMPARE(QPoint(editor.sprite->pivotX, editor.sprite->pivotY), QPoint(32, 32));

    // A marker is still selected by clicking it.
    editor.clickAt(QPointF(5, 5));
    QCOMPARE(editor.overlay->selectedMarkerName(), QStringLiteral("point 1"));
}

void SpriteEditorTests::testTheMarkerAddedLastIsFound() {
    NamedPoint a;
    a.name = QStringLiteral("a");
    NamedPoint b;
    b.name = QStringLiteral("b");
    NamedPoint c;
    c.name = QStringLiteral("c");
    QCOMPARE(addedMarkerName({a}, {a, b, c}), QStringLiteral("c"));
    QCOMPARE(addedMarkerName({a, b}, {a, b}), QString());
    QCOMPARE(addedMarkerName({a, b}, {b}), QString());  // removed, none added
    QCOMPARE(addedMarkerName({}, {a}), QStringLiteral("a"));
}
