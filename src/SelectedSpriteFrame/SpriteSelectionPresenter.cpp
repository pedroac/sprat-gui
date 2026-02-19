#include "SpriteSelectionPresenter.h"

#include <QDoubleSpinBox>

void SpriteSelectionPresenter::applySpriteSelection(
    SpritePtr sprite,
    const QString& selectedPointName,
    QLineEdit* spriteNameEdit,
    QSpinBox* pivotXSpin,
    QSpinBox* pivotYSpin,
    QPushButton* configPointsBtn,
    PreviewCanvas* previewView,
    QDoubleSpinBox* previewZoomSpin,
    QComboBox* handleCombo) {
    if (sprite) {
        spriteNameEdit->setText(sprite->name);
        spriteNameEdit->setEnabled(true);
        pivotXSpin->setValue(sprite->pivotX);
        pivotXSpin->setEnabled(true);
        pivotYSpin->setValue(sprite->pivotY);
        pivotYSpin->setEnabled(true);
        previewView->setSprites({sprite});
        previewView->setZoom(previewZoomSpin->value());
        previewView->centerContent();
        configPointsBtn->setEnabled(true);
    } else {
        spriteNameEdit->clear();
        spriteNameEdit->setEnabled(false);
        pivotXSpin->setEnabled(false);
        pivotYSpin->setEnabled(false);
        previewView->setSprites({});
        configPointsBtn->setEnabled(false);
    }
    refreshHandleCombo(handleCombo, sprite, selectedPointName);
}

void SpriteSelectionPresenter::refreshHandleCombo(QComboBox* handleCombo, SpritePtr selectedSprite, const QString& selectedPointName) {
    handleCombo->blockSignals(true);
    handleCombo->clear();
    handleCombo->addItem("Pivot");
    if (selectedSprite) {
        for (const auto& p : selectedSprite->points) {
            handleCombo->addItem(p.name);
        }
    }
    if (selectedPointName.isEmpty()) {
        handleCombo->setCurrentIndex(0);
    } else {
        int idx = handleCombo->findText(selectedPointName);
        handleCombo->setCurrentIndex(idx != -1 ? idx : 0);
    }
    handleCombo->blockSignals(false);
}
