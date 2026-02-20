#pragma once
#include <QString>
#include <QStringView>
#include <QRect>
#include <QVector>
#include <QPoint>
#include <memory>
#include <QColor>

/**
 * @enum MarkerKind
 * @brief Enum representing different types of markers that can be placed on sprites.
 * 
 * This enum defines the supported marker types for sprite editing:
 * - Point: A single point marker
 * - Circle: A circular marker with radius
 * - Rectangle: A rectangular marker with width and height
 * - Polygon: A polygon marker with multiple vertices
 */
enum class MarkerKind {
    Point = 0,
    Circle,
    Rectangle,
    Polygon
};

/**
 * @brief Converts MarkerKind enum to string representation.
 * 
 * @param kind MarkerKind to convert
 * @return QString String representation of the marker kind
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
    return "point"; // Default fallback
}

/**
 * @brief Converts string representation to MarkerKind enum.
 * 
 * @param kind String representation of marker kind
 * @return MarkerKind Corresponding MarkerKind enum value
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
    return MarkerKind::Point; // Default fallback
}

/**
 * @struct NamedPoint
 * @brief Represents a named point or shape on a sprite.
 * 
 * This struct defines a point or shape with a name and position.
 * It supports different marker types including points, circles,
 * rectangles, and polygons.
 */
struct NamedPoint {
    /**
     * @brief Name of the point/marker.
     * 
     * This should be a unique identifier for the point within the sprite.
     */
    QString name;

    /**
     * @brief X coordinate of the point.
     * 
     * Position relative to the sprite's top-left corner.
     */
    int x = 0;

    /**
     * @brief Y coordinate of the point.
     * 
     * Position relative to the sprite's top-left corner.
     */
    int y = 0;

    /**
     * @brief Type of marker.
     * 
     * Defines what kind of marker this point represents.
     */
    MarkerKind kind = MarkerKind::Point;

    /**
     * @brief Radius for circular markers.
     * 
     * Only used when kind is MarkerKind::Circle.
     */
    int radius = 8;

    /**
     * @brief Width for rectangular markers.
     * 
     * Only used when kind is MarkerKind::Rectangle.
     */
    int w = 16;

    /**
     * @brief Height for rectangular markers.
     * 
     * Only used when kind is MarkerKind::Rectangle.
     */
    int h = 16;

    /**
     * @brief Vertices for polygon markers.
     * 
     * Only used when kind is MarkerKind::Polygon.
     * Each QPoint represents a vertex relative to the marker's position.
     */
    QVector<QPoint> polygonPoints;
};

/**
 * @struct Sprite
 * @brief Represents a sprite image and its metadata.
 * 
 * This struct contains all information about a sprite including
 * its file path, dimensions, pivot point, and markers.
 */
struct Sprite {
    /**
     * @brief File path to the sprite image.
     * 
     * Should be an absolute or relative path that can be loaded by QPixmap.
     */
    QString path;

    /**
     * @brief Display name of the sprite.
     * 
     * Used for UI display and identification.
     */
    QString name;

    /**
     * @brief Rectangle defining the sprite's position and size in the atlas.
     * 
     * Coordinates are relative to the atlas's top-left corner.
     */
    QRect rect;

    /**
     * @brief Whether the sprite has been trimmed.
     * 
     * If true, the sprite has been cropped to remove transparent pixels.
     */
    bool trimmed = false;

    /**
     * @brief Rectangle defining the trimmed area.
     * 
     * Stored as x=l, y=t, w=r, h=b where:
     * - x, y: top-left corner of trimmed area
     * - w, h: bottom-right corner of trimmed area
     */
    QRect trimRect;

    /**
     * @brief X coordinate of the pivot point.
     * 
     * Pivot point is used as the origin for rotations and positioning.
     * Value is relative to the sprite's top-left corner.
     */
    int pivotX = 0;

    /**
     * @brief Y coordinate of the pivot point.
     * 
     * Pivot point is used as the origin for rotations and positioning.
     * Value is relative to the sprite's top-left corner.
     */
    int pivotY = 0;

