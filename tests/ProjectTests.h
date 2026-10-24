#pragma once
#include <QtTest>

class ProjectTests : public QObject {
    Q_OBJECT
private slots:
    void testProjectPayloadBuildStoresListSource();
    void testProjectPayloadMigratesLegacyRawTransform();
    void testProjectFileLoaderLoad();
    void testProjectFileLoaderLoadZip();
    void testAutosaveProjectStoreCreatesMissingParentDir();
    void testMainWindowImportPathSupport();
    void testAnimatedGifFrameExtraction();
};
