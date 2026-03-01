#pragma once

#include <QVector>
#include "models.h"

class QLabel;
class QPushButton;
class QTimer;

class AnimationPreviewService {
public:
    static void refresh(
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
        QTimer* timer);

    static QSize calculateAnimationSize(
        const QVector<AnimationTimeline>& timelines,
        int selectedTimelineIndex,
        const LayoutModel& layoutModel,
        double zoom,
        int previewPadding);
};
