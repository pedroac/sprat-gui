#include "PuppetMotion.h"

#include <cmath>
#include <QLineF>
#include <QPainterPathStroker>
#include <QPolygonF>

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
