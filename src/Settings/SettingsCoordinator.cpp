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
#ifdef Q_OS_WASM
    Q_UNUSED(theme);
    return;
#endif
    if (theme == "dark") {
        QPalette p;
        p.setColor(QPalette::Window,          QColor(53, 53, 53));
        p.setColor(QPalette::WindowText,      QColor(220, 220, 220));
        p.setColor(QPalette::Base,            QColor(42, 42, 42));
        p.setColor(QPalette::AlternateBase,   QColor(66, 66, 66));
        p.setColor(QPalette::ToolTipBase,     QColor(42, 42, 42));
        p.setColor(QPalette::ToolTipText,     QColor(220, 220, 220));
        p.setColor(QPalette::Text,            QColor(220, 220, 220));
        p.setColor(QPalette::Button,          QColor(53, 53, 53));
        p.setColor(QPalette::ButtonText,      QColor(220, 220, 220));
        p.setColor(QPalette::BrightText,      Qt::red);
        p.setColor(QPalette::Link,            QColor(42, 130, 218));
        p.setColor(QPalette::Highlight,       QColor(42, 130, 218));
        p.setColor(QPalette::HighlightedText, Qt::black);
        p.setColor(QPalette::Mid,             QColor(66, 66, 66));
        p.setColor(QPalette::Disabled, QPalette::Text,       QColor(100, 100, 100));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(100, 100, 100));
        QApplication::setPalette(p);
    } else {
        QApplication::setPalette(QApplication::style()->standardPalette());
    }
}
