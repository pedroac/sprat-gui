#include "PuppetSequencing.h"

#include <algorithm>
#include <functional>

namespace {

const PuppetPose* poseNamed(const Puppet& puppet, const QString& name) {
    for (const PuppetPose& pose : puppet.poses) {
        if (pose.name == name) return &pose;
    }
    return nullptr;
}

/// The steps in the order a cycle plays them: forward, then for ping-pong back without
/// repeating the turning point.
QVector<int> cycleOrder(const PuppetSequence& sequence) {
    QVector<int> order;
    for (int i = 0; i < sequence.steps.size(); ++i) order.append(i);
    const bool pingPong = sequence.loopMode == AnimationLoopMode::PingPong
                       || sequence.loopMode == AnimationLoopMode::OncePingPong;
    if (pingPong) {
        for (int i = sequence.steps.size() - 2; i >= 0; --i) order.append(i);
    }
    return order;
}

/// Moves what @p pose sets in @p states the way to the pose's values that @p progressOf
/// gives for each part (0..1); variants switch only when it arrives.
void approach(const Puppet& puppet, const PuppetPose& pose, const std::function<double(const QString&)>& progressOf,
              QVector<PuppetPartState>& states) {
    for (const PuppetPoseSetting& setting : pose.settings) {
        const double progress = progressOf(setting.part);
        int index = -1;
        for (int i = 0; i < puppet.parts.size(); ++i) {
            if (puppet.parts[i].name == setting.part) index = i;
        }
        if (index < 0 || index >= states.size()) continue;
        PuppetPartState& state = states[index];
        if (setting.angle) state.angle += (*setting.angle - state.angle) * progress;
        if (setting.offset) state.offset += (QPointF(*setting.offset) - state.offset) * progress;
        if (setting.position) {
            const QPointF from = state.position.value_or(QPointF(puppet.parts[index].position.value_or(QPoint())));
            state.position = from + (QPointF(*setting.position) - from) * progress;
        }
        if (!setting.variant.isEmpty() && progress >= 1.0) {
            const PuppetPart& part = puppet.parts[index];
            for (int v = 0; v < part.variants.size(); ++v) {
                if (puppetVariantName(part.variants[v]) == setting.variant) state.variant = v;
            }
        }
    }
}

/// How far part @p part has got in a step, @p frame frames into it.
double progressAt(const PuppetSequenceStep& step, int frame, const QString& part) {
    if (step.in <= 0) return 1.0;
    const double progress = std::min(1.0, double(frame + 1) / step.in);
    return step.snaps(part) ? (progress >= 1.0 ? 1.0 : 0.0) : progress;
}

double arrived(const QString&) {
    return 1.0;
}

}  // namespace

namespace PuppetSequencing {

int stepFrames(const PuppetSequenceStep& step) {
    return std::max(1, step.in + step.hold);
}

int passFrames(const PuppetSequence& sequence) {
    int frames = 0;
    for (const PuppetSequenceStep& step : sequence.steps) frames += stepFrames(step);
    return frames;
}

int cycleFrames(const PuppetSequence& sequence) {
    int frames = 0;
    for (int index : cycleOrder(sequence)) frames += stepFrames(sequence.steps[index]);
    return frames;
}

int stepStart(const PuppetSequence& sequence, int stepIndex) {
    int frame = 0;
    for (int i = 0; i < stepIndex && i < sequence.steps.size(); ++i) frame += stepFrames(sequence.steps[i]);
    return frame;
}

int passFrameOf(const PuppetSequence& sequence, int frame) {
    const int cycle = cycleFrames(sequence);
    if (cycle <= 0) return -1;
    const bool once = sequence.loopMode == AnimationLoopMode::Once
                   || sequence.loopMode == AnimationLoopMode::OncePingPong;
    int local = once ? std::clamp(frame, 0, cycle - 1) : ((frame % cycle) + cycle) % cycle;
    for (int index : cycleOrder(sequence)) {
        const int frames = stepFrames(sequence.steps[index]);
        if (local < frames) return stepStart(sequence, index) + local;
        local -= frames;
    }
    return -1;
}

int stepAt(const PuppetSequence& sequence, int frame) {
    if (frame < 0) return -1;
    for (int i = 0; i < sequence.steps.size(); ++i) {
        frame -= stepFrames(sequence.steps[i]);
        if (frame < 0) return i;
    }
    return -1;
}

QVector<PuppetPartState> statesAt(const Puppet& puppet, const PuppetSequence& sequence, int frame,
                                  QVector<PuppetPartState> base) {
    base.resize(puppet.parts.size());
    const QVector<int> order = cycleOrder(sequence);
    const int cycle = cycleFrames(sequence);
    if (order.isEmpty() || cycle <= 0) return base;
    frame = std::max(0, frame);
    const bool once = sequence.loopMode == AnimationLoopMode::Once
                   || sequence.loopMode == AnimationLoopMode::OncePingPong;
    if (once) frame = std::min(frame, cycle - 1);

    QVector<PuppetPartState> states = base;
    const auto playWhole = [&]() {
        for (int index : order) {
            if (const PuppetPose* pose = poseNamed(puppet, sequence.steps[index].pose)) approach(puppet, *pose, arrived, states);
        }
    };
    // Each later cycle starts from where the one before left off: wherever a whole cycle
    // leaves the values, since every value a pose sets ends at the last pose that sets it.
    if (frame >= cycle) playWhole();
    int local = frame % cycle;
    for (int index : order) {
        const PuppetSequenceStep& step = sequence.steps[index];
        const PuppetPose* pose = poseNamed(puppet, step.pose);
        const int frames = stepFrames(step);
        if (local >= frames) {
            if (pose) approach(puppet, *pose, arrived, states);
            local -= frames;
            continue;
        }
        if (pose) approach(puppet, *pose, [&](const QString& part) { return progressAt(step, local, part); }, states);
        break;
    }
    return states;
}

}  // namespace PuppetSequencing
