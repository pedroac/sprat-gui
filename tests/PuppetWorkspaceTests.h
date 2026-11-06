#pragma once

#include <QObject>

class PuppetWorkspaceTests : public QObject {
    Q_OBJECT

private slots:
    void testBuildingAPuppetIsUndoable();
    void testNewPuppetDialogBuildsAPuppetFromDroppedSprites();
    void testCanvasPlacesPartsByTheirPivot();
    void testClickingAPartSelectsIt();
    void testDraggingAPartMovesIt();
    void testArrowKeysNudgeTheSelectedPart();
    void testEditingKeepsTheViewStill();
    void testDraggingThePivotOntoAMarkerAttaches();
    void testDraggingThePivotAwayDetaches();
    void testPanelAttachesAndDetaches();
    void testPanelSetsJointLimits();
    void testTurningAnAttachedPartIsNotSaved();
    void testTurningStopsAtTheAngleLimits();
    void testDriftingStaysWithinTheLeeway();
    void testTurningAPositionedPartByItsHandle();
    void testPullingAHandTurnsTheArm();
    void testAngleFieldTurnsThePart();
    void testRenamingAPartUpdatesItsAttachments();
    void testPanelRefusesATakenName();
    void testPositionEditsAreOneUndoStep();
    void testReorderingPartsAndVariants();
    void testRemovingAPartSelectsItsNeighbour();
};
