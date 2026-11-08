#include "PuppetPosing.h"

namespace PuppetPosing {

QVector<PuppetPartState> statesOf(const Puppet& puppet, const PuppetPose& pose) {
    QVector<PuppetPartState> states(puppet.parts.size());
    for (const PuppetPoseSetting& setting : pose.settings) {
        int index = -1;
        for (int i = 0; i < puppet.parts.size(); ++i) {
            if (puppet.parts[i].name == setting.part) index = i;
        }
        if (index < 0) continue;
        const PuppetPart& part = puppet.parts[index];
        PuppetPartState& state = states[index];
        if (!setting.variant.isEmpty()) {
            for (int v = 0; v < part.variants.size(); ++v) {
                if (puppetVariantName(part.variants[v]) == setting.variant) state.variant = v;
            }
        }
        if (setting.angle) state.angle = *setting.angle;
        if (setting.offset) state.offset = QPointF(*setting.offset);
        if (setting.position) state.position = QPointF(*setting.position);
        if (setting.scale) state.scale = *setting.scale;
        if (setting.opacity) state.opacity = *setting.opacity;
        if (setting.tint) state.tint = *setting.tint;
    }
    return states;
}

}  // namespace PuppetPosing
