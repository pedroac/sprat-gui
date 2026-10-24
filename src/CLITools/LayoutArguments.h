#pragma once

#include <QString>
#include <QStringList>
#include "SpratProfilesConfig.h"

struct LayoutRunConfig {
    // Provide exactly one of the two input fields:
    QString sourceFolderPath; // Pass folder (or list file) as a positional argument (fast path)
    QStringList imagePathList; // Pass image list via --stdin-list
    QString layoutBinary;
    SpratProfile profile;
    double scale = 1.0;
    int sourceResolutionWidth = 0;
    int sourceResolutionHeight = 0;
    bool retryWithoutTrim = false;
    bool rotateCounterClockwise = false; // For formats such as libGDX
    QString deduplicateMode = "none";
    int dedupThreshold = 5;
    bool incrementalLayout = false;
};

/// Builds the spratlayout arguments for a run. The layout canvas, the export and the
/// export preview all use it, so they lay sprites out the same way.
/// @param config Input, profile and options of the run
/// @return Arguments for spratlayout, input first
QStringList buildLayoutArguments(const LayoutRunConfig& config);
