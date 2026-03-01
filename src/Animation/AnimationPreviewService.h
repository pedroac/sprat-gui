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
        const LayoutModel& layoutModel,
        QString& statusText,
        bool& hasFrames,
        bool& playing,
        QTimer* timer);

    static QSize calculateAnimationSize(
        const QVector<AnimationTimeline>& timelines,
        int selectedTimelineIndex,
        const LayoutModel& layoutModel,
        double zoom,
        int previewPadding);
};
