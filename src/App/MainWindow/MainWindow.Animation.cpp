#include "MainWindow.h"
#include "AnimationCanvas.h"

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
    m_timelineList->clear();
    for (const auto& timeline : m_session->timelines) {
        m_timelineList->addItem(timeline.name);
    }
    m_timelineList->setVisible(!m_session->timelines.isEmpty());
}

void MainWindow::refreshTimelineFrames() {
    if (m_timelineFrameIconCache.size() > 4096) {
        m_timelineFrameIconCache.clear();
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
    if (m_animCanvas) m_animCanvas->setZoomManual(false);
    refreshTimelineFrames();
    fitAnimationToViewport();
    refreshAnimationTest();
}

void MainWindow::onFrameMoved(int from, int to) {
    if (!AnimationTimelineOps::moveFrame(m_session->timelines, m_session->selectedTimelineIndex, from, to)) {
        return;
    }
    refreshTimelineFrames();
    refreshAnimationTest();
}

void MainWindow::onFrameDuplicateRequested(int index) {
    if (!AnimationTimelineOps::duplicateFrame(m_session->timelines, m_session->selectedTimelineIndex, index)) {
        return;
    }
    refreshTimelineFrames();
    refreshAnimationTest();
}

void MainWindow::onFrameRemoveRequested() {
    if (m_session->selectedTimelineIndex < 0) {
        return;
    }
    QList<QListWidgetItem*> items = m_timelineFramesList->selectedItems();

    QVector<int> rows;
    for (auto* item : items) {
        rows.append(m_timelineFramesList->row(item));
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());

    AnimationTimelineOps::removeFrames(m_session->timelines, m_session->selectedTimelineIndex, rows);
    refreshTimelineFrames();
    refreshAnimationTest();
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
    if (m_session->layoutModel.sprites.isEmpty()) {
        QMessageBox::information(this, tr("Generate Timelines"), tr("Load or generate a layout before creating timelines."));
        return;
    }

    int focusIndex = -1;
    QString status;
    bool changed = TimelineGenerationService::generateFromLayout(
        m_session->layoutModel,
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
    AnimationPlaybackService::togglePlayPause(
        m_session->timelines,
        m_session->selectedTimelineIndex,
        selectedTimelineFps(m_session->timelines, m_session->selectedTimelineIndex),
        m_animPlaying,
        m_animTimer,
        m_animPlayPauseBtn);
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
    if (!AnimationPlaybackService::tick(m_session->timelines, m_session->selectedTimelineIndex, m_animFrameIndex)) {
        return;
    }
    refreshAnimationTest();
}

bool MainWindow::exportAnimation(const QString& outPath) {
    return AnimationExportService::exportAnimation(
        this,
        m_session->timelines,
        m_session->selectedTimelineIndex,
        m_session->layoutModel,
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
    QString statusText;
    bool hasFrames = false;
    bool playing = m_animPlaying;
    QPixmap pixmap = AnimationPreviewService::refresh(
        m_session->timelines,
        m_session->selectedTimelineIndex,
        m_animFrameIndex,
        m_session->layoutModel,
        statusText,
        hasFrames,
        playing,
        m_animTimer);

    if (playing != m_animPlaying) {
        m_animPlaying = playing;
        m_animPlayPauseBtn->setText(tr("Play"));
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
