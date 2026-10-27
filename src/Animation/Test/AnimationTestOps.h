#pragma once

#include <QStringList>
#include <QVector>

class AnimationTestOps {
public:
    static bool stepPrev(const QStringList& frames, int& frameIndex);
    static bool stepNext(const QStringList& frames, int& frameIndex);
    // The step of playOrder that shows frameIndex: playStep when it still does,
    // otherwise (the user picked another frame) that frame's first step, or 0.
    static int currentPlayStep(const QVector<int>& playOrder, int frameIndex, int playStep);
    // Advance playStep by count steps along playOrder (see animationPlayOrder()),
    // starting from currentPlayStep(), wrapping when repeats is true and otherwise
    // stopping on the last step, where finished becomes true.
    static bool tickPlayOrder(const QVector<int>& playOrder, bool repeats,
                              int& frameIndex, int& playStep, int count, bool& finished);
};
