#pragma once
#include <QtTest>

class PuppetTests : public QObject {
    Q_OBJECT
private slots:
    void testReadsReadmeExample();
    void testWriteThenReadKeepsEverything();
    void testReadSkipsBadLinesWithWarnings();
    void testReadRejectsRepeatedNames();
    void testQuotedNamesAndSpriteIndexesRoundTrip();
    void testDefaultVariantNamesFollowSpratCli();
    void testEvaluatorPlacesRestPose();
    void testEvaluatorTurnsChildrenWithTheirParent();
    void testEvaluatorReportsPartsItCannotPlace();
    void testProjectPayloadKeepsPuppets();
    void testEditingChecksNames();
    void testRenamingAPartRenamesItsJointsAndPoses();
    void testRemovingAPartRemovesItsJointsAndPoses();
    void testNamingVariants();
    void testAddingSpriteVariants();
    void testAttachingAndDetachingParts();
    void testAttachingRefusesLoops();
    void testJointsTurnWithinTheirLimits();
    void testJointsDriftWithinTheirMarkerAndLeeway();
    void testPullingAPartTurnsThePartsItHangsFrom();
    void testPullingAPartSlidesItsJointFirst();
    void testPoseStatesComeFromWhatItSets();
    void testEditingPosesKeepsSequencesInStep();
};
