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

int AnimationTestOps::currentPlayStep(const QVector<int>& playOrder, int frameIndex, int playStep) {
    if (playStep >= 0 && playStep < playOrder.size() && playOrder[playStep] == frameIndex)
        return playStep;
    return qMax(0, playOrder.indexOf(frameIndex));
}

bool AnimationTestOps::tickPlayOrder(const QVector<int>& playOrder, bool repeats,
                                     int& frameIndex, int& playStep, int count, bool& finished) {
    finished = false;
    if (playOrder.isEmpty())
        return false;
    const int n = playOrder.size();
    playStep = currentPlayStep(playOrder, frameIndex, playStep);
    if (repeats) {
        playStep = (playStep + qMax(1, count)) % n;
    } else {
        playStep = qMin(playStep + qMax(1, count), n - 1);
        finished = playStep == n - 1;
    }
    frameIndex = playOrder[playStep];
    return true;
}
