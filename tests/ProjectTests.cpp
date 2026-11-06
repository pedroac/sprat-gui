#include "ProjectTests.h"
#include "AnimatedImageImport.h"
#include "AutosaveProjectStore.h"
#include "ImportPathSupport.h"
#include "ProjectFileLoader.h"
#include "ProjectPayloadCodec.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <limits>

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

void ProjectTests::testProjectPayloadMigratesLegacyRawTransform() {
    auto payloadWithRawTransform = [](int schemaVersion) {
        QJsonObject root;
        root["schema_version"] = schemaVersion;
        root["save_options"] = QJsonObject{{"transform", "raw"}};
        root["export_presets"] = QJsonArray{QJsonObject{{"name", "preset"}, {"transform", "raw"}}};
        root["atlases"] = QJsonArray{QJsonObject{
            {"id", "atlas"}, {"name", "atlas"},
            {"export_override", QJsonObject{{"transform", "raw"}}}}};
        return root;
    };

    auto atlasTransform = [](const ProjectPayloadApplyResult& result) {
        for (const AtlasEntry& atlas : result.atlases) {
            if (atlas.id == QLatin1String("atlas")) return atlas.exportConfig.transform;
        }
        return QStringLiteral("<missing atlas>");
    };

    // Before schema 5, "raw" was the built-in layout-text export.
    QVector<LayoutModel> layoutModels;
    const ProjectPayloadApplyResult legacy =
        ProjectPayloadCodec::applyToLayout(payloadWithRawTransform(4), "/tmp/project", layoutModels);
    QCOMPARE(legacy.saveConfig.transform, kLayoutTextTransform);
    QCOMPARE(legacy.exportPresets.size(), 1);
    QCOMPARE(legacy.exportPresets.first().transform, kLayoutTextTransform);
    QCOMPARE(atlasTransform(legacy), kLayoutTextTransform);

    // From schema 5 on, "raw" is sprat-cli's raw.jsonnet transform.
    const ProjectPayloadApplyResult current =
        ProjectPayloadCodec::applyToLayout(payloadWithRawTransform(5), "/tmp/project", layoutModels);
    QCOMPARE(current.saveConfig.transform, QString("raw"));
    QCOMPARE(current.exportPresets.first().transform, QString("raw"));
    QCOMPARE(atlasTransform(current), QString("raw"));
}

void ProjectTests::testSanitizedAnimationFps() {
    QCOMPARE(sanitizedAnimationFps(1), 1);
    QCOMPARE(sanitizedAnimationFps(MAX_ANIMATION_FPS), MAX_ANIMATION_FPS);
    QCOMPARE(sanitizedAnimationFps(MAX_ANIMATION_FPS + 1), MAX_ANIMATION_FPS);
    QCOMPARE(sanitizedAnimationFps(std::numeric_limits<int>::max()), MAX_ANIMATION_FPS);
    QCOMPARE(sanitizedAnimationFps(0), DEFAULT_ANIMATION_FPS);
    QCOMPARE(sanitizedAnimationFps(-5), DEFAULT_ANIMATION_FPS);
    QCOMPARE(MAX_ANIMATION_FPS, 100); // GIF delays are whole hundredths of a second
}

