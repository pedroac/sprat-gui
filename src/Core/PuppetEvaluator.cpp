#include "PuppetEvaluator.h"

#include <QCoreApplication>

namespace {

QString trPE(const char* text) {
    return QCoreApplication::translate("PuppetEvaluator", text);
}

// The point a part's own marker names in its sprite. A sprite without a "pivot" marker
// uses its pivot, as the export writes one from it.
std::optional<QPointF> markerPoint(const PuppetSpriteGeometry& geometry, const QString& name) {
    const auto it = geometry.markers.constFind(name);
    if (it != geometry.markers.constEnd()) return *it;
    if (name == QLatin1String("pivot")) return geometry.pivot;
    return std::nullopt;
}

}  // namespace

QVector<PuppetPartPlacement> PuppetEvaluator::evaluate(const Puppet& puppet, const QVector<PuppetPartState>& states,
                                                       const PuppetSpriteLookup& lookup) {
    const int count = puppet.parts.size();
    QVector<PuppetPartPlacement> placements(count);
    QHash<QString, int> indexByName;
    for (int i = 0; i < count; ++i) indexByName.insert(puppet.parts[i].name, i);

    const auto stateOf = [&states](int index) {
        return index < states.size() ? states[index] : PuppetPartState{};
    };
    const auto geometryOf = [&](int index) -> std::optional<PuppetSpriteGeometry> {
        const PuppetPart& part = puppet.parts[index];
        const int variant = stateOf(index).variant;
        if (variant < 0 || variant >= part.variants.size()) return std::nullopt;
        return lookup ? lookup(part.variants[variant]) : std::nullopt;
    };

    // 0 = not visited, 1 = being placed (its parent chain is on the stack), 2 = done.
    QVector<int> visit(count, 0);
    // A parent is placed before its child, whatever their draw order.
    std::function<void(int)> place = [&](int index) {
        if (visit[index] != 0) {
            if (visit[index] == 1)
                placements[index].problem = trPE("Part \"%1\" is part of a cycle of attachments")
                                                .arg(puppet.parts[index].name);
            return;
        }
        visit[index] = 1;
        PuppetPartPlacement& result = placements[index];
        const PuppetPart& part = puppet.parts[index];
        const PuppetPartState state = stateOf(index);
        const std::optional<PuppetSpriteGeometry> geometry = geometryOf(index);
        const auto done = [&]() { visit[index] = 2; };
        result.opacity = state.opacity;
        result.tint = state.tint;

        if (!geometry) {
            result.problem = part.variants.isEmpty()
                ? trPE("Part \"%1\" has no variants").arg(part.name)
                : trPE("Part \"%1\" shows a sprite or animation that isn't in the atlas").arg(part.name);
            return done();
        }

        const PuppetAttachment* joint = puppetParentAttachment(puppet, part.name);
        if (!joint) {
            if (!part.position && !state.position) {
                result.problem = trPE("Part \"%1\" has no position and is attached to no part").arg(part.name);
                return done();
            }
            const QPointF at = state.position ? *state.position : QPointF(*part.position);
            result.pivot = at;
            result.totalAngle = state.angle;
            result.transform.translate(at.x(), at.y());
            result.transform.rotate(state.angle);
            result.transform.scale(state.scale.x(), state.scale.y());
            result.transform.translate(-geometry->pivot.x(), -geometry->pivot.y());
            result.placed = true;
            return done();
        }

        const auto parentIt = indexByName.constFind(joint->parent);
        if (parentIt == indexByName.constEnd()) {
            result.problem = trPE("Part \"%1\" is attached to unknown part \"%2\"").arg(part.name, joint->parent);
            return done();
        }
        const int parentIndex = *parentIt;
        if (parentIndex == index) {
            result.problem = trPE("Part \"%1\" is attached to itself").arg(part.name);
            return done();
        }
        place(parentIndex);
        if (!result.problem.isEmpty()) return done();  // the cycle came back to this part
        const PuppetPartPlacement& parent = placements[parentIndex];
        if (!parent.placed) {
            result.problem = trPE("Part \"%1\" hangs from part \"%2\", which can't be placed")
                                 .arg(part.name, joint->parent);
            return done();
        }
        const std::optional<QPointF> childPoint = markerPoint(*geometry, joint->childMarker);
        const std::optional<PuppetSpriteGeometry> parentGeometry = geometryOf(parentIndex);
        const std::optional<QPointF> parentPoint =
            parentGeometry ? markerPoint(*parentGeometry, joint->parentMarker) : std::nullopt;
        if (!childPoint || !parentPoint) {
            result.problem = !childPoint
                ? trPE("Part \"%1\" has no marker \"%2\" to attach it by").arg(part.name, joint->childMarker)
                : trPE("Part \"%1\" has no marker \"%2\" to attach part \"%3\" to")
                      .arg(joint->parent, joint->parentMarker, part.name);
            return done();
        }
        // The offset is in the parent's sprite pixels, so it moves (turns, and scales) with the parent.
        const QPointF anchor = parent.transform.map(*parentPoint + state.offset);
        result.pivot = anchor;
        result.totalAngle = parent.totalAngle + state.angle;
        result.transform.translate(anchor.x(), anchor.y());
        result.transform.rotate(result.totalAngle);
        result.transform.scale(state.scale.x(), state.scale.y());
        result.transform.translate(-childPoint->x(), -childPoint->y());
        result.placed = true;
        done();
    };

    for (int i = 0; i < count; ++i) place(i);
    return placements;
}

QVector<PuppetPartPlacement> PuppetEvaluator::evaluateRest(const Puppet& puppet, const PuppetSpriteLookup& lookup) {
    return evaluate(puppet, {}, lookup);
}
