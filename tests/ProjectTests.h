#pragma once
#include <QtTest>

class ProjectTests : public QObject {
    Q_OBJECT
private slots:
    void testProjectPayloadBuildStoresListSource();
    void testProjectPayloadMigratesLegacyRawTransform();
    void testSanitizedAnimationFps();
    void testProjectPayloadCapsAnimationFps();
    void testProjectPayloadKeepsAnimationLoopMode();
    void testProjectPayloadDropsPathsOutsideTheProject();
    void testProjectPayloadKeepsExcludedSpritesWhenProjectMoves();
    void testProjectPayloadKeepsSpritesOfOtherAtlases();
    void testAtlasExportPayloadHoldsOnlyThatAtlas();
    void testProjectFileLoaderLoad();
    void testProjectFileLoaderLoadZip();
    void testAutosaveProjectStoreCreatesMissingParentDir();
    void testMainWindowImportPathSupport();
    void testAnimatedGifFrameExtraction();
};
