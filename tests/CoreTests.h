#pragma once
#include <QtTest>

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void testMarkerKindConversions();
    void testMarkerNameNormalization();
    void testResolutionUtils();
    void testUpdateOffersOnlyNewerVersions();
    void testUpdateAssetNeedsSha256Digest();
    void testPathSafetyRejectsEscapingPaths();
    void testArchiveExtractionSkipsLinks();
    void testCreateZipRoundTripsAndKeepsOriginalOnFailure();
    void testTrashBinMoveFolderKeepsExistingTarget();
    void testClickOnEmptySpacePositionsSelectedMarker();
    void testMessagesShowParagraphsAndToolOutput();
};
