#pragma once
#include <QtTest>

class AnimationTests : public QObject {
    Q_OBJECT
private slots:
    void testAnimationPreviewUsesTimelineBounds();
    void testTimelineGenerationFromLayout();
};
