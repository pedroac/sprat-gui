#include "MainWindow.h"
#include "MessageDialog.h"
#include "AnimationCanvas.h"
#include "NavigatorTreeWidget.h"
#include "UndoCommands.h"

#include "AnimationExportService.h"
#include "AnimationPlaybackService.h"
#include "AnimationPreviewService.h"
#include "AnimationTimelineOps.h"
#include "SpriteSelectionPresenter.h"
#include "TimelineListWidget.h"

#include <QDoubleSpinBox>
#include <QApplication>
#include <QMetaObject>
#include <QStyle>
#include <QtConcurrent>
#include <QToolButton>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QScrollBar>
#include <QSpinBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QtGlobal>

namespace {
int selectedTimelineFps(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return 8;
    }
    return qMax(1, timelines[selectedTimelineIndex].fps);
}
}

void MainWindow::refreshTimelineList() {
    if (m_timelineEditorPanel) m_timelineEditorPanel->refreshTimelineList();
}

void MainWindow::refreshTimelineFrames() {
    if (m_timelineEditorPanel) m_timelineEditorPanel->refreshTimelineFrames();
}

// placeholder to keep old code compiling — replaced by TimelineEditorPanel::onGenerateTimelinesFromFrames
void MainWindow::onGenerateTimelinesFromFrames() {
    if (m_timelineEditorPanel) m_timelineEditorPanel->onGenerateTimelinesFromFrames();
}

void MainWindow::onAnimZoomChanged(double value) {
    if (m_animCanvas) {
        if (!m_animZoomSpin->signalsBlocked()) {
            m_animCanvas->setZoomManual(true);
        }
        m_animCanvas->setZoom(value / 100.0);
    }
}

void MainWindow::onAnimPrevClicked() {
    if (!AnimationPlaybackService::prev(
            m_session->activeAtlas().timelines,
            m_session->selectedTimelineIndex,
            m_animFrameIndex,
            m_animPlaying,
            m_animTimer,
            m_animPlayPauseBtn)) {
        return;
    }
    refreshAnimationTest();
    syncSelectedSpriteToAnimFrame();
}

void MainWindow::onAnimPlayPauseClicked() {
    const bool wasPlaying = m_animPlaying;
    AnimationPlaybackService::togglePlayPause(
        m_session->activeAtlas().timelines,
        m_session->selectedTimelineIndex,
        selectedTimelineFps(m_session->activeAtlas().timelines, m_session->selectedTimelineIndex),
        m_animPlaying,
        m_animTimer,
        m_animPlayPauseBtn);
    if (m_animPlaying && !wasPlaying) {
        m_animElapsed.start();
    } else if (wasPlaying && !m_animPlaying) {
        syncSelectedSpriteToAnimFrame();
    }
    if (m_animCanvas)
        m_animCanvas->setOverlayEditable(!m_animPlaying);
}

void MainWindow::onAnimNextClicked() {
    if (!AnimationPlaybackService::next(
            m_session->activeAtlas().timelines,
            m_session->selectedTimelineIndex,
            m_animFrameIndex,
            m_animPlaying,
            m_animTimer,
            m_animPlayPauseBtn)) {
        return;
    }
    refreshAnimationTest();
    syncSelectedSpriteToAnimFrame();
}

