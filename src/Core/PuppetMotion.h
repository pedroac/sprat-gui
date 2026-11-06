#pragma once
#include <QPainterPath>
#include <QPoint>
#include <QPointF>
#include "MarkerModels.h"
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

/// @p offset (from the marker's point) moved to the nearest place the joint may go: inside
/// the marker's shape, or up to @p leeway pixels from it. Whole pixels.
QPoint clampOffset(const NamedPoint& marker, int leeway, const QPointF& offset);

/// Where the joint may go: the marker's shape grown by @p leeway (a circle of that radius
/// around a point marker). Empty when it can't move at all.
QPainterPath jointArea(const NamedPoint& marker, int leeway);

}  // namespace PuppetMotion
