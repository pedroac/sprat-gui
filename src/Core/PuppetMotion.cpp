#include "PuppetMotion.h"

#include <cmath>
#include <QLineF>
#include <QPainterPathStroker>
#include <QPolygonF>
#include <QSet>

namespace {

/// The point of segment @p a–@p b nearest to @p p.
QPointF nearestOnSegment(const QPointF& a, const QPointF& b, const QPointF& p) {
    const QPointF ab = b - a;
    const qreal lengthSquared = QPointF::dotProduct(ab, ab);
    if (lengthSquared <= 0) return a;
    const qreal t = qBound<qreal>(0, QPointF::dotProduct(p - a, ab) / lengthSquared, 1);
    return a + t * ab;
}

/// The point of the marker (its area, for shapes) nearest to @p p: @p p itself when inside.
QPointF nearestOnMarker(const NamedPoint& marker, const QPointF& p) {
    switch (marker.kind) {
    case MarkerKind::Circle: {
        const QPointF center(marker.x, marker.y);
        const qreal distance = QLineF(center, p).length();
        if (distance <= marker.radius || distance <= 0) return p;
        return center + (p - center) * (marker.radius / distance);
    }
    case MarkerKind::Rectangle:
        return QPointF(qBound<qreal>(marker.x, p.x(), marker.x + marker.w),
                       qBound<qreal>(marker.y, p.y(), marker.y + marker.h));
    case MarkerKind::Polygon: {
        if (marker.polygonPoints.size() < 3) break;
        const QPolygonF polygon(QPolygon(marker.polygonPoints));
        if (polygon.containsPoint(p, Qt::OddEvenFill)) return p;
        QPointF nearest = polygon.first();
        qreal nearestDistance = QLineF(nearest, p).length();
        for (int i = 0; i < polygon.size(); ++i) {
            const QPointF candidate = nearestOnSegment(polygon[i], polygon[(i + 1) % polygon.size()], p);
            const qreal distance = QLineF(candidate, p).length();
            if (distance < nearestDistance) {
                nearest = candidate;
                nearestDistance = distance;
            }
        }
        return nearest;
    }
    case MarkerKind::Point:
        break;
    }
    return PuppetMotion::markerPoint(marker);
}

}  // namespace

namespace PuppetMotion {

QPointF markerPoint(const NamedPoint& marker) {
    if (marker.kind == MarkerKind::Polygon && !marker.polygonPoints.isEmpty())
        return marker.polygonPoints.first();
    return QPointF(marker.x, marker.y);
}

QPainterPath markerShape(const NamedPoint& marker) {
    QPainterPath path;
    switch (marker.kind) {
    case MarkerKind::Circle:
        path.addEllipse(QPointF(marker.x, marker.y), marker.radius, marker.radius);
        break;
    case MarkerKind::Rectangle:
        path.addRect(QRectF(marker.x, marker.y, marker.w, marker.h));
        break;
    case MarkerKind::Polygon:
        if (marker.polygonPoints.size() >= 3) {
            path.addPolygon(QPolygonF(QPolygon(marker.polygonPoints)));
            path.closeSubpath();
        }
        break;
    case MarkerKind::Point:
        break;
    }
    return path;
}

int clampAngle(const PuppetAttachment& joint, double angle) {
    if (joint.angleMin && joint.angleMax)
        return qBound(*joint.angleMin, int(std::lround(angle)), *joint.angleMax);
    return freeAngle(angle);
}

int freeAngle(double angle) {
    int free = int(std::lround(std::fmod(angle, 360.0)));
    if (free > 180) free -= 360;
    if (free <= -180) free += 360;
    return free;
}

QPoint clampOffset(const NamedPoint& marker, int leeway, const QPointF& offset) {
    const QPointF origin = markerPoint(marker);
    QPointF wanted = origin + offset;
    const QPointF nearest = nearestOnMarker(marker, wanted);
    const qreal distance = QLineF(nearest, wanted).length();
    if (distance > leeway) wanted = nearest + (wanted - nearest) * (qMax(0, leeway) / distance);
    const QPointF clamped = wanted - origin;
    return QPoint(int(std::lround(clamped.x())), int(std::lround(clamped.y())));
}

QVector<PuppetPartState> reach(const Puppet& puppet, QVector<PuppetPartState> states, int partIndex,
                               const QPointF& grabbed, const QPointF& target, const PuppetSpriteLookup& lookup) {
    constexpr int MAX_PASSES = 16;
    constexpr qreal CLOSE_ENOUGH = 0.5;  // pixels
    states.resize(puppet.parts.size());
    if (partIndex < 0 || partIndex >= puppet.parts.size()) return states;

    // The part, its parent, and so on, while they hang from another part.
    QVector<QPair<int, const PuppetAttachment*>> chain;
    QSet<int> seen;
    for (int i = partIndex; i >= 0 && !seen.contains(i);) {
        seen.insert(i);
        const PuppetAttachment* joint = puppetParentAttachment(puppet, puppet.parts[i].name);
        if (!joint) break;
        chain.append({i, joint});
        i = -1;
        for (int p = 0; p < puppet.parts.size(); ++p) {
            if (puppet.parts[p].name == joint->parent) i = p;
        }
    }

    for (int pass = 0; pass < MAX_PASSES; ++pass) {
        bool turned = false;
        for (const auto& [index, joint] : chain) {
            const QVector<PuppetPartPlacement> placements = PuppetEvaluator::evaluate(puppet, states, lookup);
            if (!placements[partIndex].placed || !placements[index].placed) return states;
            const QPointF pivot = placements[index].pivot;
            const QPointF effector = placements[partIndex].transform.map(grabbed);
            if (QLineF(effector, target).length() < CLOSE_ENOUGH) return states;
            if (QLineF(pivot, effector).length() < CLOSE_ENOUGH || QLineF(pivot, target).length() < CLOSE_ENOUGH)
                continue;
            double step = QLineF(pivot, effector).angleTo(QLineF(pivot, target));
            // QLineF angles go counter-clockwise with y up; the puppet's go clockwise with y down.
            step = step > 180 ? 360 - step : -step;
            const int angle = clampAngle(*joint, states[index].angle + step);
            turned = turned || angle != int(std::lround(states[index].angle));
            states[index].angle = angle;
        }
        if (!turned) break;
    }
    return states;
}

QPainterPath jointArea(const NamedPoint& marker, int leeway) {
    QPainterPath shape = markerShape(marker);
    if (shape.isEmpty()) {
        QPainterPath around;
        if (leeway > 0) around.addEllipse(markerPoint(marker), leeway, leeway);
        return around;
    }
    if (leeway <= 0) return shape;
    QPainterPathStroker stroker;
    stroker.setWidth(2.0 * leeway);
    stroker.setJoinStyle(Qt::RoundJoin);
    stroker.setCapStyle(Qt::RoundCap);
    return shape.united(stroker.createStroke(shape)).simplified();
}

}  // namespace PuppetMotion
