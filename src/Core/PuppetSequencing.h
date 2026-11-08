#pragma once
#include <QVector>
#include "PuppetEvaluator.h"
#include "PuppetModels.h"

/// Plays a puppet's sequences as sprat-cli's "Playing a puppet" describes: at its fps, each
/// step tweens what its pose sets from where it is to the pose's values over `in` frames
/// (or keeps it and snaps at the end, with ease:step; a step's partSnaps ease their own
/// way), switches variants when the pose
/// arrives, then holds `hold` frames. A loop's first step starts from where the last left
/// off; ping-pong plays the steps back in reverse. Pure: the Puppet workspace's preview.
namespace PuppetSequencing {

/// Frames a step lasts: `in` + `hold`, at least one.
int stepFrames(const PuppetSequenceStep& step);
/// Frames one pass of the steps lasts, forward.
int passFrames(const PuppetSequence& sequence);
/// Frames before it repeats (or, once, stops): a pass, and for ping-pong the way back too.
int cycleFrames(const PuppetSequence& sequence);
/// The frame of the forward pass where step @p stepIndex starts.
int stepStart(const PuppetSequence& sequence, int stepIndex);
/// Where frame @p frame of the playback (any cycle, ping-pong's way back included) is in the
/// forward pass: what a track of the steps shows.
int passFrameOf(const PuppetSequence& sequence, int frame);
/// The step playing at @p frame of the forward pass, or -1.
int stepAt(const PuppetSequence& sequence, int frame);

/// @p base with what @p sequence sets at frame @p frame changed: from the base, the values
/// its poses set, tweened. Other values stay as in @p base. Once modes hold their last frame.
QVector<PuppetPartState> statesAt(const Puppet& puppet, const PuppetSequence& sequence, int frame,
                                  QVector<PuppetPartState> base);

}  // namespace PuppetSequencing
