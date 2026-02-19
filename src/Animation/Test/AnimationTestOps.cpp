#include "AnimationTestOps.h"

bool AnimationTestOps::stepPrev(const QStringList& frames, int& frameIndex) {
    if (frames.isEmpty()) {
        return false;
    }
    frameIndex = (frameIndex - 1 + frames.size()) % frames.size();
    return true;
}

bool AnimationTestOps::stepNext(const QStringList& frames, int& frameIndex) {
    if (frames.isEmpty()) {
        return false;
    }
    frameIndex = (frameIndex + 1) % frames.size();
    return true;
}

bool AnimationTestOps::tick(const QStringList& frames, int& frameIndex) {
    return stepNext(frames, frameIndex);
}
