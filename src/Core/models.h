#pragma once
#include <QString>
#include <QStringView>
#include <QRect>
#include <QVector>
#include <QPoint>
#include <memory>
#include <QColor>

enum class MarkerKind {
    Point = 0,
    Circle,
    Rectangle,
    Polygon
};

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
};

/**
 * @brief Represents a sprite image and its metadata.
 */
struct Sprite {
    QString path;
    QString name;
    QRect rect;
    bool trimmed = false;
    QRect trimRect; // Stored as x=l, y=t, w=r, h=b
    
    int pivotX = 0;
    int pivotY = 0;
    QVector<NamedPoint> points;
};

using SpritePtr = std::shared_ptr<Sprite>;

/**
 * @brief Represents the layout of sprites in an atlas.
 */
struct LayoutModel {
    int atlasWidth = 0;
    int atlasHeight = 0;
    double scale = 1.0;
    QVector<SpritePtr> sprites;
};

/**
 * @brief Represents an animation sequence.
 */
struct AnimationTimeline {
    QString name;
    int fps = 8;
    QStringList frames; // Paths to sprite images
};

/**
 * @brief Configuration for saving the project/spritesheet.
 */
struct SaveConfig {
    QString destination;
    QString transform;
    QStringList profiles;
};

/**
 * @brief Application visual settings.
 */
struct AppSettings {
    QColor canvasColor = QColor(90, 90, 90);
    QColor frameColor = QColor(240, 240, 240);
    bool showBorders = true;
    QColor borderColor = QColor(86, 86, 86);
    Qt::PenStyle borderStyle = Qt::SolidLine;
};

struct CliPaths {
    QString layoutBinary;
    QString packBinary;
    QString convertBinary;
};
