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
    int dedupThreshold = 5;
    bool incrementalLayout = false;
    bool framesHasRectangles = false;
    QString framesRectangleColor;         // Empty = use CLI default (255,0,255)
    int framesTolerance = 1;
    int framesMinSize = 4;
    int framesMaxSprites = 10000;
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

    // Nine-Slice Editor
    NineSliceZoomOnChange nineSliceZoomOnChange = NineSliceZoomOnChange::FitToFrame;
    QColor nineSliceLineColor = QColor(255, 80, 80, 200);
    Qt::PenStyle nineSliceLineStyle = Qt::DashLine;
    QColor nineSliceLabelColor = QColor(255, 255, 255, 220);
    QColor nineSliceResizeHandleColor = QColor(0xaa, 0xaa, 0xaa, 160);
    QColor nineSliceCornerColor = QColor(80, 120, 200);
    QColor nineSliceCenterColor = QColor(200, 200, 80);
    QColor nineSliceEdgeLRColor = QColor(80, 200, 120);
    QColor nineSliceEdgeTBColor = QColor(80, 200, 120);
    int nineSliceOverlayOpacity = 25;

    // Puppets (canvas sizes are in screen pixels)
    int puppetPivotRingRadius = 7;          ///< The selected part's pivot ring: dragged from inside to attach
    int puppetPivotDoubleClickRadius = 10;  ///< How far from the pivot a double-click hangs the part
    int puppetTurnHandleDistance = 36;      ///< From the pivot to the turn handle; dragging within moves the part
    int puppetTurnHandleRadius = 5;
    int puppetMarkerSnapDistance = 12;      ///< A dragged pivot ring snaps to markers this near
    int puppetDragDistance = 0;             ///< Pointer travel before a drag starts; 0 = the system's
    int puppetPivotCrossSize = 8;           ///< Half the selected part's pivot cross
    int puppetOriginCrossSize = 24;         ///< Half the puppet's origin cross
    double puppetMarkerDotRadius = 3.5;
    double puppetHandleLineWidth = 1.5;
    QColor puppetHandleColor = QColor(61, 174, 233);  ///< Pivot ring and cross, angle line, turn handle
    QColor puppetRangeColor = QColor(255, 210, 0);    ///< Markers, the joint's area, the turn circle
    QColor puppetOriginColor = QColor(255, 255, 255);
    QColor puppetOnionBeforeColor = QColor(220, 50, 50);
    QColor puppetOnionAfterColor = QColor(40, 170, 70);
    int puppetOnionSkinOpacity = 30;        ///< Percent
    int puppetFadedPartOpacity = 35;        ///< Percent: parts the pose leaves alone
    int puppetStepIn = 4;                   ///< Frames a new animation step takes to reach its pose
    int puppetStepHold = 0;                 ///< Frames a new animation step holds its pose
    int puppetAnimationFps = 8;             ///< A new animation's frames per second
};
