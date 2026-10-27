/// @file ExportPipelineTests.cpp
/// End-to-end export tests: generate sprites, export them with ProjectSaveService through the
/// real sprat-cli tools, then check the files written. Paths contain spaces and non-ASCII
/// characters on purpose, since those are what differ between operating systems.

#include "ExportPipelineTests.h"

#include "ArchiveExtractor.h"
#include "LayoutParser.h"
#include "ProjectSaveService.h"
#ifdef SPRAT_EMBEDDED_CLI
#include "EmbeddedCli.h"
#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QSet>
#include <QtTest>

namespace {

constexpr int TOOL_TIMEOUT_MS = 120000;
const QString PROFILE_NAME = QStringLiteral("e2e");

/// Writes a PNG filled with @p color inside @p opaqueRect and transparent elsewhere.
void writeSprite(const QString& path, const QSize& size, const QColor& color,
                 const QRect& opaqueRect = QRect()) {
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.fillRect(opaqueRect.isNull() ? image.rect() : opaqueRect, color);
    painter.end();
    QVERIFY2(image.save(path), qPrintable(QStringLiteral("Could not write ") + path));
}

/// Wide bars and tall poles that only fit a 64x64 atlas when some are rotated.
void writeRotationSprites(const QString& dir) {
    QVERIFY(QDir().mkpath(dir));
    for (int i = 0; i < 3; ++i)
        writeSprite(QDir(dir).filePath(QStringLiteral("bar_%1.png").arg(i)), {64, 8}, Qt::red);
    for (int i = 0; i < 2; ++i)
        writeSprite(QDir(dir).filePath(QStringLiteral("pole_%1.png").arg(i)), {8, 64}, Qt::blue);
}

/// Runs a tool as a separate process, as MainWindow::runTool does in non-embedded builds.
bool runProcess(const QString& tool, const QStringList& args, const QByteArray* input,
                QByteArray* output, QByteArray* errorOutput) {
    QProcess process;
    process.start(tool, args);
    if (!process.waitForStarted()) {
        if (errorOutput) *errorOutput = process.errorString().toUtf8();
        return false;
    }
    if (input) process.write(*input);
    process.closeWriteChannel();
    const bool finished = process.waitForFinished(TOOL_TIMEOUT_MS);
    if (output) *output = process.readAllStandardOutput();
    if (errorOutput) *errorOutput = process.readAllStandardError();
    return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

SpratProfile makeProfile() {
    SpratProfile profile;
    profile.name = PROFILE_NAME;
    profile.preset = QStringLiteral("quality");
    profile.padding = 1;
    return profile;
}

QString readTextFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QString describe(const QString& error, const QVector<ExportLogEntry>& log) {
    QStringList lines{error};
    for (const ExportLogEntry& entry : log)
        lines << entry.path;
    return lines.join(QLatin1Char('\n'));
}

} // namespace

void ExportPipelineTests::initTestCase() {
    QVERIFY(m_root.isValid());
#ifndef SPRAT_EMBEDDED_CLI
    m_cliDir = qEnvironmentVariable("SPRAT_CLI_DIR");
    if (m_cliDir.isEmpty())
        QSKIP("SPRAT_CLI_DIR is not set; run these tests through ctest.");
    QVERIFY2(QFileInfo::exists(toolPath(QStringLiteral("spratlayout"))),
             qPrintable(QStringLiteral("spratlayout not found in ") + m_cliDir));
#endif

    m_spritesDir = m_root.filePath(QStringLiteral("sprites"));
    writeRotationSprites(m_spritesDir);
    for (int i = 0; i < 2; ++i) {
        // Transparent border around an opaque centre, so trimming has work to do.
        writeSprite(QDir(m_spritesDir).filePath(QStringLiteral("walk_%1.png").arg(i)),
                    {32, 32}, Qt::green, QRect(8, 8, 16, 16));
    }
}

QString ExportPipelineTests::toolPath(const QString& toolName) const {
#ifdef SPRAT_EMBEDDED_CLI
    return toolName;  // ProjectSaveService runs sprat-* tools in-process.
#else
#ifdef Q_OS_WIN
    return QDir(m_cliDir).filePath(toolName + QStringLiteral(".exe"));
#else
    return QDir(m_cliDir).filePath(toolName);
#endif
#endif
}

ExportPipelineTests::ExportRun ExportPipelineTests::exportSprites(
    const QString& spritesDir, const QString& destination, const QString& transform,
    const SpratProfile& profile, const QJsonObject& projectPayload) const {
    SaveConfig config;
    config.destination = destination;
    config.transform = transform;
    config.profiles = {profile.name};

    ExportRun run;
    ProjectSaveService::SaveCallbacks callbacks;
    callbacks.runProcess = runProcess;
    callbacks.logEntry = [&run](const ExportLogEntry& entry) { run.log.append(entry); };

    QString savedDestination;
    run.ok = ProjectSaveService::save(
        config, spritesDir, {}, spritesDir, {profile}, profile.name,
        toolPath(QStringLiteral("spratlayout")), toolPath(QStringLiteral("spratpack")),
        toolPath(QStringLiteral("spratconvert")), projectPayload, savedDestination, run.error,
        QStringLiteral("none"), 5, callbacks);
    return run;
}

void ExportPipelineTests::testLayoutOutputParsesInGui() {
    const QStringList args{m_spritesDir, QStringLiteral("--trim-transparent"),
                           QStringLiteral("--padding"), QStringLiteral("1")};
    QByteArray output;
    QByteArray errorOutput;
#ifdef SPRAT_EMBEDDED_CLI
    const CliResult result = EmbeddedCli::run(QStringLiteral("spratlayout"), args, {});
    const bool ok = result.exitCode == 0;
    output = result.stdOut;
    errorOutput = result.stdErr;
#else
    const bool ok = runProcess(toolPath(QStringLiteral("spratlayout")), args, nullptr,
                               &output, &errorOutput);
#endif
    QVERIFY2(ok, errorOutput.constData());

    const QVector<LayoutModel> models =
        LayoutParser::parse(QString::fromUtf8(output), m_spritesDir, m_spritesDir);
    QVERIFY(!models.isEmpty());

    QSet<QString> names;
    for (const LayoutModel& model : models) {
        for (const SpritePtr& sprite : model.sprites) {
            QVERIFY2(QFileInfo::exists(sprite->path), qPrintable(sprite->path));
            names.insert(sprite->name);
            if (sprite->name.startsWith(QStringLiteral("walk_")))
                QVERIFY2(sprite->trimmed, qPrintable(sprite->name));
        }
    }
    const QSet<QString> expected{"bar_0", "bar_1", "bar_2", "pole_0", "pole_1", "walk_0", "walk_1"};
    QCOMPARE(names, expected);
}

void ExportPipelineTests::testJsonExportWritesAtlasAndMetadata() {
    const QString destination = m_root.filePath(QStringLiteral("json export"));
    const ExportRun run = exportSprites(m_spritesDir, destination, QStringLiteral("json"),
                                        makeProfile());
    QVERIFY2(run.ok, qPrintable(describe(run.error, run.log)));

    const QDir profileDir(QDir(destination).filePath(PROFILE_NAME));
    QVERIFY(!QImage(profileDir.filePath(QStringLiteral("spritesheet.png"))).isNull());

    const QStringList metadataFiles = profileDir.entryList({QStringLiteral("*.json")}, QDir::Files);
    QVERIFY2(!metadataFiles.isEmpty(), qPrintable(profileDir.entryList().join(", ")));
    const QString metadata = readTextFile(profileDir.filePath(metadataFiles.first()));
    QJsonParseError parseError;
    QJsonDocument::fromJson(metadata.toUtf8(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    for (const QString& name : {"bar_0", "pole_1", "walk_0"})
        QVERIFY2(metadata.contains(name), qPrintable(name));
}

void ExportPipelineTests::testLibGdxExportWithRotatedSprites() {
    const QString spritesDir = m_root.filePath(QStringLiteral("rotation sprites"));
    writeRotationSprites(spritesDir);
    SpratProfile profile = makeProfile();
    profile.padding = 0;
    profile.allowRotation = true;
    profile.maxWidth = 64;
    profile.maxHeight = 64;

    const QString destination = m_root.filePath(QStringLiteral("libgdx export"));
    const ExportRun run = exportSprites(spritesDir, destination, QStringLiteral("libgdx"), profile);
    QVERIFY2(run.ok, qPrintable(describe(run.error, run.log)));

    const QDir profileDir(QDir(destination).filePath(PROFILE_NAME));
    const QStringList atlasFiles = profileDir.entryList({QStringLiteral("*.atlas")}, QDir::Files);
    QVERIFY2(!atlasFiles.isEmpty(), qPrintable(profileDir.entryList().join(", ")));
    // The test only covers the rotation fix if the packer actually rotated something.
    QVERIFY(readTextFile(profileDir.filePath(atlasFiles.first())).contains(QStringLiteral("rotate: true")));
}

void ExportPipelineTests::testZopfliProfileExportsPng() {
    SpratProfile profile = makeProfile();
    profile.zopfli = true;

    const QString destination = m_root.filePath(QStringLiteral("zopfli export"));
    const ExportRun run = exportSprites(m_spritesDir, destination, QStringLiteral("json"), profile);
    // Builds without Zopfli fall back to a regular PNG; either way the export must succeed.
    QVERIFY2(run.ok, qPrintable(describe(run.error, run.log)));
    QVERIFY(!QImage(QDir(destination).filePath(PROFILE_NAME + QStringLiteral("/spritesheet.png"))).isNull());
}

void ExportPipelineTests::testMultipackExportWritesEveryAtlas() {
    const QString spritesDir = m_root.filePath(QStringLiteral("multipack sprites"));
    QVERIFY(QDir().mkpath(spritesDir));
    constexpr int SPRITE_COUNT = 4;
    for (int i = 0; i < SPRITE_COUNT; ++i)
        writeSprite(QDir(spritesDir).filePath(QStringLiteral("tile_%1.png").arg(i)), {48, 48}, Qt::magenta);
    SpratProfile profile = makeProfile();
    profile.multipack = true;
    profile.maxWidth = 64;  // One 48x48 sprite per atlas.
    profile.maxHeight = 64;

    const QString destination = m_root.filePath(QStringLiteral("multipack export"));
    const ExportRun run = exportSprites(spritesDir, destination, QStringLiteral("json"), profile);
    QVERIFY2(run.ok, qPrintable(describe(run.error, run.log)));

    const QString tarPath = QDir(destination).filePath(PROFILE_NAME + QStringLiteral("/spritesheet.tar"));
    QString error;
    const QStringList entries = ArchiveExtractor::listEntries(tarPath, error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(entries.size(), SPRITE_COUNT);
    for (int i = 0; i < SPRITE_COUNT; ++i)
        QVERIFY2(entries.contains(QStringLiteral("atlas_%1.png").arg(i)), qPrintable(entries.join(", ")));
}

void ExportPipelineTests::testZipDestinationContainsProfileFolder() {
    const QString zipPath = m_root.filePath(QStringLiteral("zip export/project.zip"));
    QVERIFY(QDir().mkpath(QFileInfo(zipPath).path()));
    const ExportRun run = exportSprites(m_spritesDir, zipPath, QStringLiteral("json"), makeProfile());
    QVERIFY2(run.ok, qPrintable(describe(run.error, run.log)));

    QString error;
    const QStringList entries = ArchiveExtractor::listEntries(zipPath, error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QString atlasEntry = PROFILE_NAME + QStringLiteral("/spritesheet.png");
    const bool hasAtlas = std::any_of(entries.cbegin(), entries.cend(), [&](const QString& entry) {
        return entry.endsWith(atlasEntry);
    });
    QVERIFY2(hasAtlas, qPrintable(entries.join(", ")));
}

void ExportPipelineTests::testUnicodePaths() {
    const QString spritesDir = m_root.filePath(QStringLiteral("spritës ü 雪"));
    QVERIFY(QDir().mkpath(spritesDir));
    for (const QString& fileName : QDir(m_spritesDir).entryList({QStringLiteral("*.png")}, QDir::Files))
        QVERIFY(QFile::copy(QDir(m_spritesDir).filePath(fileName), QDir(spritesDir).filePath(fileName)));

    const QString destination = m_root.filePath(QStringLiteral("saída ü 雪"));
    const ExportRun run = exportSprites(spritesDir, destination, QStringLiteral("json"), makeProfile());
    QVERIFY2(run.ok, qPrintable(describe(run.error, run.log)));

    const QDir profileDir(QDir(destination).filePath(PROFILE_NAME));
    QVERIFY(!QImage(profileDir.filePath(QStringLiteral("spritesheet.png"))).isNull());
    const QStringList metadataFiles = profileDir.entryList({QStringLiteral("*.json")}, QDir::Files);
    QVERIFY(!metadataFiles.isEmpty());
    QVERIFY(readTextFile(profileDir.filePath(metadataFiles.first())).contains(QStringLiteral("walk_1")));
}

void ExportPipelineTests::testNineSliceInsetsAreExportedAndFitted() {
    const QString spritesDir = m_root.filePath(QStringLiteral("nine slice sprites"));
    QVERIFY(QDir().mkpath(spritesDir));
    writeSprite(QDir(spritesDir).filePath(QStringLiteral("panel.png")), {32, 32}, Qt::gray);
    // Trimmed to its 16x16 centre: its insets must still fit the full 32x32 image.
    writeSprite(QDir(spritesDir).filePath(QStringLiteral("frame.png")), {32, 32}, Qt::gray,
                QRect(8, 8, 16, 16));

    // Insets that don't fit, as projects saved before the editor kept them fitted may hold.
    auto slice = [](int left, int top, int right, int bottom) {
        return QJsonObject{{"is_nine_sliced", true}, {"ns_left", left}, {"ns_top", top},
                           {"ns_right", right}, {"ns_bottom", bottom},
                           {"ns_h_mode", "stretch"}, {"ns_v_mode", "repeat"}};
    };
    const QJsonObject sprites{{"panel.png", slice(20, 8, 20, 8)}, {"frame.png", slice(4, 4, 30, 30)}};
    const QJsonObject payload{{"spritemarkers", QJsonObject{{"sprites", sprites}}}};

    const QString destination = m_root.filePath(QStringLiteral("nine slice export"));
    const ExportRun run = exportSprites(spritesDir, destination, QStringLiteral("json"),
                                        makeProfile(), payload);
    QVERIFY2(run.ok, qPrintable(describe(run.error, run.log)));

    const QDir profileDir(QDir(destination).filePath(PROFILE_NAME));
    const QStringList metadataFiles = profileDir.entryList({QStringLiteral("*.json")}, QDir::Files);
    QVERIFY2(!metadataFiles.isEmpty(), qPrintable(profileDir.entryList().join(", ")));
    const QJsonDocument document =
        QJsonDocument::fromJson(readTextFile(profileDir.filePath(metadataFiles.first())).toUtf8());

    // Slices keyed by file name without extension.
    QHash<QString, QJsonObject> slices;
    for (const QJsonValue& value : document.object()["sprites"].toArray()) {
        const QJsonObject sprite = value.toObject();
        if (sprite["has_slice"].toBool())
            slices.insert(QFileInfo(sprite["path"].toString()).completeBaseName(),
                          sprite["slice"].toObject());
    }
    QCOMPARE(slices.size(), 2);
    auto insets = [](const QJsonObject& s) {
        return QList<int>{s["left"].toInt(), s["top"].toInt(), s["right"].toInt(), s["bottom"].toInt()};
    };
    QCOMPARE(insets(slices.value(QStringLiteral("panel"))), (QList<int>{20, 8, 12, 8}));
    QCOMPARE(insets(slices.value(QStringLiteral("frame"))), (QList<int>{4, 4, 28, 28}));
    QCOMPARE(slices.value(QStringLiteral("panel"))["v_mode"].toString(), QStringLiteral("repeat"));
}

void ExportPipelineTests::testProjectTextCantInjectCliRecords() {
    const QString spritesDir = m_root.filePath(QStringLiteral("injection sprites"));
    QVERIFY(QDir().mkpath(spritesDir));
    writeSprite(QDir(spritesDir).filePath(QStringLiteral("panel.png")), {32, 32}, Qt::gray);

    // A shared project file may hold any string; none of these may start a record of its own.
    const QString injected = QStringLiteral("\nsprite \"/etc/passwd\" 0,0 1,1\n");
    const QJsonArray markers{
        QJsonObject{{"name", QStringLiteral("say \"hi\"") + injected}, {"kind", "point"},
                    {"x", 3}, {"y", 4}},
        QJsonObject{{"name", "bogus"}, {"kind", QStringLiteral("point 1,1") + injected}},
    };
    const QJsonObject panel{{"is_nine_sliced", true}, {"ns_left", 4}, {"ns_top", 4},
                            {"ns_right", 4}, {"ns_bottom", 4},
                            {"ns_h_mode", QStringLiteral("stretch") + injected},
                            {"ns_v_mode", "mirror"}, {"markers", markers}};
    const QJsonObject timeline{{"name", QStringLiteral("idle") + injected}, {"fps", 6},
                               {"frames", QJsonArray{"panel.png"}}};
    const QJsonObject payload{
        {"spritemarkers", QJsonObject{{"sprites", QJsonObject{{"panel.png", panel}}}}},
        {"animations", QJsonObject{{"timelines", QJsonArray{timeline}}}}};

    const QString destination = m_root.filePath(QStringLiteral("injection export"));
    const ExportRun run = exportSprites(spritesDir, destination, QStringLiteral("json"),
                                        makeProfile(), payload);
    QVERIFY2(run.ok, qPrintable(describe(run.error, run.log)));

    const QDir profileDir(QDir(destination).filePath(PROFILE_NAME));
    const QStringList metadataFiles = profileDir.entryList({QStringLiteral("*.json")}, QDir::Files);
    QVERIFY2(!metadataFiles.isEmpty(), qPrintable(profileDir.entryList().join(", ")));
    const QJsonObject document =
        QJsonDocument::fromJson(readTextFile(profileDir.filePath(metadataFiles.first())).toUtf8())
            .object();

    const QJsonArray sprites = document["sprites"].toArray();
    QCOMPARE(sprites.size(), 1);
    const QJsonObject sprite = sprites.first().toObject();
    QVERIFY(!sprite["path"].toString().contains(QStringLiteral("passwd")));

    const QJsonObject slice = sprite["slice"].toObject();
    QCOMPARE(slice["h_mode"].toString(), QStringLiteral("stretch"));
    QCOMPARE(slice["v_mode"].toString(), QStringLiteral("mirror"));

    // The quoted name survives intact on one line; the unknown kind is dropped.
    QStringList markerNames;
    for (const QJsonValue& value : sprite["markers"].toArray())
        markerNames << value.toObject()["name"].toString();
    QVERIFY2(markerNames.contains(QStringLiteral("say \"hi\" sprite \"/etc/passwd\" 0,0 1,1")),
             qPrintable(markerNames.join(QStringLiteral(" | "))));
    QVERIFY(!markerNames.contains(QStringLiteral("bogus")));

    const QJsonArray animations = document["animations"].toArray();
    QCOMPARE(animations.size(), 1);
    QVERIFY(animations.first().toObject()["name"].toString().startsWith(QStringLiteral("idle ")));
}

void ExportPipelineTests::testAnimationLoopModesAreExported() {
    const QString spritesDir = m_root.filePath(QStringLiteral("loop sprites"));
    QVERIFY(QDir().mkpath(spritesDir));
    writeSprite(QDir(spritesDir).filePath(QStringLiteral("a.png")), {16, 16}, Qt::red);
    writeSprite(QDir(spritesDir).filePath(QStringLiteral("b.png")), {16, 16}, Qt::blue);

    const QJsonArray frames{"a.png", "b.png"};
    const QJsonArray timelines{
        QJsonObject{{"name", "walk"}, {"fps", 6}, {"frames", frames}},
        QJsonObject{{"name", "jump"}, {"fps", 6}, {"frames", frames}, {"loop", "once"}},
        QJsonObject{{"name", "swing"}, {"fps", 6}, {"frames", frames}, {"loop", "pingpong"}},
        QJsonObject{{"name", "wave"}, {"fps", 6}, {"frames", frames}, {"loop", "once-pingpong"}},
        // An alias plays the way its source does; its own mode is not written.
        QJsonObject{{"name", "swing_left"}, {"fps", 6}, {"alias_of", "swing"}, {"loop", "once"},
                    {"h_flip", true}},
    };
    const QJsonObject payload{{"animations", QJsonObject{{"timelines", timelines}}}};

    const QString destination = m_root.filePath(QStringLiteral("loop export"));
    const ExportRun run = exportSprites(spritesDir, destination, QStringLiteral("json"),
                                        makeProfile(), payload);
    QVERIFY2(run.ok, qPrintable(describe(run.error, run.log)));

    const QDir profileDir(QDir(destination).filePath(PROFILE_NAME));
    const QStringList metadataFiles = profileDir.entryList({QStringLiteral("*.json")}, QDir::Files);
    QVERIFY2(!metadataFiles.isEmpty(), qPrintable(profileDir.entryList().join(", ")));
    const QJsonObject document =
        QJsonDocument::fromJson(readTextFile(profileDir.filePath(metadataFiles.first())).toUtf8())
            .object();

    QHash<QString, QJsonObject> animations;
    for (const QJsonValue& value : document["animations"].toArray())
        animations.insert(value.toObject()["name"].toString(), value.toObject());
    QCOMPARE(animations.size(), 5);
    QCOMPARE(animations["walk"]["loop"].toString(), QStringLiteral("loop"));
    QCOMPARE(animations["jump"]["loop"].toString(), QStringLiteral("once"));
    QCOMPARE(animations["swing"]["loop"].toString(), QStringLiteral("pingpong"));
    QCOMPARE(animations["wave"]["loop"].toString(), QStringLiteral("once-pingpong"));
    QCOMPARE(animations["swing_left"]["alias"].toString(), QStringLiteral("swing"));
    QCOMPARE(animations["swing_left"]["flip"].toString(), QStringLiteral("h"));
}

QTEST_MAIN(ExportPipelineTests)
