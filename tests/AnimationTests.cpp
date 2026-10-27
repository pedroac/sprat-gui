#include "AnimationTests.h"
#include "AnimationPlaybackService.h"
#include "AnimationPreviewService.h"
#include "models.h"
#include <QImage>
#include <QDir>
#include <QTemporaryDir>
#include <QTimer>

namespace {
bool saveSolidImage(const QString& path, int width, int height) {
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(qRgba(255, 255, 255, 255));
    return image.save(path);
}
}

void AnimationTests::testAnimationPreviewUsesTimelineBounds() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString currentFramePath = QDir(tempDir.path()).filePath("frame_current.png");
    const QString widerFramePath = QDir(tempDir.path()).filePath("frame_wider.png");
    QVERIFY(saveSolidImage(currentFramePath, 100, 100));
    QVERIFY(saveSolidImage(widerFramePath, 300, 120));

    LayoutModel model;
    auto currentSprite = std::make_shared<Sprite>();
    currentSprite->path = currentFramePath;
    currentSprite->pivotX = 0;
    currentSprite->pivotY = 0;
    model.sprites.append(currentSprite);

    auto widerSprite = std::make_shared<Sprite>();
    widerSprite->path = widerFramePath;
    widerSprite->pivotX = 0;
    widerSprite->pivotY = 0;
    model.sprites.append(widerSprite);

    QVector<AnimationTimeline> timelines;
    AnimationTimeline timeline;
    timeline.name = "Test";
    timeline.fps = 8;
    timeline.frames = QStringList{currentFramePath, widerFramePath};
    timelines.append(timeline);

    int frameIndex = 0;
    bool playing = false;
    QTimer timer;
    QString statusText;
    bool hasFrames = false;

    QPixmap pixmap = AnimationPreviewService::refresh(
        timelines,
        0,
        frameIndex,
        {model},
        statusText,
        hasFrames,
        playing,
        &timer);

    QVERIFY(!pixmap.isNull());
    QVERIFY(pixmap.width() >= 120);
    QVERIFY(pixmap.height() >= 120);
}

#include "TimelineGenerationService.h"

void AnimationTests::testTimelineGenerationFromLayout() {
    LayoutModel model;
    auto addSprite = [&](const QString& name, const QString& path) {
        auto sprite = std::make_shared<Sprite>();
        sprite->name = name;
        sprite->path = path;
        model.sprites.append(sprite);
    };

    addSprite("Idle_0", "/tmp/idle0.png");
    addSprite("Idle_1", "/tmp/idle1.png");

    QVector<AnimationTimeline> timelines;
    int focusIndex = -1;
    QString status;
    
    auto resolver = [](const QString&) { return TimelineGenerationService::ConflictResolution::Replace; };

    bool result = TimelineGenerationService::generateFromLayout({model}, timelines, focusIndex, resolver, status);
    
    QVERIFY(result);
    QCOMPARE(timelines.size(), 1);
    QCOMPARE(timelines[0].name, QString("Idle"));
    QCOMPARE(timelines[0].frames.size(), 2);
}

void AnimationTests::testAnimationLoopModeNames() {
    for (const auto mode : {AnimationLoopMode::Loop, AnimationLoopMode::Once,
                            AnimationLoopMode::PingPong, AnimationLoopMode::OncePingPong}) {
        QCOMPARE(animationLoopModeFromName(animationLoopModeName(mode)), std::optional(mode));
    }
    QCOMPARE(animationLoopModeName(AnimationLoopMode::OncePingPong), QString("once-pingpong"));
    QVERIFY(!animationLoopModeFromName("bounce").has_value());
    QVERIFY(!animationLoopModeFromName(QString()).has_value());
}

void AnimationTests::testAnimationPlayOrder() {
    QCOMPARE(animationPlayOrder(3, AnimationLoopMode::Loop), (QVector<int>{0, 1, 2}));
    QCOMPARE(animationPlayOrder(3, AnimationLoopMode::Once), (QVector<int>{0, 1, 2}));
    QCOMPARE(animationPlayOrder(3, AnimationLoopMode::PingPong), (QVector<int>{0, 1, 2, 1}));
    QCOMPARE(animationPlayOrder(3, AnimationLoopMode::OncePingPong), (QVector<int>{0, 1, 2, 1, 0}));
    QCOMPARE(animationPlayOrder(2, AnimationLoopMode::PingPong), (QVector<int>{0, 1}));
    QCOMPARE(animationPlayOrder(2, AnimationLoopMode::OncePingPong), (QVector<int>{0, 1, 0}));
    QCOMPARE(animationPlayOrder(1, AnimationLoopMode::PingPong), (QVector<int>{0}));
    QCOMPARE(animationPlayOrder(1, AnimationLoopMode::OncePingPong), (QVector<int>{0}));
    QVERIFY(animationPlayOrder(0, AnimationLoopMode::PingPong).isEmpty());
}

