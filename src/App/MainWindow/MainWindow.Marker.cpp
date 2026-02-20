#include "MainWindow.h"

#include "MarkersDialog.h"

#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QSet>
#include <algorithm>

namespace {
SpritePtr spriteByPath(const LayoutModel& model, const QString& path) {
    for (const auto& sprite : model.sprites) {
        if (sprite && sprite->path == path) {
            return sprite;
        }
    }
    return SpritePtr();
}
}

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

void MainWindow::onApplyPivotToSelectedTimelineFrames() {
    if (!m_selectedSprite) {
        m_statusLabel->setText(tr("Select a source sprite first."));
        return;
    }
    if (m_selectedSprites.isEmpty()) {
        m_statusLabel->setText(tr("Select frames in the layout canvas first."));
        return;
    }

    QSet<QString> targetPaths;
    for (const auto& sprite : m_selectedSprites) {
        if (sprite) {
            targetPaths.insert(sprite->path);
        }
    }

    int updated = 0;
    for (const QString& path : targetPaths) {
        SpritePtr target = spriteByPath(m_layoutModel, path);
        if (!target) {
            continue;
        }
        target->pivotX = m_selectedSprite->pivotX;
        target->pivotY = m_selectedSprite->pivotY;
        ++updated;
    }

    if (updated <= 0) {
        m_statusLabel->setText(tr("No selected frames matched loaded sprites."));
        return;
    }

    if (targetPaths.contains(m_selectedSprite->path)) {
        onCanvasPivotChanged(m_selectedSprite->pivotX, m_selectedSprite->pivotY);
    }
    m_previewView->overlay()->updateLayout();
    m_canvas->update();
    refreshAnimationTest();
    m_statusLabel->setText(tr("Applied pivot to %1 sprite(s).").arg(updated));
}

void MainWindow::onApplyMarkerToSelectedTimelineFrames(const QString& markerName) {
    if (!m_selectedSprite) {
        m_statusLabel->setText(tr("Select a source sprite first."));
        return;
    }
    const QString sourceMarkerName = markerName.trimmed();
    if (sourceMarkerName.isEmpty()) {
        m_statusLabel->setText(tr("Select a marker first."));
        return;
    }
    if (m_selectedSprites.isEmpty()) {
        m_statusLabel->setText(tr("Select frames in the layout canvas first."));
        return;
    }

    auto markerIt = std::find_if(
        m_selectedSprite->points.begin(),
        m_selectedSprite->points.end(),
        [&sourceMarkerName](const NamedPoint& point) { return point.name == sourceMarkerName; });
    if (markerIt == m_selectedSprite->points.end()) {
        m_statusLabel->setText(tr("Selected marker was not found in the source sprite."));
        return;
    }
    const NamedPoint sourceMarker = *markerIt;

    QSet<QString> targetPaths;
    for (const auto& sprite : m_selectedSprites) {
        if (sprite) {
            targetPaths.insert(sprite->path);
        }
    }

    int updated = 0;
    for (const QString& path : targetPaths) {
        SpritePtr target = spriteByPath(m_layoutModel, path);
        if (!target) {
            continue;
        }
        auto targetIt = std::find_if(
            target->points.begin(),
            target->points.end(),
            [&sourceMarkerName](const NamedPoint& point) { return point.name == sourceMarkerName; });
        if (targetIt == target->points.end()) {
            target->points.append(sourceMarker);
        } else {
            *targetIt = sourceMarker;
        }
        ++updated;
    }

    if (updated <= 0) {
        m_statusLabel->setText(tr("No selected frames matched loaded sprites."));
        return;
    }

    m_previewView->overlay()->updateLayout();
    refreshHandleCombo();
    refreshAnimationTest();
    m_statusLabel->setText(tr("Applied marker '%1' to %2 sprite(s).").arg(sourceMarkerName).arg(updated));
}
