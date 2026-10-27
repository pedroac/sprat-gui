#pragma once
#include <QtTest>

class AnimationTests : public QObject {
    Q_OBJECT
private slots:
    void testAnimationPreviewUsesTimelineBounds();
    void testTimelineGenerationFromLayout();
    void testAnimationLoopModeNames();
    void testAnimationPlayOrder();
    void testPlaybackFollowsLoopMode();
    void testPlaybackRestartsFinishedOnceAnimation();
};
