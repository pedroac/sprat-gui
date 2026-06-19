#pragma once
#include <QString>
#include <QStringView>
#include <QVector>
#include <QPoint>

/**
 * @enum MarkerKind
 * @brief Enum representing different types of markers that can be placed on sprites.
 */
enum class MarkerKind {
    Point = 0,
    Circle,
    Rectangle,
    Polygon
};

/**
 * @brief Converts MarkerKind enum to string representation.
 */
inline QString markerKindToString(MarkerKind kind) {
    switch (kind) {
    case MarkerKind::Point:
        return "point";
    case MarkerKind::Circle:
        return "circle";
    case MarkerKind::Rectangle:
        return "rectangle";
    case MarkerKind::Polygon:
        return "polygon";
    }
    return "point";
}

/**
 * @brief Converts string representation to MarkerKind enum.
 */
inline MarkerKind markerKindFromString(QStringView kind) {
    const QString normalized = kind.toString().trimmed().toLower();
    if (normalized == "circle") {
        return MarkerKind::Circle;
    }
    if (normalized == "rectangle" || normalized == "rect") {
        return MarkerKind::Rectangle;
    }
    if (normalized == "polygon") {
        return MarkerKind::Polygon;
    }
    return MarkerKind::Point;
}

/**
 * @struct NamedPoint
 * @brief Represents a named point or shape on a sprite.
 */
struct NamedPoint {
    QString name;
    int x = 0;
    int y = 0;
    MarkerKind kind = MarkerKind::Point;
    int radius = 8;
    int w = 16;
    int h = 16;
    QVector<QPoint> polygonPoints;

    bool operator==(const NamedPoint& other) const {
        return name == other.name && x == other.x && y == other.y &&
               kind == other.kind && radius == other.radius &&
               w == other.w && h == other.h &&
               polygonPoints == other.polygonPoints;
    }

    bool operator!=(const NamedPoint& other) const {
        return !(*this == other);
    }
};

/**
 * @struct MarkerTemplate
 * @brief A named set of markers that can be saved and applied to sprites.
 */
struct MarkerTemplate {
    QString             name;
    QVector<NamedPoint> points;
};
