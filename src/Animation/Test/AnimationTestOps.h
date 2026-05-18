#pragma once

#include <QStringList>

class AnimationTestOps {
public:
    static bool stepPrev(const QStringList& frames, int& frameIndex);
    static bool stepNext(const QStringList& frames, int& frameIndex);
    // Advance frameIndex by count frames (wrapping).  count defaults to 1
    // for the common single-step case.
    static bool tick(const QStringList& frames, int& frameIndex, int count = 1);
};
