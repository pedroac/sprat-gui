#include "SettingsCoordinator.h"

#include <QLabel>
#include "LayoutCanvas.h"
#include "PreviewCanvas.h"

void SettingsCoordinator::apply(const AppSettings& settings, LayoutCanvas* canvas, PreviewCanvas* previewView, QLabel* animPreviewLabel) {
    canvas->setSettings(settings);
    previewView->setSettings(settings);
    QString qss = QString("border: 1px solid #565656; background: %1;").arg(settings.frameColor.name());
    animPreviewLabel->setStyleSheet(qss);
}
