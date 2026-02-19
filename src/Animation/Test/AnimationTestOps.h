#pragma once

#include <QStringList>

class AnimationTestOps {
public:
    static bool stepPrev(const QStringList& frames, int& frameIndex);
    static bool stepNext(const QStringList& frames, int& frameIndex);
    static bool tick(const QStringList& frames, int& frameIndex);
};
