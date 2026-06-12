#include "SettingsCoordinator.h"

#include "LayoutCanvas.h"
#include "PreviewCanvas.h"
#include "AnimationCanvas.h"

#include <QApplication>
#include <QPalette>
#include <QColor>
#include <QStyle>

void SettingsCoordinator::apply(const AppSettings& settings, LayoutCanvas* canvas, PreviewCanvas* previewView, AnimationCanvas* animCanvas) {
    canvas->setSettings(settings);
    previewView->setSettings(settings);
    if (animCanvas) {
        animCanvas->setSettings(settings);
    }
}

void SettingsCoordinator::applyTheme(const QString& theme) {
    Q_UNUSED(theme);
#ifdef Q_OS_WASM
    return;
#endif
    // Use the system's default theme - don't force any custom palette
    QApplication::setPalette(QApplication::style()->standardPalette());
}
