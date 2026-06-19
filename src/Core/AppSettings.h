#pragma once
#include <QString>
#include <QStringList>
#include <QColor>
#include <Qt>
#include "SyncMode.h"
#include "ViewEnums.h"

/**
 * @struct AppSettings
 * @brief Application visual settings.
 */
struct AppSettings {
    QColor workspaceColor = QColor(90, 90, 90);
    QColor spriteFrameColor = QColor("#ffffff");
    bool showCheckerboard = true;
    bool showBorders = true;
    QColor borderColor = QColor(86, 86, 86);
    QColor detectionSelectedColor = Qt::green;
    Qt::PenStyle borderStyle = Qt::DashLine;
    QString deduplicateMode = "exact";
    SyncMode syncMode = SyncMode::Watch;
    QString defaultProjectsFolder;
    QStringList recentProjects;
    QString theme = "system";
    int onionSkinOpacity = 30;
    bool propagateEditsToChecked = true;
    CoordUnit coordUnit = CoordUnit::Pixels;
    bool showTrimRect = true;
    QColor trimRectColor = QColor(255, 140, 0);
    Qt::PenStyle trimRectStyle = Qt::DashLine;
    FlipbookMode flipbookMode = FlipbookMode::None;
    FrameZoomMode frameZoomMode = FrameZoomMode::Fit;
    LayoutZoomOnChange layoutZoomOnChange = LayoutZoomOnChange::NoChange;
    LayoutLabelMode layoutLabelMode = LayoutLabelMode::Name;
    ExportZoomOnChange exportZoomOnChange = ExportZoomOnChange::Fit;
    QString exportDefaultOutputFolder;
    QString exportDefaultFormat = "none";
    QString exportDefaultScaleFilter = "nearest";
    bool spritePreviewEnabled = true;
    double spritePreviewDelay = 0.4;
    bool navigatorGroupSimilar = true;
    bool showGrid = false;
    int gridCellWidth = 16;
    int gridCellHeight = 16;
    int gridOffsetX = 0;
    int gridOffsetY = 0;
    QColor gridColor = QColor(255, 255, 255, 80);
};
