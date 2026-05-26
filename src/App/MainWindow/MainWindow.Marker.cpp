#include "MainWindow.h"
#include "UndoCommands.h"

#include "MarkersDialog.h"
#include "AnimationPreviewService.h"
#include <algorithm>

#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QSet>
#include <QImageReader>
#include <algorithm>

namespace {
SpritePtr spriteByPath(const QVector<LayoutModel>& models, const QString& path) {
    for (const auto& model : models) {
        for (const auto& sprite : model.sprites) {
            if (sprite && sprite->path == path) {
                return sprite;
            }
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
    if (!m_session->selectedSprite) return;

    const int newX = m_pivotXSpin->value();
    const int newY = m_pivotYSpin->value();

    if (m_session->selectedPointName.isEmpty()) {
        // Pivot
        const int oldX = m_session->selectedSprite->pivotX;
        const int oldY = m_session->selectedSprite->pivotY;
        if (oldX == newX && oldY == newY) return;
        QVector<QPair<SpritePtr, QPair<int,int>>> coTargets;
        if (m_settings.propagateEditsToChecked) {
            for (const auto& sprite : m_session->selectedSprites) {
                if (sprite && sprite != m_session->selectedSprite) {
                    coTargets.append({sprite, {sprite->pivotX, sprite->pivotY}});
                    sprite->pivotX = newX;
                    sprite->pivotY = newY;
                }
            }
        }
        AnimationPreviewService::invalidateBounds();
        m_undoStack->push(new SetPivotCommand(
            m_session->selectedSprite, oldX, oldY, newX, newY,
            /*alreadyApplied=*/false, std::move(coTargets)));
        m_previewView->overlay()->updateLayout();
        updateOnionSkinDisplay();
    } else {
        // Named marker — translate its position to (newX, newY)
        auto& sprite = m_session->selectedSprite;
        const QVector<NamedPoint> oldPoints = sprite->points;
        bool found = false;
        for (auto& p : sprite->points) {
            if (p.name != m_session->selectedPointName) continue;
            if (p.x == newX && p.y == newY) return;
            if (p.kind == MarkerKind::Polygon && !p.polygonPoints.isEmpty()) {
                const QPoint delta(newX - p.polygonPoints[0].x(), newY - p.polygonPoints[0].y());
                for (auto& pt : p.polygonPoints) pt += delta;
            }
            p.x = newX;
            p.y = newY;
            found = true;
            break;
        }
        if (!found) return;
        QVector<QPair<SpritePtr, QVector<NamedPoint>>> coTargets;
        if (m_settings.propagateEditsToChecked) {
            for (const auto& coSprite : m_session->selectedSprites) {
                if (!coSprite || coSprite == sprite) continue;
                coTargets.append({coSprite, coSprite->points});
                for (auto& p : coSprite->points) {
                    if (p.name != m_session->selectedPointName) continue;
                    if (p.kind == MarkerKind::Polygon && !p.polygonPoints.isEmpty()) {
                        const QPoint delta(newX - p.polygonPoints[0].x(), newY - p.polygonPoints[0].y());
                        for (auto& pt : p.polygonPoints) pt += delta;
                    }
                    p.x = newX;
                    p.y = newY;
                    break;
                }
            }
        }
        m_undoStack->push(new SetMarkersCommand(
            sprite, oldPoints, sprite->points,
            [this]() {
                m_previewView->overlay()->updateLayout();
                refreshHandleCombo();
            },
            std::move(coTargets)
        ));
        m_previewView->overlay()->updateLayout();
    }
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
        if (m_previewView && m_previewView->overlay())
            m_previewView->overlay()->setSelectedMarker("");
        // Show pivot coordinates in spin boxes
        if (m_session->selectedSprite)
            onCanvasPivotChanged(m_session->selectedSprite->pivotX, m_session->selectedSprite->pivotY);
    } else {
        m_session->selectedPointName = m_handleCombo->itemText(index);
        if (m_previewView && m_previewView->overlay())
            m_previewView->overlay()->setSelectedMarker(m_session->selectedPointName);
        // Show selected marker coordinates in spin boxes
        if (m_session->selectedSprite) {
            for (const auto& p : m_session->selectedSprite->points) {
                if (p.name == m_session->selectedPointName) {
                    m_pivotXSpin->blockSignals(true);
                    m_pivotYSpin->blockSignals(true);
                    m_pivotXSpin->setValue(p.x);
                    m_pivotYSpin->setValue(p.y);
                    m_pivotXSpin->blockSignals(false);
                    m_pivotYSpin->blockSignals(false);
                    break;
                }
            }
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

    const QVector<NamedPoint> oldPoints = m_session->selectedSprite->points;

    MarkersDialog dlg(m_session->selectedSprite, suggestion, this);
    connect(&dlg, &MarkersDialog::markersChanged, this, [this]() {
        m_previewView->overlay()->updateLayout();
        refreshHandleCombo();
    });
    dlg.exec();

    const QVector<NamedPoint> newPoints = m_session->selectedSprite->points;
    if (newPoints != oldPoints) {
        QVector<QPair<SpritePtr, QVector<NamedPoint>>> coTargets;
        if (m_settings.propagateEditsToChecked) {
            for (const auto& sprite : m_session->selectedSprites) {
                if (sprite && sprite != m_session->selectedSprite) {
                    coTargets.append({sprite, sprite->points});
                    sprite->points = newPoints;
                }
            }
        }
        m_undoStack->push(new SetMarkersCommand(
            m_session->selectedSprite,
            oldPoints,
            newPoints,
            [this]() {
                m_previewView->overlay()->updateLayout();
                refreshHandleCombo();
            },
            std::move(coTargets)
        ));
    }
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
        // Update spin boxes to show this marker's position
        if (m_session->selectedSprite) {
            for (const auto& p : m_session->selectedSprite->points) {
                if (p.name == name) {
                    m_pivotXSpin->blockSignals(true);
                    m_pivotYSpin->blockSignals(true);
                    m_pivotXSpin->setValue(p.x);
                    m_pivotYSpin->setValue(p.y);
                    m_pivotXSpin->blockSignals(false);
                    m_pivotYSpin->blockSignals(false);
                    break;
                }
            }
        }
        return;
    }
    m_statusLabel->setText(tr("Selected: ") + m_session->selectedSprite->name);
    m_handleCombo->blockSignals(true);
    m_handleCombo->setCurrentIndex(0);
    m_handleCombo->blockSignals(false);
    // Update spin boxes to show pivot
    if (m_session->selectedSprite)
        onCanvasPivotChanged(m_session->selectedSprite->pivotX, m_session->selectedSprite->pivotY);
}

void MainWindow::onMarkerChangedFromCanvas() {
    m_previewView->overlay()->update();
    if (!m_session->selectedSprite) return;
    if (m_session->selectedPointName.isEmpty()) {
        onCanvasPivotChanged(m_session->selectedSprite->pivotX, m_session->selectedSprite->pivotY);
    } else {
        for (const auto& p : m_session->selectedSprite->points) {
            if (p.name == m_session->selectedPointName) {
                m_pivotXSpin->blockSignals(true);
                m_pivotYSpin->blockSignals(true);
                m_pivotXSpin->setValue(p.x);
                m_pivotYSpin->setValue(p.y);
                m_pivotXSpin->blockSignals(false);
                m_pivotYSpin->blockSignals(false);
                break;
            }
        }
    }
}