void ProjectTests::testProjectPayloadCapsAnimationFps() {
    // A hand-edited project: speeds above the cap, or unset.
    const QJsonArray timelines{
        QJsonObject{{"name", "fast"}, {"fps", 1000}},
        QJsonObject{{"name", "max"}, {"fps", 100}},
        QJsonObject{{"name", "zero"}, {"fps", 0}},
        QJsonObject{{"name", "legacy"}},  // takes the file-wide animation_fps
    };
    auto fpsOf = [](const QVector<AnimationTimeline>& list, const QString& name) {
        for (const AnimationTimeline& t : list) {
            if (t.name == name) return t.fps;
        }
        return -1;
    };

    // Schema 3: one set of timelines.
    QJsonObject legacy;
    legacy["schema_version"] = 3;
    legacy["animations"] = QJsonObject{{"animation_fps", 500}, {"timelines", timelines}};
    QVector<LayoutModel> layoutModels;
    const ProjectPayloadApplyResult old =
        ProjectPayloadCodec::applyToLayout(legacy, "/tmp/project", layoutModels);
    QCOMPARE(fpsOf(old.timelines, "fast"), MAX_ANIMATION_FPS);
    QCOMPARE(fpsOf(old.timelines, "max"), 100);
    QCOMPARE(fpsOf(old.timelines, "zero"), DEFAULT_ANIMATION_FPS);
    QCOMPARE(fpsOf(old.timelines, "legacy"), MAX_ANIMATION_FPS);

    // Schema 4: timelines per atlas.
    QJsonObject current;
    current["schema_version"] = 4;
    current["atlases"] = QJsonArray{QJsonObject{
        {"id", "atlas"}, {"name", "atlas"}, {"timelines", timelines}}};
    const ProjectPayloadApplyResult result =
        ProjectPayloadCodec::applyToLayout(current, "/tmp/project", layoutModels);
    const AtlasEntry* atlas = nullptr;
    for (const AtlasEntry& a : result.atlases) {
        if (a.id == QLatin1String("atlas")) atlas = &a;
    }
    QVERIFY(atlas != nullptr);
    QCOMPARE(fpsOf(atlas->timelines, "fast"), MAX_ANIMATION_FPS);
    QCOMPARE(fpsOf(atlas->timelines, "max"), 100);
    QCOMPARE(fpsOf(atlas->timelines, "zero"), DEFAULT_ANIMATION_FPS);
    QCOMPARE(fpsOf(atlas->timelines, "legacy"), DEFAULT_ANIMATION_FPS);
}

void ProjectTests::testProjectPayloadKeepsAnimationLoopMode() {
    AnimationTimeline pingPong;
    pingPong.name = "run";
    pingPong.frames = {"/tmp/project/run_0.png", "/tmp/project/run_1.png"};
    pingPong.loopMode = AnimationLoopMode::PingPong;
    AnimationTimeline looping;
    looping.name = "idle";
    looping.frames = {"/tmp/project/idle_0.png"};

    AtlasEntry atlas;
    atlas.id = "atlas";
    atlas.name = "atlas";
    atlas.timelines = {pingPong, looping};
    ProjectPayloadBuildInput input;
    input.currentFolder = "/tmp/project";
    input.atlases = {atlas};
    input.timelines = atlas.timelines;

    const QJsonObject payload = ProjectPayloadCodec::build(input);
    const QJsonArray saved = payload["animations"].toObject()["timelines"].toArray();
    QCOMPARE(saved.size(), 2);
    QCOMPARE(saved[0].toObject()["loop"].toString(), QString("pingpong"));
    // The default is left out, so the file reads the same in older versions.
    QVERIFY(!saved[1].toObject().contains("loop"));

    QVector<LayoutModel> layoutModels;
    const ProjectPayloadApplyResult loaded =
        ProjectPayloadCodec::applyToLayout(payload, "/tmp/project", layoutModels);
    QCOMPARE(loaded.timelines.size(), 2);
    QCOMPARE(loaded.timelines[0].loopMode, AnimationLoopMode::PingPong);
    QCOMPARE(loaded.timelines[1].loopMode, AnimationLoopMode::Loop);
    const AtlasEntry* loadedAtlas = nullptr;
    for (const AtlasEntry& a : loaded.atlases) {
        if (a.id == QLatin1String("atlas")) loadedAtlas = &a;
    }
    QVERIFY(loadedAtlas != nullptr);
    QCOMPARE(loadedAtlas->timelines.size(), 2);
    QCOMPARE(loadedAtlas->timelines[0].loopMode, AnimationLoopMode::PingPong);

    // An unknown mode in a hand-edited file loads as the default.
    QJsonObject edited;
    edited["schema_version"] = 3;
    edited["animations"] = QJsonObject{{"timelines", QJsonArray{
        QJsonObject{{"name", "spin"}, {"loop", "bounce"}}}}};
    const ProjectPayloadApplyResult editedResult =
        ProjectPayloadCodec::applyToLayout(edited, "/tmp/project", layoutModels);
    QCOMPARE(editedResult.timelines.size(), 1);
    QCOMPARE(editedResult.timelines[0].loopMode, AnimationLoopMode::Loop);
}

