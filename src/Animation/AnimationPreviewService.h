#pragma once

#include <QVector>
#include <QPixmap>
#include "models.h"

class QLabel;
class QPushButton;
class QTimer;

class AnimationPreviewService {
public:
    static QPixmap refresh(
        const QVector<AnimationTimeline>& timelines,
        int selectedTimelineIndex,
        int& frameIndex,
        const QVector<LayoutModel>& layoutModels,
        QString& statusText,
        bool& hasFrames,
        bool& playing,
        QTimer* timer);

    static QSize calculateAnimationSize(
        const QVector<AnimationTimeline>& timelines,
        int selectedTimelineIndex,
        const QVector<LayoutModel>& layoutModels,
        double zoom,
        int previewPadding);
};
