#include "MainWindow.h"
#include "UndoCommands.h"

#include "MarkersDialog.h"
#include "AnimationPreviewService.h"
#include "CliToolsConfig.h"
#include <algorithm>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSet>
#include <QImageReader>

namespace {
static double toDisplay(int px, int dim, CoordUnit unit) {
    return (unit == CoordUnit::Percent && dim > 0)
        ? px * 100.0 / dim : double(px);
}
static int fromDisplay(double v, int dim, CoordUnit unit) {
    return (unit == CoordUnit::Percent && dim > 0)
        ? qRound(v * dim / 100.0) : qRound(v);
}
}

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

QSize MainWindow::spriteCoordinateSpaceSize(const SpritePtr& sprite) const {
    if (!sprite) {
        return {};
    }

    const QSize sourceSize = QImageReader(sprite->path).size();
    if (sourceSize.isValid() && sourceSize.width() > 0 && sourceSize.height() > 0) {
        return sourceSize;
    }

    const int contentWidth = sprite->rotated ? sprite->rect.height() : sprite->rect.width();
    const int contentHeight = sprite->rotated ? sprite->rect.width() : sprite->rect.height();

    if (sprite->trimmed) {
        const int fullWidth = sprite->trimRect.x() + contentWidth + sprite->trimRect.width();
        const int fullHeight = sprite->trimRect.y() + contentHeight + sprite->trimRect.height();
        if (fullWidth > 0 && fullHeight > 0) {
            return QSize(fullWidth, fullHeight);
        }
    }

    if (contentWidth > 0 && contentHeight > 0) {
        return QSize(contentWidth, contentHeight);
    }

    return sprite->rect.size();
}

