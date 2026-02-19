#include "MainWindow.h"

#include "MarkersDialog.h"

#include <QComboBox>
#include <QLabel>
#include <QSpinBox>

void MainWindow::onPreviewZoomChanged(double value) {
    m_previewView->setZoom(value);
}

void MainWindow::onPivotSpinChanged() {
    if (!m_selectedSprite) {
        return;
    }
    m_selectedSprite->pivotX = m_pivotXSpin->value();
    m_selectedSprite->pivotY = m_pivotYSpin->value();
    m_previewView->overlay()->updateLayout();
}

void MainWindow::onCanvasPivotChanged(int x, int y) {
    m_pivotXSpin->blockSignals(true);
    m_pivotYSpin->blockSignals(true);
    m_pivotXSpin->setValue(x);
    m_pivotYSpin->setValue(y);
    m_pivotXSpin->blockSignals(false);
    m_pivotYSpin->blockSignals(false);
}

void MainWindow::onHandleComboChanged(int index) {
    if (index <= 0) {
        m_selectedPointName.clear();
        if (m_previewView && m_previewView->overlay()) {
            m_previewView->overlay()->setSelectedMarker("");
        }
    } else {
        m_selectedPointName = m_handleCombo->itemText(index);
        if (m_previewView && m_previewView->overlay()) {
            m_previewView->overlay()->setSelectedMarker(m_selectedPointName);
        }
    }

    m_statusLabel->setText(m_selectedPointName.isEmpty()
        ? tr("Selected: ") + (m_selectedSprite ? m_selectedSprite->name : tr("none"))
        : tr("Selected Marker: ") + m_selectedPointName);
}

void MainWindow::onPointsConfigClicked() {
    if (!m_selectedSprite) {
        return;
    }
    MarkersDialog dlg(m_selectedSprite, this);
    connect(&dlg, &MarkersDialog::markersChanged, this, [this]() {
        m_previewView->overlay()->updateLayout();
        refreshHandleCombo();
    });
    dlg.exec();
}

void MainWindow::onMarkerSelectedFromCanvas(const QString& name) {
    m_selectedPointName = name;
    if (!name.isEmpty()) {
        m_statusLabel->setText(tr("Selected Marker: ") + name);
        const int idx = m_handleCombo->findText(name);
        if (idx != -1) {
            m_handleCombo->blockSignals(true);
            m_handleCombo->setCurrentIndex(idx);
            m_handleCombo->blockSignals(false);
        }
        return;
    }
    m_statusLabel->setText(tr("Selected: ") + m_selectedSprite->name);
    m_handleCombo->blockSignals(true);
    m_handleCombo->setCurrentIndex(0);
    m_handleCombo->blockSignals(false);
}

void MainWindow::onMarkerChangedFromCanvas() {
    m_previewView->overlay()->update();
    if (m_selectedPointName.isEmpty() && m_selectedSprite) {
        onCanvasPivotChanged(m_selectedSprite->pivotX, m_selectedSprite->pivotY);
    }
}
