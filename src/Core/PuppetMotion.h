#pragma once
#include <functional>
#include <optional>
#include <QPainterPath>
#include <QPoint>
#include <QPointF>
#include <QSet>
#include "MarkerModels.h"
#include "PuppetEvaluator.h"
#include "PuppetModels.h"

/// How far an attached part may turn and drift, as sprat-cli's "Puppets" says: within its
/// parent joint's `angle:` limits (freely without them), and with its point inside the parent
/// marker's shape or up to `leeway` pixels beyond it. Pure, in the parent sprite's pixels.
namespace PuppetMotion {

/// The point a joint on @p marker goes to: a point, a circle's center, a rectangle's corner,
/// a polygon's first vertex (as sprat-cli's markers file writes them).
QPointF markerPoint(const NamedPoint& marker);
/// The marker's outline; empty for a point.
QPainterPath markerShape(const NamedPoint& marker);

/// @p angle (degrees clockwise from the parent) within the joint's limits, in whole degrees.
/// Without limits the part turns freely: the angle is brought into -179..180.
int clampAngle(const PuppetAttachment& joint, double angle);
/// @p angle of a free turn (no limits, or a positioned part around its pivot), in whole
/// degrees brought into -179..180.
int freeAngle(double angle);

/// @p offset (from the marker's point) moved to the nearest place the joint may go: inside
/// the marker's shape, or up to @p leeway pixels from it. Whole pixels.
QPoint clampOffset(const NamedPoint& marker, int leeway, const QPointF& offset);

/// Where the joint may go: the marker's shape grown by @p leeway (a circle of that radius
/// around a point marker). Empty when it can't move at all.
QPainterPath jointArea(const NamedPoint& marker, int leeway);

/// The marker a part hangs from, in its parent's sprite, or nullopt (positioned, or not found).
using PuppetParentMarker = std::function<std::optional<NamedPoint>(int partIndex)>;

/// Pulls attached part @p partIndex so that its point @p grabbed, in its shown sprite's pixels,
/// gets as close to @p target, in the puppet's space, as its joints allow. From the part up
/// to the first part placed by position (which stays), each joint first moves within its
/// parent marker's shape and leeway (a pupil in its socket), then turns within its angle
/// limits (moving a hand turns the forearm and the upper arm). Cyclic coordinate descent.
/// Without @p parentMarker, joints only turn. Parts in @p fixed (locked) don't move: the chain
/// stops below them. Returns @p states with the new angles and offsets.
QVector<PuppetPartState> reach(const Puppet& puppet, QVector<PuppetPartState> states, int partIndex,
                               const QPointF& grabbed, const QPointF& target, const PuppetSpriteLookup& lookup,
                               const PuppetParentMarker& parentMarker = {}, const QSet<int>& fixed = {});

}  // namespace PuppetMotion
