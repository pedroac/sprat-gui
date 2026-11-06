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
};
