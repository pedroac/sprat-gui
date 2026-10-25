#pragma once
#include <QString>
#include <QStringList>

/// Frames per second of an animation with no speed of its own.
constexpr int DEFAULT_ANIMATION_FPS = 8;

/// Most frames per second an animation may have, the limit sprat-cli and
/// sprat-lib (SPRAT_FPS_LIMIT) enforce. GIF frame delays are whole hundredths
/// of a second, so faster animations could not be exported as GIF anyway.
constexpr int MAX_ANIMATION_FPS = 100;

/// An fps as it may be stored: 0 or less means unset and becomes the default,
/// and more than MAX_ANIMATION_FPS becomes MAX_ANIMATION_FPS.
/// @param fps Value read from a project file or set by code
/// @return A value within 1..MAX_ANIMATION_FPS
constexpr int sanitizedAnimationFps(int fps) {
    return fps <= 0 ? DEFAULT_ANIMATION_FPS : (fps > MAX_ANIMATION_FPS ? MAX_ANIMATION_FPS : fps);
}

/**
 * @struct AnimationTimeline
 * @brief Represents an animation sequence.
 */
struct AnimationTimeline {
    QString name;
    int fps = DEFAULT_ANIMATION_FPS;
    QStringList frames;
    QString aliasOf;
    bool hFlip = false;
    bool vFlip = false;
};
