#include "MainWindow.h"

#include "MarkersDialog.h"

#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QSet>
#include <QImageReader>
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
    if (!m_previewZoomSpin->signalsBlocked()) {
        m_previewView->setZoomManual(true);
    }
    m_previewView->setZoom(value / 100.0);
}

void MainWindow::onPivotSpinChanged() {
    if (!m_session->selectedSprite) {
        return;
    }
    m_session->selectedSprite->pivotX = m_pivotXSpin->value();
    m_session->selectedSprite->pivotY = m_pivotYSpin->value();
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
        m_session->selectedPointName.clear();
        if (m_previewView && m_previewView->overlay()) {
            m_previewView->overlay()->setSelectedMarker("");
        }
    } else {
        m_session->selectedPointName = m_handleCombo->itemText(index);
        if (m_previewView && m_previewView->overlay()) {
            m_previewView->overlay()->setSelectedMarker(m_session->selectedPointName);
        }
    }

    m_statusLabel->setText(m_session->selectedPointName.isEmpty()
        ? tr("Selected: ") + (m_session->selectedSprite ? m_session->selectedSprite->name : tr("none"))
        : tr("Selected Marker: ") + m_session->selectedPointName);
}

void MainWindow::onPointsConfigClicked() {
    if (!m_session->selectedSprite) {
        return;
    }

    SuggestedMarkerPosition suggestion;
    if (m_previewView && m_previewView->scene()) {
        // Find visible area in scene coordinates
        QRect viewportRect = m_previewView->viewport()->rect();
        QRectF visibleRectInScene = m_previewView->mapToScene(viewportRect).boundingRect();

        // Get actual image dimensions
        QSize imgSize = QImageReader(m_session->selectedSprite->path).size();
        if (!imgSize.isValid()) {
            imgSize = m_session->selectedSprite->rect.size();
        }
        QRectF imageRect(0, 0, imgSize.width(), imgSize.height());

        // Find intersection of visible area and image
        QRectF visiblePart = imageRect.intersected(visibleRectInScene);
        
        double zoom = m_previewView->transform().m11();
        if (zoom <= 1e-9) zoom = 1.0;
        
        // Target ~80 pixels on screen for the marker size
        double targetSceneSize = 80.0 / zoom;

        if (!visiblePart.isEmpty()) {
            // Use center of visible part of the image
            suggestion.pos = visiblePart.center().toPoint();
            // Size: target 80px on screen, but don't let it exceed 50% of the visible part's smaller dimension
            double maxSafeSize = qMin(visiblePart.width(), visiblePart.height()) * 0.5;
            suggestion.baseSize = qMax(8, qRound(qMin(targetSceneSize, maxSafeSize > 16 ? maxSafeSize : targetSceneSize)));
        } else {
            // Image not visible at all, fallback to image center
            suggestion.pos = imageRect.center().toPoint();
            suggestion.baseSize = qMax(8, qRound(targetSceneSize));
        }
    } else {
        suggestion.pos = QPoint(m_session->selectedSprite->rect.width() / 2, m_session->selectedSprite->rect.height() / 2);
        suggestion.baseSize = 20;
    }

    MarkersDialog dlg(m_session->selectedSprite, suggestion, this);
    connect(&dlg, &MarkersDialog::markersChanged, this, [this]() {
        m_previewView->overlay()->updateLayout();
        refreshHandleCombo();
    });
    dlg.exec();
}

void MainWindow::onMarkerSelectedFromCanvas(const QString& name) {
    m_session->selectedPointName = name;
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
    m_statusLabel->setText(tr("Selected: ") + m_session->selectedSprite->name);
    m_handleCombo->blockSignals(true);
    m_handleCombo->setCurrentIndex(0);
    m_handleCombo->blockSignals(false);
}

void MainWindow::onMarkerChangedFromCanvas() {
    m_previewView->overlay()->update();
    if (m_session->selectedPointName.isEmpty() && m_session->selectedSprite) {
        onCanvasPivotChanged(m_session->selectedSprite->pivotX, m_session->selectedSprite->pivotY);
    }
}

void MainWindow::onApplyPivotToSelectedTimelineFrames() {
    if (!m_session->selectedSprite) {
        m_statusLabel->setText(tr("Select a source sprite first."));
        return;
    }
    if (m_session->selectedSprites.isEmpty()) {
        m_statusLabel->setText(tr("Select frames in the layout canvas first."));
        return;
    }

    QSet<QString> targetPaths;
    for (const auto& sprite : m_session->selectedSprites) {
        if (sprite) {
            targetPaths.insert(sprite->path);
        }
    }

    int updated = 0;
    for (const QString& path : targetPaths) {
        SpritePtr target = spriteByPath(m_session->layoutModel, path);
        if (!target) {
            continue;
        }
        target->pivotX = m_session->selectedSprite->pivotX;
        target->pivotY = m_session->selectedSprite->pivotY;
        ++updated;
    }

    if (updated <= 0) {
        m_statusLabel->setText(tr("No selected frames matched loaded sprites."));
        return;
    }

    if (targetPaths.contains(m_session->selectedSprite->path)) {
        onCanvasPivotChanged(m_session->selectedSprite->pivotX, m_session->selectedSprite->pivotY);
    }
    m_previewView->overlay()->updateLayout();
    m_canvas->update();
    refreshAnimationTest();
    m_statusLabel->setText(tr("Applied pivot to %1 sprite(s).").arg(updated));
}

void MainWindow::onApplyMarkerToSelectedTimelineFrames(const QString& markerName) {
    if (!m_session->selectedSprite) {
        m_statusLabel->setText(tr("Select a source sprite first."));
        return;
    }
    const QString sourceMarkerName = markerName.trimmed();
    if (sourceMarkerName.isEmpty()) {
        m_statusLabel->setText(tr("Select a marker first."));
        return;
    }
    if (m_session->selectedSprites.isEmpty()) {
        m_statusLabel->setText(tr("Select frames in the layout canvas first."));
        return;
    }

    auto markerIt = std::find_if(
        m_session->selectedSprite->points.begin(),
        m_session->selectedSprite->points.end(),
        [&sourceMarkerName](const NamedPoint& point) { return point.name == sourceMarkerName; });
    if (markerIt == m_session->selectedSprite->points.end()) {
        m_statusLabel->setText(tr("Selected marker was not found in the source sprite."));
        return;
    }
    const NamedPoint sourceMarker = *markerIt;

    QSet<QString> targetPaths;
    for (const auto& sprite : m_session->selectedSprites) {
        if (sprite) {
            targetPaths.insert(sprite->path);
        }
    }

    int updated = 0;
    for (const QString& path : targetPaths) {
        SpritePtr target = spriteByPath(m_session->layoutModel, path);
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
