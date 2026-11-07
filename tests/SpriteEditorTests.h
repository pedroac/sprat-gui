#pragma once
#include <QObject>

/// The selected sprite's editor (EditorOverlayItem): selecting markers and moving the pivot.
class SpriteEditorTests : public QObject {
    Q_OBJECT

private slots:
    void testClickingTheSpriteKeepsTheSelectedMarker();
    void testClickingTheSpritePlacesTheSelectedPivot();
    void testTheMarkerAddedLastIsFound();
};
