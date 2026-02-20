#include "MainWindow.h"

#include "AnimationExportService.h"
#include "AnimationPlaybackService.h"
#include "AnimationPreviewService.h"
#include "AnimationTimelineOps.h"
#include "SpriteSelectionPresenter.h"
#include "TimelineGenerationService.h"
#include "TimelineUi.h"

#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
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
    for (const auto& timeline : m_timelines) {
        m_timelineList->addItem(timeline.name);
    }
    m_timelineList->setVisible(!m_timelines.isEmpty());
}

void MainWindow::refreshTimelineFrames() {
    if (m_timelineFrameIconCache.size() > 4096) {
        m_timelineFrameIconCache.clear();
    }
    m_timelineFramesList->setUpdatesEnabled(false);
    m_timelineFramesList->clear();
    if (m_selectedTimelineIndex < 0 || m_selectedTimelineIndex >= m_timelines.size()) {
        m_timelineFramesList->setUpdatesEnabled(true);
        return;
    }

    const auto& timeline = m_timelines[m_selectedTimelineIndex];
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
    if (!AnimationTimelineOps::dropFrame(m_timelines, m_selectedTimelineIndex, path, index)) {
        return;
    }
    refreshTimelineFrames();
    refreshAnimationTest();
}

void MainWindow::onFrameMoved(int from, int to) {
    if (!AnimationTimelineOps::moveFrame(m_timelines, m_selectedTimelineIndex, from, to)) {
        return;
    }
    refreshTimelineFrames();
    refreshAnimationTest();
}

void MainWindow::onFrameDuplicateRequested(int index) {
    if (!AnimationTimelineOps::duplicateFrame(m_timelines, m_selectedTimelineIndex, index)) {
        return;
    }
    refreshTimelineFrames();
    refreshAnimationTest();
}

void MainWindow::onFrameRemoveRequested() {
    if (m_selectedTimelineIndex < 0) {
        return;
    }
    QList<QListWidgetItem*> items = m_timelineFramesList->selectedItems();

    QVector<int> rows;
    for (auto* item : items) {
        rows.append(m_timelineFramesList->row(item));
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());

    AnimationTimelineOps::removeFrames(m_timelines, m_selectedTimelineIndex, rows);
    refreshTimelineFrames();
    refreshAnimationTest();
}

void MainWindow::onTimelineFrameSelectionChanged() {
    if (m_animPlaying || m_selectedTimelineIndex < 0 || m_selectedTimelineIndex >= m_timelines.size() || !m_timelineFramesList) {
        return;
    }
    const int selectedRow = m_timelineFramesList->currentRow();
    if (selectedRow < 0 || selectedRow >= m_timelines[m_selectedTimelineIndex].frames.size()) {
        return;
    }
    m_animFrameIndex = selectedRow;
    refreshAnimationTest();
}

void MainWindow::onGenerateTimelinesFromFrames() {
    if (m_layoutModel.sprites.isEmpty()) {
        QMessageBox::information(this, tr("Generate Timelines"), tr("Load or generate a layout before creating timelines."));
        return;
    }

    int focusIndex = -1;
    QString status;
    bool changed = TimelineGenerationService::generateFromLayout(
        m_layoutModel,
        m_timelines,
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
        refreshAnimationTest();
        m_statusLabel->setText(status);
    } else {
        QMessageBox::information(this, tr("Generate Timelines"), status);
    }
}

void MainWindow::onAnimPrevClicked() {
    if (!AnimationPlaybackService::prev(
            m_timelines,
            m_selectedTimelineIndex,
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
        m_timelines,
        m_selectedTimelineIndex,
        selectedTimelineFps(m_timelines, m_selectedTimelineIndex),
        m_animPlaying,
        m_animTimer,
        m_animPlayPauseBtn);
}

void MainWindow::onAnimNextClicked() {
    if (!AnimationPlaybackService::next(
            m_timelines,
            m_selectedTimelineIndex,
            m_animFrameIndex,
            m_animPlaying,
            m_animTimer,
            m_animPlayPauseBtn)) {
        return;
    }
    refreshAnimationTest();
}

void MainWindow::onAnimTimerTimeout() {
    if (!AnimationPlaybackService::tick(m_timelines, m_selectedTimelineIndex, m_animFrameIndex)) {
        return;
    }
    refreshAnimationTest();
}

bool MainWindow::exportAnimation(const QString& outPath) {
    return AnimationExportService::exportAnimation(
        this,
        m_timelines,
        m_selectedTimelineIndex,
        m_layoutModel,
        selectedTimelineFps(m_timelines, m_selectedTimelineIndex),
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
    const int previewPadding = m_animPaddingSpin ? m_animPaddingSpin->value() : 24;
    AnimationPreviewService::refresh(
        m_timelines,
        m_selectedTimelineIndex,
        m_animFrameIndex,
        m_layoutModel,
        m_animZoomSpin->value(),
        previewPadding,
        m_animPreviewLabel,
        m_animStatusLabel,
        m_animPrevBtn,
        m_animPlayPauseBtn,
        m_animNextBtn,
        m_animPlaying,
        m_animTimer);
}

void MainWindow::refreshHandleCombo() {
    SpriteSelectionPresenter::refreshHandleCombo(m_handleCombo, m_selectedSprite, m_selectedPointName);
}
