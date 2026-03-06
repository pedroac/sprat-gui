#include "AnimationPreviewService.h"

#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QPixmapCache>
#include <QTimer>
#include <QCoreApplication>
#include <QHash>
#include <QtGlobal>

namespace {
QString trAnimationPreview(const char* text) {
    return QCoreApplication::translate("AnimationPreviewService", text);
}
}

QPixmap AnimationPreviewService::refresh(
    const QVector<AnimationTimeline>& timelines,
    int selectedTimelineIndex,
    int& frameIndex,
    const QVector<LayoutModel>& layoutModels,
    QString& statusText,
    bool& hasFrames,
    bool& playing,
    QTimer* timer) {
    
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size() || timelines[selectedTimelineIndex].frames.isEmpty()) {
        statusText = trAnimationPreview("Create/select a timeline and drag frames into it.");
        hasFrames = false;
        if (playing) {
            playing = false;
            timer->stop();
        }
        return QPixmap();
    }

    hasFrames = true;
    const auto& frames = timelines[selectedTimelineIndex].frames;
    if (frameIndex >= frames.size()) {
        frameIndex = 0;
    }

    QString path = frames[frameIndex];
    QString spriteName = QFileInfo(path).baseName();
    SpritePtr currentSprite = nullptr;
    for (const auto& model : layoutModels) {
        for (const auto& s : model.sprites) {
            if (s->path == path) {
                spriteName = s->name;
                currentSprite = s;
                break;
            }
        }
        if (currentSprite) break;
    }

    statusText = QString("%1 | frame %2/%3 | %4")
                             .arg(timelines[selectedTimelineIndex].name)
                             .arg(frameIndex + 1)
                             .arg(frames.size())
                             .arg(spriteName);

    QPixmap pix;
    if (!QPixmapCache::find(path, &pix)) {
        pix.load(path);
        QPixmapCache::insert(path, pix);
    }
    if (pix.isNull()) {
        return QPixmap();
    }

    static QHash<QString, QSize> frameSizeCache;
    if (frameSizeCache.size() > 16384) {
        frameSizeCache.clear();
    }
    int maxLeftExtent = 0;
    int maxRightExtent = 0;
    int maxTopExtent = 0;
    int maxBottomExtent = 0;

    for (const QString& framePath : frames) {
        QSize frameSize = frameSizeCache.value(framePath);
        if (!frameSize.isValid()) {
            frameSize = QImageReader(framePath).size();
            if (frameSize.isValid()) {
                frameSizeCache.insert(framePath, frameSize);
            }
        }
        if (!frameSize.isValid()) {
            continue;
        }

        int framePivotX = frameSize.width() / 2;
        int framePivotY = frameSize.height() / 2;
        for (const auto& model : layoutModels) {
            for (const auto& sprite : model.sprites) {
                if (sprite->path == framePath) {
                    framePivotX = qBound(0, sprite->pivotX, frameSize.width());
                    framePivotY = qBound(0, sprite->pivotY, frameSize.height());
                    break;
                }
            }
        }

        maxLeftExtent = qMax(maxLeftExtent, framePivotX);
        maxRightExtent = qMax(maxRightExtent, frameSize.width() - framePivotX);
        maxTopExtent = qMax(maxTopExtent, framePivotY);
        maxBottomExtent = qMax(maxBottomExtent, frameSize.height() - framePivotY);
    }

    if (maxLeftExtent <= 0 && maxRightExtent <= 0 && maxTopExtent <= 0 && maxBottomExtent <= 0) {
        int pivotX = currentSprite ? qBound(0, currentSprite->pivotX, pix.width()) : pix.width() / 2;
        int pivotY = currentSprite ? qBound(0, currentSprite->pivotY, pix.height()) : pix.height() / 2;
        maxLeftExtent = qMax(1, pivotX);
        maxRightExtent = qMax(1, pix.width() - pivotX);
        maxTopExtent = qMax(1, pivotY);
        maxBottomExtent = qMax(1, pix.height() - pivotY);
    }

    const int animationWidth = qMax(1, maxLeftExtent + maxRightExtent);
    const int animationHeight = qMax(1, maxTopExtent + maxBottomExtent);

    QPixmap canvas(animationWidth, animationHeight);
    canvas.fill(Qt::transparent);

    QPainter p(&canvas);
    int pivotX = currentSprite ? qBound(0, currentSprite->pivotX, pix.width()) : pix.width() / 2;
    int pivotY = currentSprite ? qBound(0, currentSprite->pivotY, pix.height()) : pix.height() / 2;
    const int anchorX = maxLeftExtent;
    const int anchorY = maxTopExtent;
    int destX = anchorX - pivotX;
    int destY = anchorY - pivotY;
    p.drawPixmap(destX, destY, pix);
    p.end();

    return canvas;
}

QSize AnimationPreviewService::calculateAnimationSize(
    const QVector<AnimationTimeline>& timelines,
    int selectedTimelineIndex,
    const QVector<LayoutModel>& layoutModels,
    double zoom,
    int previewPadding) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size() || timelines[selectedTimelineIndex].frames.isEmpty()) {
        return QSize(280, 180);
    }

    const auto& frames = timelines[selectedTimelineIndex].frames;
    static QHash<QString, QSize> frameSizeCache;
    if (frameSizeCache.size() > 16384) {
        frameSizeCache.clear();
    }
    int maxLeftExtent = 0;
    int maxRightExtent = 0;
    int maxTopExtent = 0;
    int maxBottomExtent = 0;

    for (const QString& framePath : frames) {
        QSize frameSize = frameSizeCache.value(framePath);
        if (!frameSize.isValid()) {
            frameSize = QImageReader(framePath).size();
            if (frameSize.isValid()) {
                frameSizeCache.insert(framePath, frameSize);
            }
        }
        if (!frameSize.isValid()) {
            continue;
        }

        int framePivotX = frameSize.width() / 2;
        int framePivotY = frameSize.height() / 2;
        for (const auto& model : layoutModels) {
            for (const auto& sprite : model.sprites) {
                if (sprite->path == framePath) {
                    framePivotX = qBound(0, sprite->pivotX, frameSize.width());
                    framePivotY = qBound(0, sprite->pivotY, frameSize.height());
                    break;
                }
            }
        }

        maxLeftExtent = qMax(maxLeftExtent, framePivotX);
        maxRightExtent = qMax(maxRightExtent, frameSize.width() - framePivotX);
        maxTopExtent = qMax(maxTopExtent, framePivotY);
        maxBottomExtent = qMax(maxBottomExtent, frameSize.height() - framePivotY);
    }

    const int animationWidth = qMax(1, maxLeftExtent + maxRightExtent);
    const int animationHeight = qMax(1, maxTopExtent + maxBottomExtent);

    return QSize(qRound(animationWidth * zoom), qRound(animationHeight * zoom));
}
