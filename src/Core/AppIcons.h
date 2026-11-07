#pragma once
#include <QIcon>
#include <QString>

/// The app's icons (assets/icons): one set, 24x24 outlines with 2 px round strokes, drawn in
/// "currentColor". icon() draws them in the palette's text colour, so they follow light and
/// dark themes, highlighted text when selected and the disabled colour when disabled. Colours
/// written in an SVG (warning amber, error red) are kept.
namespace AppIcons {

/// The icon assets/icons/@p name.svg, e.g. icon("save"). Null for an unknown name.
QIcon icon(const QString& name);

}  // namespace AppIcons
