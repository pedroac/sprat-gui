#include <QApplication>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <iostream>
#include <memory>

#include "AnimationPreviewService.h"
#include "ProjectPayloadCodec.h"
#include "TimelineBuilder.h"
#include "models.h"

namespace {
bool expectTrue(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

bool saveSolidImage(const QString& path, int width, int height) {
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(qRgba(255, 255, 255, 255));
    return image.save(path);
}

bool testAnimationPreviewUsesTimelineBounds() {
    QTemporaryDir tempDir;
    if (!expectTrue(tempDir.isValid(), "temporary directory should be valid")) {
        return false;
    }

    const QString currentFramePath = QDir(tempDir.path()).filePath("frame_current.png");
    const QString widerFramePath = QDir(tempDir.path()).filePath("frame_wider.png");
    if (!expectTrue(saveSolidImage(currentFramePath, 100, 100), "current frame image should be written")) {
        return false;
    }
    if (!expectTrue(saveSolidImage(widerFramePath, 300, 120), "wider frame image should be written")) {
        return false;
    }

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
    QLabel previewLabel;
    QLabel statusLabel;
    QPushButton prevButton;
    QPushButton playPauseButton;
    QPushButton nextButton;

    AnimationPreviewService::refresh(
        timelines,
        0,
        frameIndex,
        model,
        1.0,
        0,
        &previewLabel,
        &statusLabel,
        &prevButton,
        &playPauseButton,
        &nextButton,
        playing,
        &timer);

    if (!expectTrue(previewLabel.width() >= 300, "preview width should include the widest frame bounds")) {
        return false;
    }
    if (!expectTrue(previewLabel.height() >= 120, "preview height should include the tallest frame bounds")) {
        return false;
    }
    return true;
}

bool testProjectPayloadBuildStoresListSource() {
    ProjectPayloadBuildInput input;
    input.currentFolder = "/tmp/project";
    input.layoutSourceIsList = true;
    input.activeFramePaths = QStringList{
        "/tmp/project/a/frame_0.png",
        "/tmp/project/a/frame_1.png"
    };
    input.layoutOptionScale = 0.5;
    input.sourceResolutionWidth = 1920;
    input.sourceResolutionHeight = 1080;

    const QJsonObject payload = ProjectPayloadCodec::build(input);
    const QJsonObject layout = payload.value("layout").toObject();
    const QJsonArray framePaths = layout.value("frame_paths").toArray();

    if (!expectTrue(layout.value("source_mode").toString() == "list", "payload should persist list source mode")) {
        return false;
    }
    if (!expectTrue(framePaths.size() == 2, "payload should persist all frame paths")) {
        return false;
    }
    if (!expectTrue(framePaths.at(0).toString() == "/tmp/project/a/frame_0.png", "payload should preserve frame path ordering")) {
        return false;
    }
    return true;
}

bool testTimelineBuilderParsesSupportedPatterns() {
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
    if (!expectTrue(run != nullptr, "Run timeline should be generated")) {
        return false;
    }
    if (!expectTrue(run->frames == QStringList{"/tmp/run0.png", "/tmp/run1.png", "/tmp/run2.png"},
                    "Run timeline frames should be sorted by numeric suffix")) {
        return false;
    }

    const TimelineSeed* idle = findSeed("Idle");
    if (!expectTrue(idle != nullptr, "Idle timeline should be generated")) {
        return false;
    }
    if (!expectTrue(idle->frames == QStringList{"/tmp/idle1.png", "/tmp/idle3.png"},
                    "Idle timeline should support parenthesis and bracket formats")) {
        return false;
    }

    const TimelineSeed* punch = findSeed("Punch");
    if (!expectTrue(punch != nullptr, "Punch timeline should be generated from compact suffix")) {
        return false;
    }
    return true;
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    int failed = 0;
    if (!testAnimationPreviewUsesTimelineBounds()) {
        ++failed;
    }
    if (!testProjectPayloadBuildStoresListSource()) {
        ++failed;
    }
    if (!testTimelineBuilderParsesSupportedPatterns()) {
        ++failed;
    }

    if (failed > 0) {
        std::cerr << failed << " test(s) failed\n";
        return 1;
    }
    std::cout << "All tests passed\n";
    return 0;
}
