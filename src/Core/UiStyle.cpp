#include "UiStyle.h"

#include <QTabBar>
#include <QVariant>
#include <QWidget>

namespace {

constexpr char ROLE_PROPERTY[] = "spratRole";
constexpr int DARK_THEME_LIGHTNESS = 128;

}  // namespace

namespace UiStyle {

void setRole(QWidget* widget, Role role) {
    if (!widget) return;
    widget->setProperty(ROLE_PROPERTY, static_cast<int>(role));
    widget->update();
}

Role role(const QWidget* widget) {
    if (!widget) return Role::None;
    const QVariant value = widget->property(ROLE_PROPERTY);
    return value.isValid() ? static_cast<Role>(value.toInt()) : Role::None;
}

void setTabRole(QTabBar* tabBar, int index, Role role) {
    if (!tabBar || index < 0 || index >= tabBar->count()) return;
    tabBar->setTabData(index, static_cast<int>(role));
    tabBar->update();
}

Role tabRole(const QTabBar* tabBar, int index) {
    if (!tabBar || index < 0 || index >= tabBar->count()) return Role::None;
    const QVariant value = tabBar->tabData(index);
    return value.typeId() == QMetaType::Int ? static_cast<Role>(value.toInt()) : Role::None;
}

QColor roleColor(Role role, const QPalette& palette) {
    const bool dark = palette.color(QPalette::Window).lightness() < DARK_THEME_LIGHTNESS;
    switch (role) {
    case Role::Add:         return dark ? QColor(0x2f, 0x4d, 0x36) : QColor(0xc2, 0xe8, 0xc6);
    case Role::Destructive: return dark ? QColor(0x5a, 0x33, 0x36) : QColor(0xf6, 0xcb, 0xc8);
    case Role::Accent:      return dark ? QColor(0x2c, 0x44, 0x60) : QColor(0xc8, 0xdc, 0xf6);
    case Role::None:        break;
    }
    return palette.color(QPalette::Button);
}

}  // namespace UiStyle