    /**
     * @brief List of markers/points on the sprite.
     * 
     * Each NamedPoint represents a marker or point of interest on the sprite.
     */
    QVector<NamedPoint> points;
};

/**
 * @typedef SpritePtr
 * @brief Shared pointer to Sprite struct.
 * 
 * Using shared_ptr for automatic memory management and shared ownership.
 */
using SpritePtr = std::shared_ptr<Sprite>;

/**
 * @struct LayoutModel
 * @brief Represents the layout of sprites in an atlas.
 * 
 * This struct contains information about how sprites are arranged
 * in a sprite sheet or atlas.
 */
struct LayoutModel {
    /**
     * @brief Width of the atlas in pixels.
     */
    int atlasWidth = 0;

    /**
     * @brief Height of the atlas in pixels.
     */
    int atlasHeight = 0;

    /**
     * @brief Scale factor applied to the layout.
     * 
     * Used for zooming and display purposes.
     */
    double scale = 1.0;

    /**
     * @brief List of sprites in the layout.
     * 
     * Each sprite contains its position and size within the atlas.
     */
    QVector<SpritePtr> sprites;
};

/**
 * @struct AnimationTimeline
 * @brief Represents an animation sequence.
 * 
 * This struct defines a named animation with a specific frame rate
 * and sequence of frames.
 */
struct AnimationTimeline {
    /**
     * @brief Name of the animation.
     * 
     * Used for identification and UI display.
     */
    QString name;

    /**
     * @brief Frames per second for the animation.
     * 
     * Controls the playback speed of the animation.
     */
    int fps = 8;

    /**
     * @brief List of frame paths in sequence.
     * 
     * Each path should point to a valid sprite image file.
     */
    QStringList frames;
};

/**
 * @struct SaveConfig
 * @brief Configuration for saving the project/spritesheet.
 * 
 * This struct contains settings for how the project should be saved,
 * including destination, transformations, and profiles to use.
 */
struct SaveConfig {
    /**
     * @brief Destination path for saving.
     * 
     * Can be a file path or directory path depending on the save operation.
     */
    QString destination;

    /**
     * @brief Transformation to apply during save.
     * 
     * May include format conversions or other processing options.
     */
    QString transform;

    /**
     * @brief List of profiles to use during save.
     * 
     * Profiles define layout and optimization settings.
     */
    QStringList profiles;
};

/**
 * @struct AppSettings
 * @brief Application visual settings.
 * 
 * This struct contains user-configurable visual settings for the application.
 */
struct AppSettings {
    /**
     * @brief Background color of the canvas.
     * 
     * Default: QColor(90, 90, 90) - dark gray
     */
    QColor canvasColor = QColor(90, 90, 90);

    /**
     * @brief Color of sprite frames.
     * 
     * Default: QColor(240, 240, 240) - light gray
     */
    QColor frameColor = QColor(240, 240, 240);

    /**
     * @brief Whether to show borders around sprites.
     * 
     * Default: true
     */
    bool showBorders = true;

    /**
     * @brief Color of borders around sprites.
     * 
     * Default: QColor(86, 86, 86) - medium gray
     */
    QColor borderColor = QColor(86, 86, 86);

    /**
     * @brief Style of borders around sprites.
     * 
     * Default: Qt::SolidLine
     */
    Qt::PenStyle borderStyle = Qt::SolidLine;
};

/**
 * @struct CliPaths
 * @brief Paths to CLI tool binaries.
 * 
 * This struct contains the file paths to the external CLI tools
 * used by the application for layout generation and processing.
 */
struct CliPaths {
    /**
     * @brief Path to spratlayout binary.
     * 
     * Used for generating sprite sheet layouts.
     */
    QString layoutBinary;

    /**
     * @brief Path to spratpack binary.
     * 
     * Used for packing sprites into atlases.
     */
    QString packBinary;

    /**
     * @brief Path to spratconvert binary.
     * 
     * Used for format conversions (optional).
     */
    QString convertBinary;
};