void MainWindow::onPivotSpinChanged() {
    if (!m_session->selectedSprite) return;

    const auto unit = m_settings.coordUnit;
    const QSize activeSize = spriteCoordinateSpaceSize(m_session->selectedSprite);
    const int  sw   = activeSize.width();
    const int  sh   = activeSize.height();
    const int  newX = fromDisplay(m_pivotXSpin->value(), sw, unit);
    const int  newY = fromDisplay(m_pivotYSpin->value(), sh, unit);

    if (m_session->selectedPointName.isEmpty()) {
        // Pivot
        const int oldX = m_session->selectedSprite->pivotX;
        const int oldY = m_session->selectedSprite->pivotY;
        const bool activeChanged = oldX != newX || oldY != newY;
        QVector<SetPivotCommand::CoTarget> coTargets;
        if (m_settings.propagateEditsToChecked) {
            for (const auto& sprite : m_session->selectedSprites) {
                if (sprite && sprite != m_session->selectedSprite) {
                    const QPair<int, int> oldPos{sprite->pivotX, sprite->pivotY};
                    const QSize coSize = spriteCoordinateSpaceSize(sprite);
                    sprite->pivotX = fromDisplay(m_pivotXSpin->value(),
                                                 coSize.width(), unit);
                    sprite->pivotY = fromDisplay(m_pivotYSpin->value(),
                                                 coSize.height(), unit);
                    const QPair<int, int> newPos{sprite->pivotX, sprite->pivotY};
                    if (oldPos != newPos) {
                        coTargets.append({sprite, oldPos, newPos});
                    }
                }
            }
        }
        if (!activeChanged && coTargets.isEmpty()) return;
        storeCoordinateFieldOverride();
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
        bool activeChanged = false;
        for (auto& p : sprite->points) {
            if (p.name != m_session->selectedPointName) continue;
            if (p.x != newX || p.y != newY) {
                if (p.kind == MarkerKind::Polygon && !p.polygonPoints.isEmpty()) {
                    const QPoint delta(newX - p.polygonPoints[0].x(), newY - p.polygonPoints[0].y());
                    for (auto& pt : p.polygonPoints) pt += delta;
                }
                p.x = newX;
                p.y = newY;
                activeChanged = true;
            }
            found = true;
            break;
        }
        if (!found) return;
        QVector<SetMarkersCommand::CoTarget> coTargets;
        if (m_settings.propagateEditsToChecked) {
            for (const auto& coSprite : m_session->selectedSprites) {
                if (!coSprite || coSprite == sprite) continue;
                const QVector<NamedPoint> oldCoPoints = coSprite->points;
                const QSize coSize = spriteCoordinateSpaceSize(coSprite);
                const int cx = fromDisplay(m_pivotXSpin->value(),
                                           coSize.width(), unit);
                const int cy = fromDisplay(m_pivotYSpin->value(),
                                           coSize.height(), unit);
                for (auto& p : coSprite->points) {
                    if (p.name != m_session->selectedPointName) continue;
                    if (p.kind == MarkerKind::Polygon && !p.polygonPoints.isEmpty()) {
                        const QPoint delta(cx - p.polygonPoints[0].x(), cy - p.polygonPoints[0].y());
                        for (auto& pt : p.polygonPoints) pt += delta;
                    }
                    p.x = cx;
                    p.y = cy;
                    break;
                }
                if (coSprite->points != oldCoPoints) {
                    coTargets.append({coSprite, oldCoPoints, coSprite->points});
                }
            }
        }
        if (!activeChanged && coTargets.isEmpty()) return;
        storeCoordinateFieldOverride();
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
    Q_UNUSED(x);
    Q_UNUSED(y);
    clearCoordinateFieldOverride();
    syncCoordinateSpinsFromSelection();
}

void MainWindow::onHandleComboChanged(int index) {
    clearCoordinateFieldOverride();
    if (index <= 0) {
        m_session->selectedPointName.clear();
        if (m_previewView && m_previewView->overlay())
            m_previewView->overlay()->setSelectedMarker("");
        // Show pivot coordinates in spin boxes
        if (m_session->selectedSprite)
            syncCoordinateSpinsFromSelection();
    } else {
        m_session->selectedPointName = m_handleCombo->itemText(index);
        if (m_previewView && m_previewView->overlay())
            m_previewView->overlay()->setSelectedMarker(m_session->selectedPointName);
        syncCoordinateSpinsFromSelection();
    }

    m_statusLabel->setText(m_session->selectedPointName.isEmpty()
        ? tr("Selected: ") + (m_session->selectedSprite ? m_session->selectedSprite->name : tr("none"))
        : tr("Selected Marker: ") + m_session->selectedPointName);
}

void MainWindow::onPointsConfigClicked() {
    if (!m_session->selectedSprite) {
        return;
    }
    clearCoordinateFieldOverride();

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
        QVector<SetMarkersCommand::CoTarget> coTargets;
        if (m_settings.propagateEditsToChecked) {
            for (const auto& sprite : m_session->selectedSprites) {
                if (sprite && sprite != m_session->selectedSprite) {
                    const QVector<NamedPoint> oldCoPoints = sprite->points;
                    sprite->points = newPoints;
                    coTargets.append({sprite, oldCoPoints, sprite->points});
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
    clearCoordinateFieldOverride();
    m_session->selectedPointName = name;
    if (!name.isEmpty()) {
        m_statusLabel->setText(tr("Selected Marker: ") + name);
        const int idx = m_handleCombo->findText(name);
        if (idx != -1) {
            m_handleCombo->blockSignals(true);
            m_handleCombo->setCurrentIndex(idx);
            m_handleCombo->blockSignals(false);
        }
        syncCoordinateSpinsFromSelection();
        return;
    }
    m_statusLabel->setText(tr("Selected: ") + m_session->selectedSprite->name);
    m_handleCombo->blockSignals(true);
    m_handleCombo->setCurrentIndex(0);
    m_handleCombo->blockSignals(false);
    if (m_session->selectedSprite)
        syncCoordinateSpinsFromSelection();
}

void MainWindow::onMarkerChangedFromCanvas() {
    m_previewView->overlay()->update();
    if (!m_session->selectedSprite) return;
    clearCoordinateFieldOverride();
    syncCoordinateSpinsFromSelection();
}

void MainWindow::onCoordUnitChanged() {
    if (!m_coordUnitCombo) {
        return;
    }
    clearCoordinateFieldOverride();

    const CoordUnit newUnit = m_coordUnitCombo->currentIndex() == 1
        ? CoordUnit::Percent
        : CoordUnit::Pixels;
    if (m_settings.coordUnit == newUnit) {
        syncCoordinateSpinsFromSelection();
        return;
    }

    m_settings.coordUnit = newUnit;
    CliToolsConfig::saveAppSettings(m_settings, m_cliPaths);
    syncCoordinateSpinsFromSelection();
}
