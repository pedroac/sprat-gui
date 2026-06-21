#pragma once

#include "MarkerModels.h"
#include <QIcon>
#include <QString>

inline QIcon markerKindIcon(MarkerKind kind) {
    switch (kind) {
    case MarkerKind::Circle:    return QIcon(":/icons/circle.svg");
    case MarkerKind::Rectangle: return QIcon(":/icons/rectangle-wide.svg");
    case MarkerKind::Polygon:   return QIcon(":/icons/polygon.svg");
    default:                    return QIcon(":/icons/point-scan.svg");
    }
}

inline QString normalizeMarkerName(QString name) {
    name = name.trimmed();
    if (name.compare("pivot", Qt::CaseInsensitive) == 0) {
        return "pivot";
    }
    return name;
}
