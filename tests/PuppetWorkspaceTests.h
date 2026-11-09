#pragma once

#include <QObject>

class PuppetWorkspaceTests : public QObject {
    Q_OBJECT

private slots:
    void testBuildingAPuppetIsUndoable();
    void testNewPuppetDialogBuildsAPuppetFromDroppedSprites();
    void testNewPuppetDialogPartsMenu();
    void testCanvasPlacesPartsByTheirPivot();
    void testClickingAPartSelectsIt();
    void testDraggingAPartMovesIt();
    void testArrowKeysNudgeTheSelectedPart();
    void testRigLockKeepsThePlacement();
    void testPoseLockKeepsThePartInThatPose();
    void testGridButtonShowsTheGrid();
    void testEditingKeepsTheViewStill();
    void testDraggingThePivotOntoAMarkerAttaches();
    void testDroppingThePivotOnAPartAddsAMarker();
    void testDoubleClickingThePivotHangsThePartWhereItIs();
    void testPanelHangsAPartFromANewMarker();
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
    void testPosesTabIsAsTallAsItsContent();
    void testCanvasReusesPartItemsAcrossEdits();
    void testAngleFieldTurnsThePart();
    void testScalingFadingAndTintingAPart();
    void testNewPoseSetsOnlyThePartsChanged();
    void testSelectingAPoseShowsIt();
    void testTickingPartsInAndOutOfThePose();
    void testClickingAPartOutOfThePose();
    void testPosingMovesPositionedPartsInThePose();
    void testPoseStripRenamesReordersAndRemoves();
    void testPoseStripContextMenuWorksLikeTheFrameList();
    void testBuildingAnAnimationFromPoses();
    void testPlayingAnAnimation();
    void testAnimationVariantsPlayAlong();
    void testTinyPartsAreKeyedInEveryFrameAndLoop();
    void testStoppingLetsTheCanvasEditTheStep();
    void testSettingsTuneTheCanvasAndNewSteps();
    void testDraggingNearTheSelectedPartChangesIt();
    void testPuppetsSettingsDialogEditsAndSavesTheSettings();
    void testAnimationFramesAreEditedApartFromOtherAnimations();
    void testRenamingAPartUpdatesItsAttachments();
    void testPanelRefusesATakenName();
    void testPositionEditsAreOneUndoStep();
    void testReorderingPartsAndVariants();
    void testStepPartsEaseTheirOwnWay();
    void testDrawOrderMenusOnTheTreeAndCanvas();
    void testDraggingAPartOpensAGapWhereItLands();
    void testRestPoseVariantIsTheRestVariant();
    void testShowingAVariantInAPoseFromTheTree();
    void testRemovingAPartSelectsItsNeighbour();
};
