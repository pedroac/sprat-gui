#include "AnimationPreviewService.h"

#include <QFileInfo>
#include <QLabel>
#include <QPainter>
#include <QPixmapCache>
#include <QPushButton>
#include <QTimer>

void AnimationPreviewService::refresh(
    const QVector<AnimationTimeline>& timelines,
    int selectedTimelineIndex,
    int& frameIndex,
    const LayoutModel& layoutModel,
    double zoom,
    QLabel* previewLabel,
    QLabel* statusLabel,
    QPushButton* prevButton,
    QPushButton* playPauseButton,
    QPushButton* nextButton,
    bool& playing,
    QTimer* timer) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size() || timelines[selectedTimelineIndex].frames.isEmpty()) {
        previewLabel->clear();
        previewLabel->setText("No frames");
        statusLabel->setText("Create/select a timeline and drag frames into it.");
        prevButton->setEnabled(false);
        playPauseButton->setEnabled(false);
        nextButton->setEnabled(false);
        if (playing) {
            playing = false;
            timer->stop();
            playPauseButton->setText("Play");
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
        previewLabel->setText("Invalid image");
        previewLabel->setPixmap(QPixmap());
        return;
    }

    QSize canvasSize = previewLabel->size();
    if (canvasSize.width() < 1 || canvasSize.height() < 1) {
        canvasSize = QSize(280, 180);
    }

    qreal dpr = previewLabel->devicePixelRatioF();
    QPixmap canvas(canvasSize * dpr);
    canvas.setDevicePixelRatio(dpr);
    canvas.fill(Qt::transparent);

    QPainter p(&canvas);
    if (zoom < 1.0) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
    }
    int pivotX = currentSprite ? currentSprite->pivotX : pix.width() / 2;
    int pivotY = currentSprite ? currentSprite->pivotY : pix.height() / 2;
    double destX = (canvasSize.width() / 2.0) - (pivotX * zoom);
    double destY = (canvasSize.height() / 2.0) - (pivotY * zoom);
    p.drawPixmap(QRectF(destX, destY, pix.width() * zoom, pix.height() * zoom), pix, pix.rect());
    p.end();
    previewLabel->setPixmap(canvas);
}