void MainWindow::syncSelectedSpriteToAnimFrame() {
    if (m_session->selectedTimelineIndex < 0 ||
        m_session->selectedTimelineIndex >= m_session->activeAtlas().timelines.size()) {
        if (m_animCanvas) m_animCanvas->setOverlaySprite(nullptr);
        return;
    }

    const auto& tl = m_session->activeAtlas().timelines[m_session->selectedTimelineIndex];
    const QStringList* frames = &tl.frames;
    if (!tl.aliasOf.isEmpty()) {
        for (const auto& src : m_session->activeAtlas().timelines) {
            if (src.name == tl.aliasOf && src.aliasOf.isEmpty()) {
                frames = &src.frames;
                break;
            }
        }
    }

    if (m_animFrameIndex < 0 || m_animFrameIndex >= frames->size()) {
        if (m_animCanvas) m_animCanvas->setOverlaySprite(nullptr);
        return;
    }
    const QString& path = (*frames)[m_animFrameIndex];

    for (const auto& model : m_session->activeAtlas().layoutModels) {
        for (const auto& sprite : model.sprites) {
            if (!sprite || sprite->path != path) continue;
            onSpriteSelected(sprite);
            // The animation canvas overlay tracks the current animation frame's
            // sprite independently of the global selection.
            if (m_animCanvas) {
                m_animCanvas->setOverlaySprite(sprite);
                m_animCanvas->overlay()->setSelectedMarker(m_session->selectedPointName);
                refreshAnimHandleCombo();
            }
            // Also sync the navigator tree highlight so both panels agree
            if (m_spriteTree) {
                QTreeWidgetItemIterator it(m_spriteTree);
                while (*it) {
                    if ((*it)->childCount() == 0) {
                        auto treeSprite = (*it)->data(0, Qt::UserRole).value<SpritePtr>();
                        if (treeSprite == sprite) {
                            m_spriteTree->blockSignals(true);
                            m_spriteTree->setCurrentItem(*it);
                            m_spriteTree->blockSignals(false);
                            m_spriteTree->scrollToItem(*it, QAbstractItemView::EnsureVisible);
                            break;
                        }
                    }
                    ++it;
                }
            }
            // Sync the timeline frames list so the current frame is highlighted
            if (m_timelineEditorPanel) {
                auto* framesList = m_timelineEditorPanel->timelineFramesList();
                if (framesList && m_animFrameIndex < framesList->count()) {
                    framesList->blockSignals(true);
                    framesList->setCurrentRow(m_animFrameIndex);
                    framesList->blockSignals(false);
                    if (QListWidgetItem* item = framesList->item(m_animFrameIndex))
                        framesList->scrollToItem(item, QAbstractItemView::EnsureVisible);
                }
            }
            return;
        }
    }
    // Frame path not found in layout models — clear the overlay.
    if (m_animCanvas) m_animCanvas->setOverlaySprite(nullptr);
}

void MainWindow::onAnimTimerTimeout() {
    const int fps = selectedTimelineFps(m_session->activeAtlas().timelines, m_session->selectedTimelineIndex);
#ifdef Q_OS_WASM
    // Timer fires on every RAF callback (~16 ms). Skip until a full frame-duration
    // has elapsed so the animation plays at the correct fps rather than at 60 fps.
    if (m_animElapsed.elapsed() < static_cast<qint64>(1000.0 / fps))
        return;
#endif
    const qint64 elapsed = m_animElapsed.restart();
    if (!AnimationPlaybackService::tick(m_session->activeAtlas().timelines, m_session->selectedTimelineIndex,
                                        m_animFrameIndex, elapsed, fps)) {
        return;
    }
    refreshAnimationTest();
}

bool MainWindow::exportAnimation(const QString& outPath) {
    // Capture all data needed by the background task by value so there are
    // no shared-state races with the UI thread.
    auto timelines    = m_session->activeAtlas().timelines;
    int  tlIndex      = m_session->selectedTimelineIndex;
    auto layoutModels = m_session->activeAtlas().layoutModels;
    int  fps          = selectedTimelineFps(timelines, tlIndex);

    // Callbacks must post to the main thread because they touch widgets.
    AnimationExportService::ExportCallbacks cbs;
    cbs.setLoading = [this](bool v) {
        QMetaObject::invokeMethod(this, [this, v]() { setLoading(v); }, Qt::QueuedConnection);
    };
    cbs.setStatus = [this](const QString& s) {
        QMetaObject::invokeMethod(this, [this, s]() { m_statusLabel->setText(s); }, Qt::QueuedConnection);
    };
    cbs.showError = [this](const QString& t, const QString& msg) {
        QMetaObject::invokeMethod(this, [this, t, msg]() { MessageDialog::critical(this, t, msg); }, Qt::QueuedConnection);
    };

    m_animExportOutPath = outPath;
    setLoading(true);

    // Connect once: disconnect previous connection first to avoid duplicates.
    disconnect(&m_animExportWatcher, &QFutureWatcher<bool>::finished,
               this, &MainWindow::onAnimExportFinished);
    connect(&m_animExportWatcher, &QFutureWatcher<bool>::finished,
            this, &MainWindow::onAnimExportFinished);

    m_animExportWatcher.setFuture(
        QtConcurrent::run([timelines, tlIndex, layoutModels, fps, outPath, cbs]() {
            return AnimationExportService::exportAnimation(
                timelines, tlIndex, layoutModels, fps, outPath, cbs);
        }));

    return true; // result delivered asynchronously via onAnimExportFinished
}

void MainWindow::onAnimExportFinished() {
    const bool ok = m_animExportWatcher.result();
    if (ok)
        m_statusLabel->setText(tr("Animation saved to %1").arg(m_animExportOutPath));
    // setLoading(false) is invoked by the export service via the queued callback.
}

