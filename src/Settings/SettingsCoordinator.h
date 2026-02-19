#pragma once

#include "models.h"

class LayoutCanvas;
class PreviewCanvas;
class QLabel;

class SettingsCoordinator {
public:
    static void apply(const AppSettings& settings, LayoutCanvas* canvas, PreviewCanvas* previewView, QLabel* animPreviewLabel);
};
