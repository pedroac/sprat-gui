#pragma once
#include <QtCore/Qt>
#include <QPointF>
#include <QSizeF>

namespace SplitModeUtils {
    // Returns Qt::Horizontal when cursor is closer to left/right edges (horizontal split line),
    // Qt::Vertical when closer to top/bottom edges (vertical split line).
    // localPos: cursor position relative to rect's top-left corner.
    inline Qt::Orientation splitOrientation(const QPointF& localPos, const QSizeF& size) {
        const double distH = qMin(localPos.x(), size.width()  - localPos.x());
        const double distV = qMin(localPos.y(), size.height() - localPos.y());
        return (distH <= distV) ? Qt::Horizontal : Qt::Vertical;
    }
}