namespace {
SpritePtr spriteWithMarker(const QString& path, const QString& markerName, int x, int y) {
    auto sprite = std::make_shared<Sprite>();
    sprite->path = path;
    sprite->name = QFileInfo(path).completeBaseName();
    sprite->pivotX = 3;
    sprite->pivotY = 4;
    NamedPoint marker;
    marker.name = markerName;
    marker.x = x;
    marker.y = y;
    sprite->points.append(marker);
    return sprite;
}
}  // namespace

void ProjectTests::testProjectPayloadKeepsSpritesOfOtherAtlases() {
    // "gem" belongs to an atlas that isn't laid out: only its stored state knows its marker.
    const QString folder = "/tmp/project";
    const SpritePtr gem = spriteWithMarker(folder + "/b/gem.png", "shine", 1, 2);
    const SpritePtr staleHero = spriteWithMarker(folder + "/a/hero.png", "hand", 0, 0);
    const SpritePtr hero = spriteWithMarker(folder + "/a/hero.png", "hand", 7, 8);
    ProjectPayloadBuildInput input;
    input.currentFolder = folder;
    input.storedSpriteStates = QJsonObject{
        {"b/gem.png", ProjectPayloadCodec::spriteStateToJson(*gem)},
        {"a/hero.png", ProjectPayloadCodec::spriteStateToJson(*staleHero)}};
    LayoutModel model;
    model.sprites = {hero};
    input.layoutModels = {model};

    const QJsonObject payload = ProjectPayloadCodec::build(input);
    const QJsonObject states = payload["spritemarkers"].toObject()["sprites"].toObject();
    QCOMPARE(states.size(), 2);

    // The laid-out sprite wins over its stored copy.
    auto loadedHero = std::make_shared<Sprite>();
    loadedHero->path = hero->path;
    const auto heroState = ProjectPayloadCodec::findSpriteState(states, folder, hero->path);
    QVERIFY(heroState.has_value());
    ProjectPayloadCodec::applySpriteState(*loadedHero, *heroState);
    QCOMPARE(loadedHero->points.size(), 1);
    QCOMPARE(loadedHero->points[0].x, 7);

    // The other atlas's sprite gets its marker and pivot back when it is laid out.
    auto loadedGem = std::make_shared<Sprite>();
    loadedGem->path = gem->path;
    const auto gemState = ProjectPayloadCodec::findSpriteState(states, folder, gem->path);
    QVERIFY(gemState.has_value());
    ProjectPayloadCodec::applySpriteState(*loadedGem, *gemState);
    QCOMPARE(loadedGem->name, QString("gem"));
    QCOMPARE(loadedGem->pivotX, 3);
    QCOMPARE(loadedGem->pivotY, 4);
    QCOMPARE(loadedGem->points.size(), 1);
    QCOMPARE(loadedGem->points[0].name, QString("shine"));
    QCOMPARE(loadedGem->points[0].y, 2);

    // Older projects keyed sprites by file name.
    const QJsonObject byFileName{{"gem.png", ProjectPayloadCodec::spriteStateToJson(*gem)}};
    QVERIFY(ProjectPayloadCodec::findSpriteState(byFileName, folder, gem->path).has_value());
    QVERIFY(!ProjectPayloadCodec::findSpriteState(byFileName, folder, hero->path).has_value());
}

