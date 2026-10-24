#include "LayoutArguments.h"
#include "ResolutionUtils.h"

#include <cmath>

namespace {
bool isCompactPreset(const QString& preset) {
    const QString p = preset.trimmed().toLower();
    return p == "quality" || p == "small";
}
}

QStringList buildLayoutArguments(const LayoutRunConfig& config) {
    QStringList args;
    if (!config.imagePathList.isEmpty()) {
        args << "--stdin-list";
    } else if (!config.sourceFolderPath.isEmpty()) {
        args << config.sourceFolderPath;
    }

    const SpratProfile& p = config.profile;
    
    // Profile specific arguments
    if (!p.preset.trimmed().isEmpty()) {
        args << "--preset" << p.preset.trimmed();
    }
    if (p.maxWidth > 0) {
        args << "--max-width" << QString::number(p.maxWidth);
    }
    if (p.maxHeight > 0) {
        args << "--max-height" << QString::number(p.maxHeight);
    }
    if (isCompactPreset(p.preset) && p.threads > 0) {
        args << "--threads" << QString::number(p.threads);
    }

    // Scale
    double effectiveScale = config.scale;
    if (std::abs(effectiveScale - 1.0) < 1e-6 && p.scale > 0) {
        effectiveScale = p.scale;
    }
    args << "--scale" << QString::number(effectiveScale, 'g', 12);

    // Resolution
    const bool hasSourceResolution = config.sourceResolutionWidth > 0 && config.sourceResolutionHeight > 0;
    const bool hasTargetResolution = p.targetResolutionUseSource || 
                                     (p.targetResolutionWidth > 0 && p.targetResolutionHeight > 0);

    if (hasSourceResolution && hasTargetResolution) {
        const int targetW = p.targetResolutionUseSource ? config.sourceResolutionWidth : p.targetResolutionWidth;
        const int targetH = p.targetResolutionUseSource ? config.sourceResolutionHeight : p.targetResolutionHeight;
        
        args << "--source-resolution" << formatResolutionText(config.sourceResolutionWidth, config.sourceResolutionHeight);
        args << "--target-resolution" << formatResolutionText(targetW, targetH);
        
        if (!p.resolutionReference.trimmed().isEmpty()) {
            args << "--resolution-reference" << p.resolutionReference.trimmed();
        }
    }

    // Padding & Extrude & Trim & Rotate
    args << "--padding" << QString::number(p.padding);
    if (p.extrude > 0) {
        args << "--extrude" << QString::number(p.extrude);
    }
    
    if (p.trimTransparent && !config.retryWithoutTrim) {
        args << "--trim-transparent";
    }
    if (p.allowRotation) {
        args << "--rotate";
        if (config.rotateCounterClockwise) {
            args << "--rotate-direction" << "ccw";
        }
    }
    if (p.multipack) {
        args << "--multipack";
    }
    if (!p.sort.trimmed().isEmpty()) {
        args << "--sort" << p.sort.trimmed();
    }

    if (!config.deduplicateMode.isEmpty() && config.deduplicateMode != "none") {
        args << "--deduplicate" << config.deduplicateMode;
        if (config.deduplicateMode == "perceptual" && config.dedupThreshold != 5) {
            args << "--dedup-threshold" << QString::number(config.dedupThreshold);
        }
    }
    if (config.incrementalLayout) {
        args << "--incremental";
    }

    return args;
}
