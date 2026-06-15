#include "MainWindow.h"
#include "UndoCommands.h"

#include "AnimationCanvas.h"
#include "MarkersDialog.h"
#include "AnimationPreviewService.h"
#include "CliToolsConfig.h"
#include "SpriteEditorPanel.h"
#include <algorithm>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QSet>
#include <QImageReader>
#include <QToolButton>

namespace {
static int fromDisplay(double v, int dim, CoordUnit unit, int origin = 0) {
    const int raw = (unit == CoordUnit::Percent && dim > 0)
        ? qRound(v * dim / 100.0) : qRound(v);
    return raw + origin;
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
    int sw = activeSize.width();
    int sh = activeSize.height();
    int ox = 0, oy = 0;
    if (m_settings.showTrimRect && m_previewView) {
        const QRect tr = m_previewView->cachedTrimRect();
        if (tr.isValid()) {
            ox = tr.left();
            oy = tr.top();
            sw = tr.width();
            sh = tr.height();
        }
    }
    const int  newX = fromDisplay(m_pivotXSpin->value(), sw, unit, ox);
    const int  newY = fromDisplay(m_pivotYSpin->value(), sh, unit, oy);

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
                    const int csw = (m_settings.showTrimRect && sw > 0) ? sw : coSize.width();
                    const int csh = (m_settings.showTrimRect && sh > 0) ? sh : coSize.height();
                    sprite->pivotX = fromDisplay(m_pivotXSpin->value(), csw, unit, ox);
                    sprite->pivotY = fromDisplay(m_pivotYSpin->value(), csh, unit, oy);
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
                const int csw = (m_settings.showTrimRect && sw > 0) ? sw : coSize.width();
                const int csh = (m_settings.showTrimRect && sh > 0) ? sh : coSize.height();
                const int cx = fromDisplay(m_pivotXSpin->value(), csw, unit, ox);
                const int cy = fromDisplay(m_pivotYSpin->value(), csh, unit, oy);
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

    // Keep animation overlay and combo in sync
    if (m_animCanvas && m_animCanvas->overlay())
        m_animCanvas->overlay()->setSelectedMarker(m_session->selectedPointName);
    if (m_animHandleCombo) {
        m_animHandleCombo->blockSignals(true);
        const int idx = m_session->selectedPointName.isEmpty()
                      ? 0
                      : m_animHandleCombo->findText(m_session->selectedPointName);
        m_animHandleCombo->setCurrentIndex(idx != -1 ? idx : 0);
        m_animHandleCombo->blockSignals(false);
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

    // Sync both handle combos
    auto syncCombo = [&](QComboBox* combo) {
        if (!combo) return;
        combo->blockSignals(true);
        if (!name.isEmpty()) {
            const int idx = combo->findText(name);
            if (idx != -1) combo->setCurrentIndex(idx);
        } else {
            combo->setCurrentIndex(0);
        }
        combo->blockSignals(false);
    };
    syncCombo(m_handleCombo);
    syncCombo(m_animHandleCombo);

    if (!name.isEmpty()) {
        m_statusLabel->setText(tr("Selected Marker: ") + name);
        syncCoordinateSpinsFromSelection();
        return;
    }
    m_statusLabel->setText(tr("Selected: ") + (m_session->selectedSprite ? m_session->selectedSprite->name : tr("none")));
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

// ---------------------------------------------------------------------------
// applyMarkersToSelection — shared helper for paste and template apply
// ---------------------------------------------------------------------------
void MainWindow::applyMarkersToSelection(const QVector<NamedPoint>& points) {
    if (!m_session->selectedSprite) return;
    const QVector<NamedPoint> old = m_session->selectedSprite->points;
    m_session->selectedSprite->points = points;
    QVector<SetMarkersCommand::CoTarget> coTargets;
    auto addCoTarget = [&](const SpritePtr& s) {
        const QVector<NamedPoint> prev = s->points;
        s->points = points;
        coTargets.append({s, prev, s->points});
    };
    if (m_settings.propagateEditsToChecked)
        for (const auto& s : m_session->selectedSprites)
            if (s && s != m_session->selectedSprite) addCoTarget(s);
    m_undoStack->push(new SetMarkersCommand(
        m_session->selectedSprite, old, points,
        [this]() { m_previewView->overlay()->updateLayout(); refreshHandleCombo(); },
        std::move(coTargets)));
    m_previewView->overlay()->updateLayout();
    refreshHandleCombo();
}

// ---------------------------------------------------------------------------
// Copy/paste handlers
// ---------------------------------------------------------------------------
void MainWindow::onCopyMarkersRequested() {
    if (!m_session->selectedSprite) return;
    m_markerClipboard = m_session->selectedSprite->points;
}

void MainWindow::onPasteMarkersRequested() {
    if (!m_session->selectedSprite || m_markerClipboard.isEmpty()) return;
    applyMarkersToSelection(m_markerClipboard);
}

// ---------------------------------------------------------------------------
// Template handlers
// ---------------------------------------------------------------------------
void MainWindow::onSaveMarkerTemplate() {
    if (!m_session->selectedSprite) return;
    bool ok;
    QString name = QInputDialog::getText(this, tr("Save Marker Template"),
        tr("Template name:"), QLineEdit::Normal, {}, &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    MarkerTemplate tmpl{name.trimmed(), m_session->selectedSprite->points};
    auto it = std::find_if(m_markerTemplates.begin(), m_markerTemplates.end(),
        [&](const MarkerTemplate& t){ return t.name == tmpl.name; });
    if (it != m_markerTemplates.end()) *it = tmpl; else m_markerTemplates.append(tmpl);
    refreshMarkerTemplatesMenu();
}

void MainWindow::onApplyMarkerTemplate(const MarkerTemplate& tmpl) {
    if (!m_session->selectedSprite) return;
    applyMarkersToSelection(tmpl.points);
}

void MainWindow::onDeleteMarkerTemplate(const QString& name) {
    m_markerTemplates.erase(
        std::remove_if(m_markerTemplates.begin(), m_markerTemplates.end(),
            [&](const MarkerTemplate& t){ return t.name == name; }),
        m_markerTemplates.end());
    refreshMarkerTemplatesMenu();
}

void MainWindow::refreshMarkerTemplatesMenu() {
    if (!m_spriteEditorPanel) return;
    auto* btn = m_spriteEditorPanel->markerTemplatesBtn();
    if (!btn) return;
    auto* menu = btn->menu();
    if (!menu) { menu = new QMenu(btn); btn->setMenu(menu); }
    menu->clear();
    menu->addAction(tr("Save current markers as template\xe2\x80\xa6"), this, &MainWindow::onSaveMarkerTemplate);
    if (!m_markerTemplates.isEmpty()) {
        menu->addSeparator();
        for (const auto& t : m_markerTemplates)
            menu->addAction(tr("Apply: %1").arg(t.name), this,
                [this, t]() { onApplyMarkerTemplate(t); });
        menu->addSeparator();
        auto* del = menu->addMenu(tr("Delete template"));
        for (const auto& t : m_markerTemplates)
            del->addAction(t.name, this,
                [this, n = t.name]() { onDeleteMarkerTemplate(n); });
    }
}
