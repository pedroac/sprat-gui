#pragma once
#include <functional>
#include <optional>
#include <QHash>
#include <QPointF>
#include <QString>
#include <QTransform>
#include <QVector>
#include "PuppetModels.h"

/// Where a sprite's pivot and markers are, in its original-image pixels (trim included).
struct PuppetSpriteGeometry {
    QPointF pivot;
    /// Each marker's x,y: a point, a circle's center, a rectangle's corner, a polygon's
    /// first vertex, as sprat-cli's markers file writes them.
    QHash<QString, QPointF> markers;
};

/// The geometry of the sprite a variant shows (an animation's current frame), or nullopt
/// when it can't be found.
using PuppetSpriteLookup = std::function<std::optional<PuppetSpriteGeometry>(const PuppetVariant&)>;

/// What a part shows and how it is turned and moved: the rest pose by default. Poses
/// change these.
struct PuppetPartState {
    int variant = 0;   ///< Index into the part's variants
    double angle = 0;  ///< Degrees clockwise: from the parent, or around the part's own pivot
    QPointF offset;    ///< Attached parts: moves the joint from the parent marker
    std::optional<QPointF> position;  ///< Positioned parts: replaces `at:`
};

/// Where a part ended up.
struct PuppetPartPlacement {
    bool placed = false;
    /// Maps the shown sprite's original-image pixels into the puppet's space (origin = the
    /// puppet's pivot, Y down).
    QTransform transform;
    double totalAngle = 0;  ///< The part's angle plus its parents'
    QPointF pivot;          ///< The point it turns around, in the puppet's space
    QString problem;        ///< Why it couldn't be placed
};

/// Places a puppet's parts as sprat-cli's "Playing a puppet" describes. Pure: no I/O.
class PuppetEvaluator {
public:
    /// Places every part (results in draw order, like puppet.parts).
    /// @param states One per part, in draw order; missing ones are the rest state
    static QVector<PuppetPartPlacement> evaluate(const Puppet& puppet, const QVector<PuppetPartState>& states,
                                                 const PuppetSpriteLookup& lookup);

    /// Places every part in the rest pose: first variant, angle 0, offset 0, positions at `at:`.
    static QVector<PuppetPartPlacement> evaluateRest(const Puppet& puppet, const PuppetSpriteLookup& lookup);
};
