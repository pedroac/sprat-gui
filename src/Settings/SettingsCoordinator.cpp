#include "SettingsCoordinator.h"

#include "LayoutCanvas.h"
#include "PreviewCanvas.h"
#include "AnimationCanvas.h"

void SettingsCoordinator::apply(const AppSettings& settings, LayoutCanvas* canvas, PreviewCanvas* previewView, AnimationCanvas* animCanvas) {
    canvas->setSettings(settings);
    previewView->setSettings(settings);
    if (animCanvas) {
        animCanvas->setBackgroundBrush(settings.canvasColor);
    }
}
