#pragma once

#include "MarkerModels.h"
#include "AppIcons.h"
#include <QIcon>
#include <QString>

inline QIcon markerKindIcon(MarkerKind kind) {
    switch (kind) {
    case MarkerKind::Circle:    return AppIcons::icon(QStringLiteral("marker-circle"));
    case MarkerKind::Rectangle: return AppIcons::icon(QStringLiteral("marker-rect"));
    case MarkerKind::Polygon:   return AppIcons::icon(QStringLiteral("marker-polygon"));
    default:                    return AppIcons::icon(QStringLiteral("marker-point"));
    }
}

inline QString normalizeMarkerName(QString name) {
    name = name.trimmed();
    if (name.compare("pivot", Qt::CaseInsensitive) == 0) {
        return "pivot";
    }
    return name;
}
