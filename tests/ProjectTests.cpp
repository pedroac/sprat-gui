#include "ProjectTests.h"
#include "AutosaveProjectStore.h"
#include "ProjectFileLoader.h"
#include "ProjectPayloadCodec.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

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

void ProjectTests::testProjectFileLoaderLoadZip() {
    const QString zipBinary = QStandardPaths::findExecutable("zip");
    const QString unzipBinary = QStandardPaths::findExecutable("unzip");
    if (zipBinary.isEmpty() || unzipBinary.isEmpty()) {
        QSKIP("zip/unzip binaries are required for zip loader test.");
    }

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString extractedJsonPath = QDir(tempDir.path()).filePath("project.spart.json");
    const QString zipPath = QDir(tempDir.path()).filePath("project archive.zip");

    QJsonObject root;
    root["version"] = "1.1";
    QJsonObject layout;
    layout["source_mode"] = "folder";
    root["layout"] = layout;

    QFile jsonFile(extractedJsonPath);
    QVERIFY(jsonFile.open(QIODevice::WriteOnly));
    QVERIFY(jsonFile.write(QJsonDocument(root).toJson()) >= 0);
    jsonFile.close();

    QProcess zipProcess;
    zipProcess.setWorkingDirectory(tempDir.path());
    zipProcess.start(zipBinary, QStringList() << "-q" << zipPath << "project.spart.json");
    QVERIFY(zipProcess.waitForStarted());
    QVERIFY(zipProcess.waitForFinished(10000));
    QCOMPARE(zipProcess.exitStatus(), QProcess::NormalExit);
    QCOMPARE(zipProcess.exitCode(), 0);
    QVERIFY(QFile::exists(zipPath));

    QJsonObject loadedRoot;
    QString error;
    QVERIFY2(ProjectFileLoader::load(zipPath, loadedRoot, error), qPrintable(error));
    QCOMPARE(loadedRoot.value("version").toString(), QString("1.1"));
    QCOMPARE(loadedRoot.value("layout").toObject().value("source_mode").toString(), QString("folder"));
}

void ProjectTests::testAutosaveProjectStoreCreatesMissingParentDir() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString autosavePath = QDir(tempDir.path()).filePath("nested/sprat/gui_saved.json");

    QJsonObject root;
    root["version"] = 2;
    root["project"] = "autosave";

    QString error;
    QVERIFY2(AutosaveProjectStore::save(autosavePath, root, error), qPrintable(error));
    QVERIFY(QFile::exists(autosavePath));

    QJsonObject loadedRoot;
    QVERIFY2(AutosaveProjectStore::load(autosavePath, loadedRoot, error), qPrintable(error));
    QCOMPARE(loadedRoot.value("version").toInt(), 2);
    QCOMPARE(loadedRoot.value("project").toString(), QString("autosave"));
}
