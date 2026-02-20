#include "AnimationPreviewService.h"

#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QPainter>
#include <QPixmapCache>
#include <QPushButton>
#include <QTimer>
#include <QCoreApplication>
#include <QHash>
#include <QtGlobal>

namespace {
constexpr int kDefaultPreviewWidth = 280;
constexpr int kDefaultPreviewHeight = 180;

QString trAnimationPreview(const char* text) {
    return QCoreApplication::translate("AnimationPreviewService", text);
}
}

void AnimationPreviewService::refresh(
    const QVector<AnimationTimeline>& timelines,
    int selectedTimelineIndex,
    int& frameIndex,
    const LayoutModel& layoutModel,
    double zoom,
    int previewPadding,
    QLabel* previewLabel,
    QLabel* statusLabel,
    QPushButton* prevButton,
    QPushButton* playPauseButton,
    QPushButton* nextButton,
    bool& playing,
    QTimer* timer) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size() || timelines[selectedTimelineIndex].frames.isEmpty()) {
        previewLabel->clear();
        previewLabel->setText(trAnimationPreview("No frames"));
        previewLabel->setFixedSize(kDefaultPreviewWidth, kDefaultPreviewHeight);
        statusLabel->setText(trAnimationPreview("Create/select a timeline and drag frames into it."));
        prevButton->setEnabled(false);
        playPauseButton->setEnabled(false);
        nextButton->setEnabled(false);
        if (playing) {
            playing = false;
            timer->stop();
            playPauseButton->setText(trAnimationPreview("Play"));
        }
        return;
    }

    prevButton->setEnabled(true);
    playPauseButton->setEnabled(true);
    nextButton->setEnabled(true);

    const auto& frames = timelines[selectedTimelineIndex].frames;
    if (frameIndex >= frames.size()) {
        frameIndex = 0;
    }

    QString path = frames[frameIndex];
    QString spriteName = QFileInfo(path).baseName();
    SpritePtr currentSprite = nullptr;
    for (const auto& s : layoutModel.sprites) {
        if (s->path == path) {
            spriteName = s->name;
            currentSprite = s;
            break;
        }
    }

    statusLabel->setText(QString("%1 | frame %2/%3 | %4")
                             .arg(timelines[selectedTimelineIndex].name)
                             .arg(frameIndex + 1)
                             .arg(frames.size())
                             .arg(spriteName));

    QPixmap pix;
    if (!QPixmapCache::find(path, &pix)) {
        pix.load(path);
        QPixmapCache::insert(path, pix);
    }
    if (pix.isNull()) {
        previewLabel->setText(trAnimationPreview("Invalid image"));
        previewLabel->setPixmap(QPixmap());
        previewLabel->setFixedSize(kDefaultPreviewWidth, kDefaultPreviewHeight);
        return;
    }

    const int effectivePadding = qMax(0, previewPadding);
    const QSize spriteSize(qMax(1, qRound(pix.width() * zoom)), qMax(1, qRound(pix.height() * zoom)));

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
        for (const auto& sprite : layoutModel.sprites) {
            if (sprite->path == framePath) {
                framePivotX = qBound(0, sprite->pivotX, frameSize.width());
                framePivotY = qBound(0, sprite->pivotY, frameSize.height());
                break;
            }
        }

        maxLeftExtent = qMax(maxLeftExtent, qRound(framePivotX * zoom));
        maxRightExtent = qMax(maxRightExtent, qRound((frameSize.width() - framePivotX) * zoom));
        maxTopExtent = qMax(maxTopExtent, qRound(framePivotY * zoom));
        maxBottomExtent = qMax(maxBottomExtent, qRound((frameSize.height() - framePivotY) * zoom));
    }

    if (maxLeftExtent <= 0 && maxRightExtent <= 0 && maxTopExtent <= 0 && maxBottomExtent <= 0) {
        int pivotX = currentSprite ? qBound(0, currentSprite->pivotX, pix.width()) : pix.width() / 2;
        int pivotY = currentSprite ? qBound(0, currentSprite->pivotY, pix.height()) : pix.height() / 2;
        maxLeftExtent = qMax(1, qRound(pivotX * zoom));
        maxRightExtent = qMax(1, qRound((pix.width() - pivotX) * zoom));
        maxTopExtent = qMax(1, qRound(pivotY * zoom));
        maxBottomExtent = qMax(1, qRound((pix.height() - pivotY) * zoom));
    }

    const int animationWidth = qMax(1, maxLeftExtent + maxRightExtent);
    const int animationHeight = qMax(1, maxTopExtent + maxBottomExtent);

    const QSize canvasSize(
        qMax(animationWidth, spriteSize.width()) + (effectivePadding > 0 ? effectivePadding * 2 : 0),
        qMax(animationHeight, spriteSize.height()) + (effectivePadding > 0 ? effectivePadding * 2 : 0));

    qreal dpr = previewLabel->devicePixelRatioF();
    QPixmap canvas(canvasSize * dpr);
    canvas.setDevicePixelRatio(dpr);
    canvas.fill(Qt::transparent);

    QPainter p(&canvas);
    if (zoom < 1.0) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
    }
    int pivotX = currentSprite ? qBound(0, currentSprite->pivotX, pix.width()) : pix.width() / 2;
    int pivotY = currentSprite ? qBound(0, currentSprite->pivotY, pix.height()) : pix.height() / 2;
    const int anchorX = effectivePadding + maxLeftExtent;
    const int anchorY = effectivePadding + maxTopExtent;
    double destX = anchorX - (pivotX * zoom);
    double destY = anchorY - (pivotY * zoom);
    p.drawPixmap(QRectF(destX, destY, pix.width() * zoom, pix.height() * zoom), pix, pix.rect());
    p.end();
    previewLabel->setPixmap(canvas);
    previewLabel->setFixedSize(canvasSize);
}
