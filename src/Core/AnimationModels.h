#pragma once
#include <optional>
#include <QString>
#include <QStringList>
#include <QVector>

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

/// How an animation plays, matching sprat-cli's loop modes.
enum class AnimationLoopMode {
    Loop,         ///< Repeats from the first frame (the default)
    Once,         ///< Plays once and holds the last frame
    PingPong,     ///< Plays forward then backward, and repeats
    OncePingPong  ///< Plays forward then backward once, and holds the first frame
};

/// The name sprat-cli's animations file and the project file use for a loop mode.
inline QString animationLoopModeName(AnimationLoopMode mode) {
    switch (mode) {
    case AnimationLoopMode::Once:         return QStringLiteral("once");
    case AnimationLoopMode::PingPong:     return QStringLiteral("pingpong");
    case AnimationLoopMode::OncePingPong: return QStringLiteral("once-pingpong");
    case AnimationLoopMode::Loop:         break;
    }
    return QStringLiteral("loop");
}

/// The loop mode with a name from animationLoopModeName(), or nullopt for an unknown name.
inline std::optional<AnimationLoopMode> animationLoopModeFromName(const QString& name) {
    for (const auto mode : {AnimationLoopMode::Loop, AnimationLoopMode::Once,
                            AnimationLoopMode::PingPong, AnimationLoopMode::OncePingPong}) {
        if (name == animationLoopModeName(mode)) {
            return mode;
        }
    }
    return std::nullopt;
}

/// Whether the animation starts over after its last step instead of stopping.
inline bool animationLoopModeRepeats(AnimationLoopMode mode) {
    return mode == AnimationLoopMode::Loop || mode == AnimationLoopMode::PingPong;
}

/// The frame indexes, in play order, of one pass of an animation with @p frameCount frames:
/// a ping-pong pass adds the way back (0 1 2 1 for pingpong, since the repeat starts on 0,
/// and 0 1 2 1 0 for once-pingpong).
inline QVector<int> animationPlayOrder(int frameCount, AnimationLoopMode mode) {
    QVector<int> order;
    for (int i = 0; i < frameCount; ++i) {
        order.append(i);
    }
    if (mode == AnimationLoopMode::PingPong || mode == AnimationLoopMode::OncePingPong) {
        const int lastBackward = mode == AnimationLoopMode::PingPong ? 1 : 0;
        for (int i = frameCount - 2; i >= lastBackward; --i) {
            order.append(i);
        }
    }
    return order;
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
    /// An alias plays the way its source does, so its own mode is not used.
    AnimationLoopMode loopMode = AnimationLoopMode::Loop;
};

/// The timeline whose frames, speed and loop mode @p index plays: an alias's source
/// (a regular timeline named by its aliasOf), or the timeline itself.
/// @param timelines The atlas's timelines
/// @param index A valid index into @p timelines
inline const AnimationTimeline& aliasSourceTimeline(const QVector<AnimationTimeline>& timelines,
                                                    int index) {
    const AnimationTimeline& timeline = timelines[index];
    if (!timeline.aliasOf.isEmpty()) {
        for (const AnimationTimeline& source : timelines) {
            if (source.name == timeline.aliasOf && source.aliasOf.isEmpty())
                return source;
        }
    }
    return timeline;
}
