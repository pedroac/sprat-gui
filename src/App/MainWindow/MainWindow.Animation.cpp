#include "MainWindow.h"
#include "AnimationCanvas.h"
#include "UndoCommands.h"

#include "AnimationExportService.h"
#include "AnimationPlaybackService.h"
#include "AnimationPreviewService.h"
#include "AnimationTimelineOps.h"
#include "SpriteSelectionPresenter.h"
#include "TimelineGenerationService.h"
#include "TimelineUi.h"

#include <QDoubleSpinBox>
#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QScrollBar>
#include <QSpinBox>
#include <QtGlobal>
#include <algorithm>

namespace {
int selectedTimelineFps(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return 8;
    }
    return qMax(1, timelines[selectedTimelineIndex].fps);
}
}

void MainWindow::refreshTimelineList() {
    int currentRow = m_timelineList->currentRow();
    m_timelineList->blockSignals(true);
    m_timelineList->clear();
    for (const auto& timeline : m_session->timelines) {
        QIcon icon;
        if (!timeline.frames.isEmpty()) {
            int middleIndex = timeline.frames.size() / 2;
            QString middlePath = timeline.frames[middleIndex];
            icon = m_timelineListIconCache.value(middlePath);
            if (icon.isNull()) {
                icon = QIcon(middlePath);
                m_timelineListIconCache.insert(middlePath, icon);
            }
        }
        
        QListWidgetItem* item = new QListWidgetItem(
            icon,
            QStringLiteral("%1 | %2 frames | %3 fps")
                .arg(timeline.name)
                .arg(timeline.frames.size())
                .arg(timeline.fps)
        );
        m_timelineList->addItem(item);
    }
    if (currentRow >= 0 && currentRow < m_timelineList->count()) {
        m_timelineList->setCurrentRow(currentRow);
    }
    m_timelineList->blockSignals(false);
    m_timelineList->setVisible(!m_session->timelines.isEmpty());
}

void MainWindow::refreshTimelineFrames() {
    if (m_timelineFrameIconCache.size() > 4096) {
        // Evict roughly half the cache to avoid sudden stutter from full clear
        auto it = m_timelineFrameIconCache.begin();
        int toRemove = m_timelineFrameIconCache.size() / 2;
        while (toRemove > 0 && it != m_timelineFrameIconCache.end()) {
            it = m_timelineFrameIconCache.erase(it);
            --toRemove;
        }
    }
    m_timelineFramesList->setUpdatesEnabled(false);
    m_timelineFramesList->clear();
    if (m_session->selectedTimelineIndex < 0 || m_session->selectedTimelineIndex >= m_session->timelines.size()) {
        m_timelineFramesList->setUpdatesEnabled(true);
        return;
    }

    const auto& timeline = m_session->timelines[m_session->selectedTimelineIndex];
    for (const QString& path : timeline.frames) {
        QFileInfo fi(path);
        QIcon icon = m_timelineFrameIconCache.value(path);
        if (icon.isNull()) {
            icon = QIcon(path);
            m_timelineFrameIconCache.insert(path, icon);
        }
        QListWidgetItem* item = new QListWidgetItem(icon, fi.baseName());
        item->setToolTip(path);
        m_timelineFramesList->addItem(item);
    }
    m_timelineFramesList->setUpdatesEnabled(true);
}

void MainWindow::onFrameDropped(const QString& path, int index) {
    if (!AnimationTimelineOps::dropFrame(m_session->timelines, m_session->selectedTimelineIndex, path, index)) {
        return;
    }

    // Compute actual inserted index (dropFrame clamps negative/overflowing indices to end)
    const auto& frames = m_session->timelines[m_session->selectedTimelineIndex].frames;
    const int insertedIdx = (index < 0 || index >= (int)frames.size())
                                ? (int)frames.size() - 1
                                : index;

    // Refresh UI for the already-applied drop
    if (m_animCanvas) m_animCanvas->setZoomManual(false);
    refreshTimelineFrames();
    refreshTimelineList();
    fitAnimationToViewport();
    refreshAnimationTest();

    m_undoStack->push(new TimelineFrameDropCommand(
        &m_session->timelines,
        m_session->selectedTimelineIndex,
        path,
        insertedIdx,
        [this]() {
            refreshTimelineFrames();
            refreshTimelineList();
            refreshAnimationTest();
        }
    ));
}

