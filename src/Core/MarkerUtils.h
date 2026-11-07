#pragma once

#include "MarkerModels.h"
#include <QIcon>
#include <QSet>
#include <QString>
#include <QVector>

inline QIcon markerKindIcon(MarkerKind kind) {
    switch (kind) {
    case MarkerKind::Circle:    return QIcon(":/icons/circle.svg");
    case MarkerKind::Rectangle: return QIcon(":/icons/rectangle-wide.svg");
    case MarkerKind::Polygon:   return QIcon(":/icons/polygon.svg");
    default:                    return QIcon(":/icons/point-scan.svg");
    }
}

/// The marker added last: the last one in @p after whose name isn't in @p before, or an empty
/// string when none was added.
inline QString addedMarkerName(const QVector<NamedPoint>& before, const QVector<NamedPoint>& after) {
    QSet<QString> old;
    for (const NamedPoint& point : before) old.insert(point.name);
    for (auto it = after.crbegin(); it != after.crend(); ++it) {
        if (!old.contains(it->name)) return it->name;
    }
    return {};
}

inline QString normalizeMarkerName(QString name) {
    name = name.trimmed();
    if (name.compare("pivot", Qt::CaseInsensitive) == 0) {
        return "pivot";
    }
    return name;
}
