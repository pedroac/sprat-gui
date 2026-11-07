#pragma once
#include <QVector>
#include "PuppetEvaluator.h"
#include "PuppetModels.h"

/// From a pose, as sprat-cli stores it (what it sets, part by part), to the states the
/// evaluator places parts with. Pure.
namespace PuppetPosing {

/// The states a pose gives on its own, from the rest pose: what it sets, rest elsewhere.
/// Settings for unknown parts, and variants a part doesn't have, are left at rest.
QVector<PuppetPartState> statesOf(const Puppet& puppet, const PuppetPose& pose);

}  // namespace PuppetPosing
