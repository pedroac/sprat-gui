#pragma once

#include <QObject>

class PuppetWorkspaceTests : public QObject {
    Q_OBJECT

private slots:
    void testBuildingAPuppetIsUndoable();
    void testCanvasPlacesPartsByTheirPivot();
    void testClickingAPartSelectsIt();
    void testDraggingAPartMovesIt();
    void testArrowKeysNudgeTheSelectedPart();
    void testDraggingThePivotOntoAMarkerAttaches();
    void testDraggingThePivotAwayDetaches();
    void testPanelAttachesAndDetaches();
    void testPanelSetsJointLimits();
    void testTurningAnAttachedPartIsNotSaved();
    void testTurningStopsAtTheAngleLimits();
    void testDriftingStaysWithinTheLeeway();
    void testRenamingAPartUpdatesItsAttachments();
    void testPanelRefusesATakenName();
    void testPositionEditsAreOneUndoStep();
    void testReorderingPartsAndVariants();
    void testRemovingAPartSelectsItsNeighbour();
};