void MainWindow::onFrameMoved(int from, int to) {
    if (m_session->selectedTimelineIndex < 0 ||
        m_session->selectedTimelineIndex >= m_session->timelines.size()) {
        return;
    }
    // Capture frames before the move for undo
    QStringList savedFramesBefore = m_session->timelines[m_session->selectedTimelineIndex].frames;

    if (!AnimationTimelineOps::moveFrame(m_session->timelines, m_session->selectedTimelineIndex, from, to)) {
        return;
    }
    refreshTimelineFrames();
    refreshTimelineList();
    refreshAnimationTest();

    m_undoStack->push(new TimelineFrameMoveCommand(
        &m_session->timelines,
        m_session->selectedTimelineIndex,
        from,
        to,
        savedFramesBefore,
        [this]() {
            refreshTimelineFrames();
            refreshTimelineList();
            refreshAnimationTest();
        }
    ));
}

void MainWindow::onFrameDuplicateRequested(int index) {
    if (m_session->selectedTimelineIndex < 0 ||
        m_session->selectedTimelineIndex >= m_session->timelines.size()) {
        return;
    }
    // Capture path for command (needed for subsequent redo after undo)
    const auto& frames = m_session->timelines[m_session->selectedTimelineIndex].frames;
    if (index < 0 || index >= frames.size()) return;
    const QString path = frames[index];

    if (!AnimationTimelineOps::duplicateFrame(m_session->timelines, m_session->selectedTimelineIndex, index)) {
        return;
    }
    refreshTimelineFrames();
    refreshTimelineList();
    refreshAnimationTest();

    m_undoStack->push(new TimelineFrameDuplicateCommand(
        &m_session->timelines,
        m_session->selectedTimelineIndex,
        index,
        path,
        [this]() {
            refreshTimelineFrames();
            refreshTimelineList();
            refreshAnimationTest();
        }
    ));
}

void MainWindow::onFrameRemoveRequested() {
    if (m_session->selectedTimelineIndex < 0 ||
        m_session->selectedTimelineIndex >= m_session->timelines.size()) {
        return;
    }
    QList<QListWidgetItem*> items = m_timelineFramesList->selectedItems();
    if (items.isEmpty()) return;

    QVector<int> rows;
    for (auto* item : items) {
        rows.append(m_timelineFramesList->row(item));
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());

    // Capture (index, path) pairs ascending for the undo command
    const auto& frames = m_session->timelines[m_session->selectedTimelineIndex].frames;
    QVector<QPair<int,QString>> removed;
    removed.reserve(rows.size());
    for (int i = rows.size() - 1; i >= 0; --i) {
        int row = rows[i];
        if (row >= 0 && row < frames.size()) {
            removed.append({row, frames[row]});
        }
    }

    AnimationTimelineOps::removeFrames(m_session->timelines, m_session->selectedTimelineIndex, rows);
    refreshTimelineFrames();
    refreshTimelineList();
    refreshAnimationTest();

    if (!removed.isEmpty()) {
        m_undoStack->push(new TimelineFrameRemoveCommand(
            &m_session->timelines,
            m_session->selectedTimelineIndex,
            removed,
            [this]() {
                refreshTimelineFrames();
                refreshTimelineList();
                refreshAnimationTest();
            }
        ));
    }
}

void MainWindow::onTimelineFrameSelectionChanged() {
    if (m_animPlaying || m_session->selectedTimelineIndex < 0 || m_session->selectedTimelineIndex >= m_session->timelines.size() || !m_timelineFramesList) {
        return;
    }
    const int selectedRow = m_timelineFramesList->currentRow();
    if (selectedRow < 0 || selectedRow >= m_session->timelines[m_session->selectedTimelineIndex].frames.size()) {
        return;
    }
    m_animFrameIndex = selectedRow;
    refreshAnimationTest();
}

