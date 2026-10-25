#pragma once
#include <QString>

#include <algorithm>

/// Nine-slice insets in source-image pixels.
struct NineSliceInsets {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    bool operator==(const NineSliceInsets& other) const {
        return left == other.left && top == other.top && right == other.right && bottom == other.bottom;
    }
};

/// Fits insets to a width x height image the way sprat-cli requires them:
/// left + right <= width and top + bottom <= height. Insets that already fit are
/// unchanged; otherwise the right and bottom insets give up the excess, as
/// spratconvert does after rounding scaled insets.
inline NineSliceInsets fitNineSliceInsets(NineSliceInsets insets, int width, int height) {
    width = std::max(width, 0);
    height = std::max(height, 0);
    insets.left = std::clamp(insets.left, 0, width);
    insets.right = std::clamp(insets.right, 0, width - insets.left);
    insets.top = std::clamp(insets.top, 0, height);
    insets.bottom = std::clamp(insets.bottom, 0, height - insets.top);
    return insets;
}


/// The fill modes sprat-cli accepts for a nine-slice's stretchable regions.
inline bool isNineSliceFillMode(const QString& mode) {
    return mode == QLatin1String("stretch") || mode == QLatin1String("repeat")
        || mode == QLatin1String("mirror") || mode == QLatin1String("fixed");
}

/// `mode` when sprat-cli accepts it, otherwise the default `stretch`. Project
/// files are user input, and the mode is written into sprat-cli's layout text.
inline QString nineSliceFillModeOrDefault(const QString& mode) {
    return isNineSliceFillMode(mode) ? mode : QStringLiteral("stretch");
}