void MainWindow::saveAnimationToFile() {
    if (m_animExportWatcher.isRunning()) return; // prevent concurrent exports
    QString path = AnimationExportService::chooseOutputPath(this);
    if (!path.isEmpty())
        exportAnimation(path);
}

void MainWindow::refreshAnimationTest() {
    // Ensure all frames are in QPixmapCache before the first tick.
    // preloadTimeline() is a no-op when the timeline has not changed.
    if (m_session->selectedTimelineIndex >= 0
            && m_session->selectedTimelineIndex < m_session->activeAtlas().timelines.size()) {
        const auto& selTl = m_session->activeAtlas().timelines[m_session->selectedTimelineIndex];
        const QStringList* preloadFrames = &selTl.frames;
        if (!selTl.aliasOf.isEmpty()) {
            for (const auto& src : m_session->activeAtlas().timelines) {
                if (src.name == selTl.aliasOf && src.aliasOf.isEmpty()) {
                    preloadFrames = &src.frames;
                    break;
                }
            }
        }
        AnimationPreviewService::preloadTimeline(*preloadFrames);
    }

    QString statusText;
    bool hasFrames = false;
    bool playing = m_animPlaying;
    const bool onionSkin = m_animOnionSkinBtn && m_animOnionSkinBtn->isChecked();
    QPixmap pixmap = AnimationPreviewService::refresh(
        m_session->activeAtlas().timelines,
        m_session->selectedTimelineIndex,
        m_animFrameIndex,
        m_session->activeAtlas().layoutModels,
        statusText,
        hasFrames,
        playing,
        m_animTimer,
        onionSkin,
        m_settings.onionSkinOpacity);

    if (playing != m_animPlaying) {
        m_animPlaying = playing;
    }
    // Update button icon to reflect current playing state
    if (m_animPlayPauseBtn) {
        static const QIcon kPlayIcon = QApplication::style()->standardIcon(QStyle::SP_MediaPlay);
        static const QIcon kPauseIcon = QApplication::style()->standardIcon(QStyle::SP_MediaPause);
        m_animPlayPauseBtn->setIcon(m_animPlaying ? kPauseIcon : kPlayIcon);
    }

    m_animStatusLabel->setText(statusText);
    m_animStatusLabel->setAlignment(hasFrames ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignCenter);
    m_animStatusLabel->setStyleSheet(hasFrames
        ? "color: #808080;"
        : "color: #808080; font-style: italic; padding: 8px 0;");
    m_animPrevBtn->setEnabled(hasFrames);
    m_animPlayPauseBtn->setEnabled(hasFrames);
    m_animNextBtn->setEnabled(hasFrames);
    
    if (m_animCanvas) {
        m_animCanvas->setPixmap(pixmap);
    }
}

void MainWindow::fitAnimationToViewport() {
    if (!m_animCanvas || !m_animZoomSpin || m_animCanvas->isZoomManual()) {
        return;
    }
    const bool blocked = m_animZoomSpin->blockSignals(true);
    m_animCanvas->initialFit();
    m_animZoomSpin->blockSignals(blocked);
}

void MainWindow::refreshHandleCombo() {
    SpriteSelectionPresenter::refreshHandleCombo(m_handleCombo, m_session->selectedSprite, m_session->selectedPointName);
    refreshAnimHandleCombo();
}

void MainWindow::refreshAnimHandleCombo() {
    if (!m_animHandleCombo) return;
    SpriteSelectionPresenter::refreshHandleCombo(
        m_animHandleCombo,
        m_animCanvas ? m_animCanvas->overlaySprite() : nullptr,
        m_session->selectedPointName);
}

void MainWindow::onAnimHandleComboChanged(int index) {
    if (index <= 0) {
        m_session->selectedPointName.clear();
    } else {
        m_session->selectedPointName = m_animHandleCombo->itemText(index);
    }

    // Apply to both overlays
    if (m_animCanvas && m_animCanvas->overlay())
        m_animCanvas->overlay()->setSelectedMarker(m_session->selectedPointName);
    if (m_previewView && m_previewView->overlay())
        m_previewView->overlay()->setSelectedMarker(m_session->selectedPointName);

    // Sync the sprite-editor handle combo
    if (m_handleCombo) {
        m_handleCombo->blockSignals(true);
        const int idx = m_session->selectedPointName.isEmpty()
                      ? 0
                      : m_handleCombo->findText(m_session->selectedPointName);
        m_handleCombo->setCurrentIndex(idx != -1 ? idx : 0);
        m_handleCombo->blockSignals(false);
    }
}
