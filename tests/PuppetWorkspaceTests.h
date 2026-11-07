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
    void testTurningAPartEditsTheSelectedPose();
    void testTurningStopsAtTheAngleLimits();
    void testDriftingStaysWithinTheLeeway();
    void testTurningAPositionedPartByItsHandle();
    void testPullingAHandTurnsTheArm();
    void testPullingSlidesWithinTheLeeway();
    void testHiddenMarkersAreNotOffered();
    void testMarkerListDoesNotSpreadThePanelRows();
    void testAngleFieldTurnsThePart();
    void testNewPoseSetsOnlyThePartsChanged();
    void testSelectingAPoseShowsIt();
    void testTickingPartsInAndOutOfThePose();
    void testPosingMovesPositionedPartsInThePose();
    void testPoseStripRenamesReordersAndRemoves();
    void testRenamingAPartUpdatesItsAttachments();
    void testPanelRefusesATakenName();
    void testPositionEditsAreOneUndoStep();
    void testReorderingPartsAndVariants();
    void testRemovingAPartSelectsItsNeighbour();
};
