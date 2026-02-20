#pragma once

#include <QVector>
#include "models.h"

class QTimer;
class QPushButton;

/**
 * @class AnimationPlaybackService
 * @brief Static service for managing animation playback.
 * 
 * This class provides static methods for controlling animation playback,
 * including frame navigation, play/pause functionality, and timing management.
 */
class AnimationPlaybackService {
public:
    /**
     * @brief Moves to the previous frame in the animation.
     * 
     * @param timelines List of available animation timelines
     * @param selectedTimelineIndex Index of the currently selected timeline
     * @param frameIndex Current frame index (will be updated)
     * @param playing Current playing state (will be updated if needed)
     * @param timer Timer used for animation playback (will be stopped if needed)
     * @param playPauseButton Play/pause button to update (will be updated if needed)
     * @return bool True if frame was changed, false if already at first frame
     */
    static bool prev(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int& frameIndex, bool& playing, QTimer* timer, QPushButton* playPauseButton);

    /**
     * @brief Moves to the next frame in the animation.
     * 
     * @param timelines List of available animation timelines
     * @param selectedTimelineIndex Index of the currently selected timeline
     * @param frameIndex Current frame index (will be updated)
     * @param playing Current playing state (will be updated if needed)
     * @param timer Timer used for animation playback (will be stopped if needed)
     * @param playPauseButton Play/pause button to update (will be updated if needed)
     * @return bool True if frame was changed, false if already at last frame
     */
    static bool next(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int& frameIndex, bool& playing, QTimer* timer, QPushButton* playPauseButton);

    /**
     * @brief Advances the animation by one tick based on FPS.
     * 
     * @param timelines List of available animation timelines
     * @param selectedTimelineIndex Index of the currently selected timeline
     * @param frameIndex Current frame index (will be updated)
     * @return bool True if frame was advanced, false if at end of timeline
     */
    static bool tick(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int& frameIndex);

    /**
     * @brief Toggles play/pause state of the animation.
     * 
     * @param timelines List of available animation timelines
     * @param selectedTimelineIndex Index of the currently selected timeline
     * @param fps Frames per second for playback
     * @param playing Current playing state (will be updated)
     * @param timer Timer used for animation playback (will be started/stopped)
     * @param playPauseButton Play/pause button to update
     * @return bool True if state was toggled, false if no valid timeline selected
     */
    static bool togglePlayPause(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex, int fps, bool& playing, QTimer* timer, QPushButton* playPauseButton);
};