namespace {
// Plays timeline 0 one frame per tick and returns the frames shown, until it stops or after maxTicks.
QVector<int> playFrames(const QVector<AnimationTimeline>& timelines, int timelineIndex, int maxTicks,
                        bool* finishedOut = nullptr) {
    QVector<int> shown;
    int frameIndex = 0;
    int playStep = 0;
    bool finished = false;
    for (int i = 0; i < maxTicks && !finished; ++i) {
        if (!AnimationPlaybackService::tick(timelines, timelineIndex, frameIndex, playStep,
                                            /*elapsedMs=*/100, /*fps=*/10, finished)) {
            break;
        }
        shown.append(frameIndex);
    }
    if (finishedOut) *finishedOut = finished;
    return shown;
}
}

void AnimationTests::testPlaybackFollowsLoopMode() {
    AnimationTimeline timeline;
    timeline.name = "run";
    timeline.frames = {"a.png", "b.png", "c.png"};

    bool finished = false;
    timeline.loopMode = AnimationLoopMode::Loop;
    QCOMPARE(playFrames({timeline}, 0, 5, &finished), (QVector<int>{1, 2, 0, 1, 2}));
    QVERIFY(!finished);

    timeline.loopMode = AnimationLoopMode::PingPong;
    QCOMPARE(playFrames({timeline}, 0, 6, &finished), (QVector<int>{1, 2, 1, 0, 1, 2}));
    QVERIFY(!finished);

    timeline.loopMode = AnimationLoopMode::Once;
    QCOMPARE(playFrames({timeline}, 0, 10, &finished), (QVector<int>{1, 2}));
    QVERIFY(finished);

    timeline.loopMode = AnimationLoopMode::OncePingPong;
    QCOMPARE(playFrames({timeline}, 0, 10, &finished), (QVector<int>{1, 2, 1, 0}));
    QVERIFY(finished);

    // An alias plays the way its source does, whatever its own mode.
    AnimationTimeline alias;
    alias.name = "run_back";
    alias.aliasOf = "run";
    alias.loopMode = AnimationLoopMode::Loop;
    QCOMPARE(playFrames({timeline, alias}, 1, 10, &finished), (QVector<int>{1, 2, 1, 0}));
    QVERIFY(finished);

    // Ticks that cover several frames stop on the last one.
    timeline.loopMode = AnimationLoopMode::Once;
    int frameIndex = 0;
    int playStep = 0;
    QVERIFY(AnimationPlaybackService::tick({timeline}, 0, frameIndex, playStep, 1000, 10, finished));
    QCOMPARE(frameIndex, 2);
    QVERIFY(finished);
}

void AnimationTests::testPlaybackRestartsFinishedOnceAnimation() {
    AnimationTimeline timeline;
    timeline.name = "jump";
    timeline.frames = {"a.png", "b.png", "c.png"};
    timeline.loopMode = AnimationLoopMode::Once;
    QTimer timer;
    bool playing = false;

    // Stopped on the last frame: playing again starts over.
    int frameIndex = 2;
    int playStep = 2;
    QVERIFY(AnimationPlaybackService::togglePlayPause({timeline}, 0, 10, frameIndex, playStep, playing, &timer));
    QVERIFY(playing);
    QCOMPARE(frameIndex, 0);
    QCOMPARE(playStep, 0);
    QVERIFY(AnimationPlaybackService::togglePlayPause({timeline}, 0, 10, frameIndex, playStep, playing, &timer));
    QVERIFY(!playing);

    // Paused partway: playing again resumes there.
    frameIndex = 1;
    playStep = 1;
    QVERIFY(AnimationPlaybackService::togglePlayPause({timeline}, 0, 10, frameIndex, playStep, playing, &timer));
    QCOMPARE(frameIndex, 1);
    timer.stop();
    playing = false;

    // A ping-pong-once animation ends on its first frame; the start of its pass is not the end.
    timeline.loopMode = AnimationLoopMode::OncePingPong;
    frameIndex = 0;
    playStep = 4;
    QVERIFY(AnimationPlaybackService::togglePlayPause({timeline}, 0, 10, frameIndex, playStep, playing, &timer));
    QCOMPARE(playStep, 0);
    timer.stop();
    playing = false;
    playStep = 0;
    QVERIFY(AnimationPlaybackService::togglePlayPause({timeline}, 0, 10, frameIndex, playStep, playing, &timer));
    QCOMPARE(playStep, 0);
    timer.stop();
}
