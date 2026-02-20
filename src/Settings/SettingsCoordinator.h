#pragma once

#include "models.h"

class LayoutCanvas;
class PreviewCanvas;
class QLabel;

/**
 * @class SettingsCoordinator
 * @brief Coordinates application settings across UI components.
 * 
 * This class provides static methods for applying application settings
 * to various UI components, ensuring consistent visual appearance
 * and behavior across the application.
 */
class SettingsCoordinator {
public:
    /**
     * @brief Applies settings to UI components.
     * 
     * This method applies the given settings to the layout canvas,
     * preview canvas, and animation preview label, ensuring consistent
     * visual appearance across the application.
     * 
     * @param settings Application settings to apply
     * @param canvas Layout canvas to update
     * @param previewView Preview canvas to update
     * @param animPreviewLabel Animation preview label to update
     */
    static void apply(const AppSettings& settings, LayoutCanvas* canvas, PreviewCanvas* previewView, QLabel* animPreviewLabel);
};
