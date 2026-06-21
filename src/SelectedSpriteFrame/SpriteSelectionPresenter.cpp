#include "SpriteSelectionPresenter.h"
#include "MarkerUtils.h"

#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QTimer>

void SpriteSelectionPresenter::applySpriteSelection(
    SpritePtr sprite,
    const QString& selectedPointName,
    const SpriteEditorWidgets& widgets,
    FrameZoomMode zoomMode) {
    if (sprite) {
        widgets.nameEdit->blockSignals(true);
        widgets.nameEdit->setText(sprite->name);
        widgets.nameEdit->blockSignals(false);
        widgets.nameEdit->setEnabled(true);
        widgets.pivotXSpin->blockSignals(true);
        widgets.pivotXSpin->setValue(sprite->pivotX);
        widgets.pivotXSpin->blockSignals(false);
        widgets.pivotXSpin->setEnabled(true);
        widgets.pivotYSpin->blockSignals(true);
        widgets.pivotYSpin->setValue(sprite->pivotY);
        widgets.pivotYSpin->blockSignals(false);
        widgets.pivotYSpin->setEnabled(true);
        widgets.previewView->setSprites({sprite});
        if (zoomMode == FrameZoomMode::Fit) {
            widgets.previewView->setZoomManual(false);
            QTimer::singleShot(0, widgets.previewView, &PreviewCanvas::initialFit);
        } else if (zoomMode == FrameZoomMode::Reset100) {
            widgets.previewView->setZoomManual(true);
            widgets.previewView->setZoom(1.0);
            widgets.previewView->centerContent();
        } else { // Keep
            widgets.previewView->setZoomManual(true);
            widgets.previewView->setZoom(widgets.previewZoomSpin->value() / 100.0);
            widgets.previewView->centerContent();
        }
        widgets.configPointsBtn->setEnabled(true);
    } else {
        widgets.nameEdit->blockSignals(true);
        widgets.nameEdit->clear();
        widgets.nameEdit->blockSignals(false);
        widgets.nameEdit->setEnabled(false);
        widgets.pivotXSpin->setEnabled(false);
        widgets.pivotYSpin->setEnabled(false);
        widgets.previewView->setSprites({});
        widgets.configPointsBtn->setEnabled(false);
    }
    refreshHandleCombo(widgets.handleCombo, sprite, selectedPointName);
}

void SpriteSelectionPresenter::refreshHandleCombo(QComboBox* handleCombo, SpritePtr selectedSprite, const QString& selectedPointName) {
    handleCombo->blockSignals(true);
    handleCombo->clear();
    handleCombo->addItem(markerKindIcon(MarkerKind::Point), QCoreApplication::translate("SpriteSelectionPresenter", "pivot"));
    if (selectedSprite) {
        for (const auto& p : selectedSprite->points) {
            handleCombo->addItem(markerKindIcon(p.kind), p.name);
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