void ProjectTests::testAtlasExportPayloadHoldsOnlyThatAtlas() {
    const QString folder = "/tmp/project";
    const SpritePtr hero = spriteWithMarker(folder + "/a/hero.png", "hand", 7, 8);
    const SpritePtr gem = spriteWithMarker(folder + "/b/gem.png", "shine", 1, 2);
    AnimationTimeline walk;
    walk.name = "walk";
    walk.fps = 12;
    walk.frames = {hero->path};
    AnimationTimeline sparkle;
    sparkle.name = "sparkle";
    sparkle.fps = 6;
    sparkle.frames = {gem->path};

    ProjectPayloadBuildInput input;
    input.currentFolder = folder;
    LayoutModel model;
    model.sprites = {hero, gem};
    input.layoutModels = {model};
    input.timelines = {walk};  // the active atlas's
    const QJsonObject payload = ProjectPayloadCodec::build(input);

    const QJsonObject gemAtlas =
        ProjectPayloadCodec::atlasExportPayload(payload, {gem->path}, {sparkle}, folder);
    const QJsonObject states = gemAtlas["spritemarkers"].toObject()["sprites"].toObject();
    QCOMPARE(states.keys(), QStringList{"b/gem.png"});
    const QJsonObject animations = gemAtlas["animations"].toObject();
    const QJsonArray timelines = animations["timelines"].toArray();
    QCOMPARE(timelines.size(), 1);
    QCOMPARE(timelines[0].toObject()["name"].toString(), QString("sparkle"));
    QCOMPARE(animations["animation_fps"].toInt(), 6);
    // Everything else is the project's.
    QCOMPARE(gemAtlas["layout"], payload["layout"]);

    const QJsonObject empty = ProjectPayloadCodec::atlasExportPayload(payload, {}, {}, folder);
    QVERIFY(empty["spritemarkers"].toObject()["sprites"].toObject().isEmpty());
    QVERIFY(empty["animations"].toObject()["timelines"].toArray().isEmpty());
    QVERIFY(!empty["animations"].toObject().contains("animation_fps"));
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

void ProjectTests::testMainWindowImportPathSupport() {
    QVERIFY(ImportPathSupport::isSupportedLocalImportPath("/tmp/sprites/archive.tar.gz"));
    QVERIFY(ImportPathSupport::isSupportedLocalImportPath("/tmp/sprites/archive.tar.bz2"));
    QVERIFY(ImportPathSupport::isSupportedLocalImportPath("/tmp/sprites/project archive.zip"));
    QVERIFY(ImportPathSupport::isSupportedLocalImportPath("/tmp/sprites/sheet.png"));
    QVERIFY(!ImportPathSupport::isSupportedLocalImportPath("/tmp/sprites/readme.txt"));

    QVERIFY(ImportPathSupport::isSupportedRemoteImportUrl(QUrl("https://example.com/sheet.png")));
    QVERIFY(ImportPathSupport::isSupportedRemoteImportUrl(QUrl("https://example.com/archive.tar.gz")));
    QVERIFY(!ImportPathSupport::isSupportedRemoteImportUrl(QUrl("ftp://example.com/sheet.png")));
}

void ProjectTests::testAnimatedGifFrameExtraction() {
    static const QByteArray animatedGif = QByteArray::fromBase64(
        "R0lGODlhAgACAPAAAP8AAP///yH/C05FVFNDQVBFMi4wAwEAAAAh+QQACgAAACwAAAAAAgACAAACAoRRACH5BAAKAAAALAAAAAACAAIAgAAA/////wIChFEAOw==");

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString gifPath = QDir(tempDir.path()).filePath("animated.gif");
    QFile gifFile(gifPath);
    QVERIFY(gifFile.open(QIODevice::WriteOnly));
    QVERIFY(gifFile.write(animatedGif) == animatedGif.size());
    gifFile.close();

    QVERIFY(AnimatedImageImport::isAnimatedGif(gifPath));

    const QString outputDir = QDir(tempDir.path()).filePath("frames");
    QString error;
    QVERIFY2(AnimatedImageImport::extractAnimatedFrames(gifPath, outputDir, &error), qPrintable(error));

    QDir framesDir(outputDir);
    const QStringList frameFiles = framesDir.entryList(QStringList() << "*.png", QDir::Files, QDir::Name);
    QCOMPARE(frameFiles.size(), 2);
    QVERIFY(QFile::exists(framesDir.filePath("frame_0000.png")));
    QVERIFY(QFile::exists(framesDir.filePath("frame_0001.png")));
}

void ProjectTests::testProjectPayloadKeepsExcludedSpritesWhenProjectMoves() {
    // Excluded files are relative to the source's cached folder, so that folder is saved
    // relative to the project and resolved against wherever the project is opened from.
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString savedDir = root.filePath("Saved Project");
    const QString movedDir = root.filePath("Moved Project ñ");
    QVERIFY(QDir().mkpath(QDir(savedDir).filePath("sprites/hero")));
    QVERIFY(QDir().mkpath(QDir(movedDir).filePath("sprites/hero")));

    ProjectSource source;
    source.name = "hero.zip";
    source.type = SourceType::Archive;
    source.originalPath = "/art/hero.zip";
    source.cachedFolderPath = QDir(savedDir).filePath("sprites/hero");
    source.excludedFiles = QStringList{"run/run_3.png"};

    ProjectPayloadBuildInput input;
    input.projectDir = savedDir;
    input.currentFolder = QDir(savedDir).filePath("sprites/hero");
    input.sources = {source};
    input.portablePaths = true;
    const QJsonObject payload = ProjectPayloadCodec::build(input);

    const QJsonObject savedSource =
        payload["layout"].toObject()["sources"].toArray().first().toObject();
    QCOMPARE(savedSource["cachedFolder"].toString(), QString("sprites/hero"));

    // The app opens a project with currentFolder set to the sprites folder, not the project's.
    QVector<LayoutModel> layoutModels;
    const ProjectPayloadApplyResult result = ProjectPayloadCodec::applyToLayout(
        payload, QDir(movedDir).filePath("sprites/hero"), layoutModels, movedDir);

    QCOMPARE(result.sources.size(), 1);
    QCOMPARE(result.sources[0].cachedFolderPath, QDir(movedDir).filePath("sprites/hero"));
    QCOMPARE(result.sources[0].excludedFiles, QStringList{"run/run_3.png"});
    QCOMPARE(result.sources[0].originalPath, QString("/art/hero.zip"));
}

void ProjectTests::testProjectPayloadDropsPathsOutsideTheProject() {
    // Names and paths from a project file end up as folders the app writes or deletes,
    // so values pointing outside the project are dropped on load.
    QTemporaryDir projectDir;
    QVERIFY(projectDir.isValid());
    const QString inside = projectDir.filePath("sprites/cache");
    QVERIFY(QDir().mkpath(inside));

    QJsonObject root;
    root["schema_version"] = 5;
    root["save_options"] = QJsonObject{{"profiles", QJsonArray{"desktop", "/home/user", ".."}}};
    root["export_presets"] = QJsonArray{QJsonObject{
        {"name", "preset"}, {"profiles", QJsonArray{"mobile", "../x"}}}};
    root["atlases"] = QJsonArray{
        QJsonObject{{"id", "safe"}, {"name", "safe"}, {"output_subdir", "ui/menus"},
                    {"export_override", QJsonObject{{"profiles", QJsonArray{"desktop", "a/b"}}}}},
        QJsonObject{{"id", "absolute"}, {"name", "absolute"}, {"output_subdir", "/home/user"}},
        QJsonObject{{"id", "dotdot"}, {"name", "dotdot"}, {"output_subdir", "../.."}},
    };
    root["layout"] = QJsonObject{{"sources", QJsonArray{
        QJsonObject{{"name", "kept"}, {"type", "archive"}, {"cachedFolder", inside},
                    {"originalPath", "sprites.zip"}},
        QJsonObject{{"name", "outside"}, {"type", "archive"}, {"cachedFolder", "/home/user/Documents"},
                    {"originalPath", "../../escape.zip"}},
    }}};

    QVector<LayoutModel> layoutModels;
    const ProjectPayloadApplyResult result =
        ProjectPayloadCodec::applyToLayout(root, projectDir.path(), layoutModels);

    QCOMPARE(result.saveConfig.profiles, QStringList{"desktop"});
    QCOMPARE(result.exportPresets.size(), 1);
    QCOMPARE(result.exportPresets.first().profiles, QStringList{"mobile"});

    QHash<QString, AtlasEntry> atlases;
    for (const AtlasEntry& atlas : result.atlases) atlases.insert(atlas.id, atlas);
    QCOMPARE(atlases.value("safe").outputSubdir, QString("ui/menus"));
    QCOMPARE(atlases.value("safe").exportConfig.profiles, QStringList{"desktop"});
    QVERIFY(atlases.value("absolute").outputSubdir.isEmpty());
    QVERIFY(atlases.value("dotdot").outputSubdir.isEmpty());

    QCOMPARE(result.sources.size(), 2);
    QCOMPARE(result.sources[0].cachedFolderPath, inside);
    QVERIFY(result.sources[0].originalPath.endsWith("sprites.zip"));
    QVERIFY(result.sources[1].cachedFolderPath.isEmpty());
    QVERIFY(result.sources[1].originalPath.isEmpty());
}
