#pragma once
#include <QColor>
#include <QPalette>

class QTabBar;
class QWidget;

/// Colour roles for buttons and tabs: soft pastel fills (muted in dark themes) that tell what an
/// action does at a glance. The app's style (SpratStyle in main.cpp) paints them; a widget only
/// carries its role, so the colours follow theme changes.
namespace UiStyle {

enum class Role {
    None,
    Add,          ///< Adds items (green): the way into an empty list
    Destructive,  ///< Deletes or removes items (red)
    Accent,       ///< Stands apart, such as the Exportation tab (blue)
};

/// Gives a button (QPushButton or QToolButton) a colour role.
void setRole(QWidget* widget, Role role);

/// The role setRole() gave widget, Role::None if none.
Role role(const QWidget* widget);

/// Gives a tab bar's tab a colour role (kept in the tab's data, which the bar mustn't use otherwise).
void setTabRole(QTabBar* tabBar, int index, Role role);

/// The role setTabRole() gave the tab, Role::None if none.
Role tabRole(const QTabBar* tabBar, int index);

/// The fill for role in palette's theme: a light pastel, or a muted dark tone in a dark theme.
QColor roleColor(Role role, const QPalette& palette);

}  // namespace UiStyle
