#include "LayoutTests.h"
#include "LayoutArguments.h"
#include "LayoutParser.h"
#include "TimelineBuilder.h"
#include "models.h"
#include <QDir>

void LayoutTests::testLayoutParserHandlesEscapedQuotes() {
    const QString output = QString::fromLatin1(R"(atlas 100,100
scale 1.0
sprite "path/with/\"quotes\".png" 0,0 10,10
)");
    
    QVector<LayoutModel> models = LayoutParser::parse(output, "/tmp");
    QCOMPARE(models.size(), 1);
    LayoutModel model = models.first();
    QCOMPARE(model.sprites.size(), 1);
    
    QString expectedPath = QDir("/tmp").absoluteFilePath("path/with/\"quotes\".png");
    QCOMPARE(model.sprites[0]->path, expectedPath);
}

void LayoutTests::testTimelineBuilderParsesSupportedPatterns() {
    QVector<SpritePtr> sprites;

    auto addSprite = [&](const QString& name, const QString& path) {
        auto sprite = std::make_shared<Sprite>();
        sprite->name = name;
        sprite->path = path;
        sprites.append(sprite);
    };

    addSprite("Run-2", "/tmp/run2.png");
    addSprite("Run 0", "/tmp/run0.png");
    addSprite("Run_1", "/tmp/run1.png");
    addSprite("Idle [3]", "/tmp/idle3.png");
    addSprite("Idle (1)", "/tmp/idle1.png");
    addSprite("Punch4", "/tmp/punch4.png");

    const QVector<TimelineSeed> seeds = TimelineBuilder::buildFromSprites(sprites);

    auto findSeed = [&](const QString& name) -> const TimelineSeed* {
        for (const TimelineSeed& seed : seeds) {
            if (seed.name == name) {
                return &seed;
            }
        }
        return nullptr;
    };

    const TimelineSeed* run = findSeed("Run");
    QVERIFY(run != nullptr);
    QStringList expectedRun = {"/tmp/run0.png", "/tmp/run1.png", "/tmp/run2.png"};
    QCOMPARE(run->frames, expectedRun);

    const TimelineSeed* idle = findSeed("Idle");
    QVERIFY(idle != nullptr);
    QStringList expectedIdle = {"/tmp/idle1.png", "/tmp/idle3.png"};
    QCOMPARE(idle->frames, expectedIdle);

    const TimelineSeed* punch = findSeed("Punch");
    QVERIFY(punch != nullptr);
}

void LayoutTests::testLayoutArgumentsIncludeResolutionAndDedupThreshold() {
    // The export and its preview build their spratlayout arguments here too, so a
    // profile's target resolution and the perceptual threshold reach every run.
    LayoutRunConfig config;
    config.sourceFolderPath = QStringLiteral("/tmp/frames");
    config.profile.allowRotation = true;
    config.profile.targetResolutionWidth = 1280;
    config.profile.targetResolutionHeight = 720;
    config.sourceResolutionWidth = 1920;
    config.sourceResolutionHeight = 1080;
    config.rotateCounterClockwise = true;
    config.deduplicateMode = QStringLiteral("perceptual");
    config.dedupThreshold = 9;

    const QStringList args = buildLayoutArguments(config);
    QCOMPARE(args.first(), QStringLiteral("/tmp/frames"));
    auto valueOf = [&](const QString& flag) {
        const int index = args.indexOf(flag);
        return index >= 0 && index + 1 < args.size() ? args.at(index + 1) : QString();
    };
    QCOMPARE(valueOf(QStringLiteral("--source-resolution")), QStringLiteral("1920x1080"));
    QCOMPARE(valueOf(QStringLiteral("--target-resolution")), QStringLiteral("1280x720"));
    QCOMPARE(valueOf(QStringLiteral("--rotate-direction")), QStringLiteral("ccw"));
    QCOMPARE(valueOf(QStringLiteral("--deduplicate")), QStringLiteral("perceptual"));
    QCOMPARE(valueOf(QStringLiteral("--dedup-threshold")), QStringLiteral("9"));

    // Without a source resolution there is nothing to scale from.
    config.sourceResolutionWidth = 0;
    config.sourceResolutionHeight = 0;
    QVERIFY(!buildLayoutArguments(config).contains(QStringLiteral("--target-resolution")));
}
