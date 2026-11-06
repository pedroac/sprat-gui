#pragma once

#include "PathSafety.h"
#include "ProjectModels.h"

/// Returns the output subfolder for an atlas, deriving one from the atlas name when
/// several atlases would otherwise all write to the export root.
/// @param rootExporterCount Number of exported atlases without an explicit subfolder
inline QString effectiveAtlasOutputSubdir(const AtlasEntry& atlas, int rootExporterCount) {
    if (!atlas.outputSubdir.isEmpty())
        return atlas.outputSubdir;
    if (rootExporterCount <= 1)
        return {};
    if (atlas.isNeutral)
        return QStringLiteral("sprites");
    return PathSafety::folderNameFromLabel(atlas.name);
}
