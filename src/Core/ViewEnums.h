#pragma once
#include <QString>

/**
 * @enum CoordUnit
 * @brief Unit for displaying and editing pivot/marker coordinates.
 */
enum class CoordUnit { Pixels, Percent };

/**
 * @enum FrameZoomMode
 * @brief Controls how the preview zoom behaves when navigating between frames.
 */
enum class FrameZoomMode { Fit, Keep, Reset100 };

inline QString frameZoomModeToString(FrameZoomMode m) {
    switch (m) {
    case FrameZoomMode::Keep:     return "keep";
    case FrameZoomMode::Reset100: return "reset_100";
    default:                      return "fit";
    }
}

inline FrameZoomMode frameZoomModeFromString(const QString& s) {
    const QString n = s.trimmed().toLower();
    if (n == "keep")      return FrameZoomMode::Keep;
    if (n == "reset_100") return FrameZoomMode::Reset100;
    return FrameZoomMode::Fit;
}

/**
 * @enum FlipbookMode
 * @brief Controls how the frame editor keeps the pivot in a fixed viewport position.
 */
enum class FlipbookMode { None, SameGroup, All };

inline QString flipbookModeToString(FlipbookMode mode) {
    switch (mode) {
    case FlipbookMode::SameGroup: return "same_group";
    case FlipbookMode::All:       return "all";
    default:                      return "none";
    }
}

inline FlipbookMode flipbookModeFromString(const QString& s) {
    const QString n = s.trimmed().toLower();
    if (n == "same_group") return FlipbookMode::SameGroup;
    if (n == "all")        return FlipbookMode::All;
    return FlipbookMode::None;
}

/**
 * @enum LayoutZoomOnChange
 * @brief Controls how the atlas layout canvas zoom behaves after each layout rebuild.
 */
enum class LayoutZoomOnChange { NoChange, Fit, Reset100 };

inline QString layoutZoomOnChangeToString(LayoutZoomOnChange m) {
    switch (m) {
    case LayoutZoomOnChange::Fit:      return "fit";
    case LayoutZoomOnChange::Reset100: return "reset_100";
    default:                           return "no_change";
    }
}

inline LayoutZoomOnChange layoutZoomOnChangeFromString(const QString& s) {
    const QString n = s.trimmed().toLower();
    if (n == "fit")       return LayoutZoomOnChange::Fit;
    if (n == "reset_100") return LayoutZoomOnChange::Reset100;
    return LayoutZoomOnChange::NoChange;
}

/**
 * @enum LayoutLabelMode
 * @brief Controls what text is shown on sprite labels in the atlas layout canvas.
 */
enum class LayoutLabelMode { Name, FullPath, None };

inline QString layoutLabelModeToString(LayoutLabelMode m) {
    switch (m) {
    case LayoutLabelMode::FullPath: return "full_path";
    case LayoutLabelMode::None:     return "none";
    default:                        return "name";
    }
}

inline LayoutLabelMode layoutLabelModeFromString(const QString& s) {
    const QString n = s.trimmed().toLower();
    if (n == "full_path") return LayoutLabelMode::FullPath;
    if (n == "none")      return LayoutLabelMode::None;
    return LayoutLabelMode::Name;
}

/**
 * @enum ExportZoomOnChange
 * @brief Controls how the export preview canvas zoom behaves after each preview update.
 */
enum class ExportZoomOnChange { Fit, NoChange, Reset100 };

inline QString exportZoomOnChangeToString(ExportZoomOnChange m) {
    switch (m) {
    case ExportZoomOnChange::NoChange:  return "no_change";
    case ExportZoomOnChange::Reset100:  return "reset_100";
    default:                            return "fit";
    }
}

inline ExportZoomOnChange exportZoomOnChangeFromString(const QString& s) {
    const QString n = s.trimmed().toLower();
    if (n == "no_change") return ExportZoomOnChange::NoChange;
    if (n == "reset_100") return ExportZoomOnChange::Reset100;
    return ExportZoomOnChange::Fit;
}

/**
 * @enum NineSliceZoomOnChange
 * @brief Controls zoom behavior when selecting a nine-slice definition.
 */
enum class NineSliceZoomOnChange { FitToFrame, Reset100, NoChange };

inline QString nineSliceZoomOnChangeToString(NineSliceZoomOnChange m) {
    switch (m) {
    case NineSliceZoomOnChange::Reset100: return "reset_100";
    case NineSliceZoomOnChange::NoChange: return "no_change";
    default:                              return "fit";
    }
}

inline NineSliceZoomOnChange nineSliceZoomOnChangeFromString(const QString& s) {
    const QString n = s.trimmed().toLower();
    if (n == "reset_100") return NineSliceZoomOnChange::Reset100;
    if (n == "no_change") return NineSliceZoomOnChange::NoChange;
    return NineSliceZoomOnChange::FitToFrame;
}
