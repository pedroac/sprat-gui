#pragma once

#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include "PreviewCanvas.h"
#include "models.h"

class QDoubleSpinBox;

class SpriteSelectionPresenter {
public:
    struct SpriteEditorWidgets {
        QLineEdit*      nameEdit        = nullptr;
        QDoubleSpinBox* pivotXSpin      = nullptr;
        QDoubleSpinBox* pivotYSpin      = nullptr;
        QPushButton*    configPointsBtn = nullptr;
        PreviewCanvas*  previewView     = nullptr;
        QDoubleSpinBox* previewZoomSpin = nullptr;
        QComboBox*      handleCombo     = nullptr;
    };

    static void applySpriteSelection(
        SpritePtr sprite,
        const QString& selectedPointName,
        const SpriteEditorWidgets& widgets,
        FrameZoomMode zoomMode = FrameZoomMode::Fit);

    static void refreshHandleCombo(QComboBox* handleCombo, SpritePtr selectedSprite, const QString& selectedPointName);
};
