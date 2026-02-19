#pragma once

#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include "PreviewCanvas.h"
#include "models.h"

class QDoubleSpinBox;

class SpriteSelectionPresenter {
public:
    static void applySpriteSelection(
        SpritePtr sprite,
        const QString& selectedPointName,
        QLineEdit* spriteNameEdit,
        QSpinBox* pivotXSpin,
        QSpinBox* pivotYSpin,
        QPushButton* configPointsBtn,
        PreviewCanvas* previewView,
        QDoubleSpinBox* previewZoomSpin,
        QComboBox* handleCombo);

    static void refreshHandleCombo(QComboBox* handleCombo, SpritePtr selectedSprite, const QString& selectedPointName);
};
