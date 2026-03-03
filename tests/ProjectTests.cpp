#include "ProjectTests.h"
#include "ProjectPayloadCodec.h"
#include <QJsonObject>
#include <QJsonArray>

void ProjectTests::testProjectPayloadBuildStoresListSource() {
    ProjectPayloadBuildInput input;
    input.currentFolder = "/tmp/project";
    input.layoutSourceIsList = true;
    input.activeFramePaths = QStringList{
        "/tmp/project/a/frame_0.png",
        "/tmp/project/a/frame_1.png"
    };
    input.sourceResolutionWidth = 1920;
    input.sourceResolutionHeight = 1080;

    const QJsonObject payload = ProjectPayloadCodec::build(input);
    const QJsonObject layout = payload.value("layout").toObject();
    const QJsonArray framePaths = layout.value("frame_paths").toArray();

    QCOMPARE(layout.value("source_mode").toString(), QString("list"));
    QCOMPARE(framePaths.size(), 2);
    QCOMPARE(framePaths.at(0).toString(), QString("/tmp/project/a/frame_0.png"));
}

#include "ProjectFileLoader.h"
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>

void ProjectTests::testProjectFileLoaderLoad() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString path = QDir(tempDir.path()).filePath("test.sprat");

    QJsonObject root;
    root["version"] = "1.0";
    QJsonObject layout;
    layout["source_mode"] = "list";
    root["layout"] = layout;

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(root).toJson());
    file.close();

    QJsonObject loadedRoot;
    QString error;
    QVERIFY(ProjectFileLoader::load(path, loadedRoot, error));
    QCOMPARE(loadedRoot.value("version").toString(), QString("1.0"));
    QCOMPARE(loadedRoot.value("layout").toObject().value("source_mode").toString(), QString("list"));
}