void MainWindow::onGenerateTimelinesFromFrames() {
    if (m_session->layoutModels.isEmpty() || m_session->layoutModels.first().sprites.isEmpty()) {
        QMessageBox::information(this, tr("Generate Timelines"), tr("Load or generate a layout before creating timelines."));
        return;
    }

    int focusIndex = -1;
    QString status;
    bool changed = TimelineGenerationService::generateFromLayout(
        m_session->layoutModels,
        m_session->timelines,
        focusIndex,
        [this](const QString& timelineName) {
            return TimelineUi::askTimelineConflictResolution(this, timelineName);
        },
        status);

    if (changed) {
        refreshTimelineList();
        if (focusIndex >= 0) {
            m_timelineList->setCurrentRow(focusIndex);
        }
        if (m_animCanvas) m_animCanvas->setZoomManual(false);
        fitAnimationToViewport();
        refreshAnimationTest();
        m_statusLabel->setText(status);
    } else {
        QMessageBox::information(this, tr("Generate Timelines"), status);
    }
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
            m_session->timelines,
            m_session->selectedTimelineIndex,
            m_animFrameIndex,
            m_animPlaying,
            m_animTimer,
            m_animPlayPauseBtn)) {
        return;
    }
    refreshAnimationTest();
}

void MainWindow::onAnimPlayPauseClicked() {
    const bool wasPlaying = m_animPlaying;
    AnimationPlaybackService::togglePlayPause(
        m_session->timelines,
        m_session->selectedTimelineIndex,
        selectedTimelineFps(m_session->timelines, m_session->selectedTimelineIndex),
        m_animPlaying,
        m_animTimer,
        m_animPlayPauseBtn);
    if (m_animPlaying && !wasPlaying)
        m_animElapsed.start();
}

void MainWindow::onAnimNextClicked() {
    if (!AnimationPlaybackService::next(
            m_session->timelines,
            m_session->selectedTimelineIndex,
            m_animFrameIndex,
            m_animPlaying,
            m_animTimer,
            m_animPlayPauseBtn)) {
        return;
    }
    refreshAnimationTest();
}

void MainWindow::onAnimTimerTimeout() {
    const int fps = selectedTimelineFps(m_session->timelines, m_session->selectedTimelineIndex);
#ifdef Q_OS_WASM
    // Timer fires on every RAF callback (~16 ms). Skip until a full frame-duration
    // has elapsed so the animation plays at the correct fps rather than at 60 fps.
    if (m_animElapsed.elapsed() < static_cast<qint64>(1000.0 / fps))
        return;
#endif
    const qint64 elapsed = m_animElapsed.restart();
    if (!AnimationPlaybackService::tick(m_session->timelines, m_session->selectedTimelineIndex,
                                        m_animFrameIndex, elapsed, fps)) {
        return;
    }
    refreshAnimationTest();
}

bool MainWindow::exportAnimation(const QString& outPath) {
    return AnimationExportService::exportAnimation(
        this,
        m_session->timelines,
        m_session->selectedTimelineIndex,
        m_session->layoutModels,
        selectedTimelineFps(m_session->timelines, m_session->selectedTimelineIndex),
        outPath,
        [this](bool loading) { setLoading(loading); },
        [this](const QString& status) { m_statusLabel->setText(status); });
}

void MainWindow::saveAnimationToFile() {
    QString path = AnimationExportService::chooseOutputPath(this);
    if (!path.isEmpty()) {
        if (exportAnimation(path)) {
            m_statusLabel->setText(tr("Animation saved to %1").arg(path));
        }
    }
}

void MainWindow::refreshAnimationTest() {
    // Ensure all frames are in QPixmapCache before the first tick.
    // preloadTimeline() is a no-op when the timeline has not changed.
    if (m_session->selectedTimelineIndex >= 0
            && m_session->selectedTimelineIndex < m_session->timelines.size()) {
        AnimationPreviewService::preloadTimeline(
            m_session->timelines[m_session->selectedTimelineIndex].frames);
    }

    QString statusText;
    bool hasFrames = false;
    bool playing = m_animPlaying;
    QPixmap pixmap = AnimationPreviewService::refresh(
        m_session->timelines,
        m_session->selectedTimelineIndex,
        m_animFrameIndex,
        m_session->layoutModels,
        statusText,
        hasFrames,
        playing,
        m_animTimer);

    if (playing != m_animPlaying) {
        m_animPlaying = playing;
    }
    // Update button icon to reflect current playing state
    if (m_animPlayPauseBtn) {
        static const QIcon kPlayIcon = QIcon::fromTheme("media-playback-start");
        static const QIcon kPauseIcon = QIcon::fromTheme("media-playback-pause");
        m_animPlayPauseBtn->setIcon(m_animPlaying ? kPauseIcon : kPlayIcon);
    }

    m_animStatusLabel->setText(statusText);
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
}
