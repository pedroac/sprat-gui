#include "AnimationTests.h"
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
