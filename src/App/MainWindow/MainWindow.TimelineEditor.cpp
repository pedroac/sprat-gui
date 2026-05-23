#include "MainWindow.h"
#include "UndoCommands.h"

#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QSignalBlocker>
#include <QSpinBox>

void MainWindow::onTimelineAddClicked() {
    QString name = m_timelineCreateEdit->text().trimmed();
    if (name.isEmpty()) {
        name = QString("Timeline %1").arg(m_session->timelines.size() + 1);
    }

    AnimationTimeline timeline;
    timeline.name = name;
    m_session->timelines.append(timeline);
    m_session->selectedTimelineIndex = m_session->timelines.size() - 1;

    m_timelineCreateEdit->clear();
    refreshTimelineList();
    m_timelineList->setCurrentRow(m_session->selectedTimelineIndex);

    m_undoStack->push(new TimelineAddCommand(
        &m_session->timelines,
        timeline,
        &m_session->selectedTimelineIndex,
        [this]() {
            refreshTimelineList();
            if (m_session->selectedTimelineIndex >= 0) {
                m_timelineList->setCurrentRow(m_session->selectedTimelineIndex);
            } else {
                onTimelineSelectionChanged();
            }
            refreshTimelineFrames();
            refreshAnimationTest();
        }
    ));
}

void MainWindow::onTimelineRemoveClicked() {
    if (m_session->selectedTimelineIndex < 0 || m_session->selectedTimelineIndex >= m_session->timelines.size()) {
        return;
    }
    const int removeIdx = m_session->selectedTimelineIndex;
    const AnimationTimeline savedTimeline = m_session->timelines[removeIdx];

    m_session->timelines.removeAt(removeIdx);
    m_session->selectedTimelineIndex = qMin(removeIdx, m_session->timelines.size() - 1);

    refreshTimelineList();
    if (m_session->selectedTimelineIndex >= 0) {
        m_timelineList->setCurrentRow(m_session->selectedTimelineIndex);
    } else {
        // No timelines left, hide the timeline editor
        onTimelineSelectionChanged();
    }

    m_undoStack->push(new TimelineRemoveCommand(
        &m_session->timelines,
        removeIdx,
        savedTimeline,
        &m_session->selectedTimelineIndex,
        [this]() {
            refreshTimelineList();
            if (m_session->selectedTimelineIndex >= 0) {
                m_timelineList->setCurrentRow(m_session->selectedTimelineIndex);
            } else {
                onTimelineSelectionChanged();
            }
            refreshTimelineFrames();
            refreshAnimationTest();
        }
    ));
}

void MainWindow::onTimelineSelectionChanged() {
    const int row = m_timelineList->currentRow();
    if (row >= 0 && row < m_session->timelines.size()) {
        m_session->selectedTimelineIndex = row;
        const auto& tl = m_session->timelines[row];
        const bool isAlias = !tl.aliasOf.isEmpty();

        m_timelineNameEdit->setText(tl.name);
        m_timelineNameEdit->setEnabled(true);

        // Alias label
        if (m_timelineAliasLabel) {
            m_timelineAliasLabel->setVisible(isAlias);
            if (isAlias)
                m_timelineAliasLabel->setText(tr("Alias of: %1").arg(tl.aliasOf));
        }

        // Flip combo
        if (m_timelineFlipLabel) m_timelineFlipLabel->setVisible(isAlias);
        if (m_timelineFlipCombo) {
            m_timelineFlipCombo->setVisible(isAlias);
            if (isAlias) {
                const QSignalBlocker b(m_timelineFlipCombo);
                int flipIdx = (tl.hFlip ? 1 : 0) + (tl.vFlip ? 2 : 0);
                m_timelineFlipCombo->setCurrentIndex(flipIdx);
            }
        }

        // FPS: always show; disable for aliases, sync from source
        int fpsToShow = tl.fps;
        if (isAlias) {
            for (const auto& src : m_session->timelines) {
                if (src.name == tl.aliasOf && src.aliasOf.isEmpty()) {
                    fpsToShow = src.fps;
                    break;
                }
            }
        }
        {
            const QSignalBlocker blocker(m_timelineFpsSpin);
            m_timelineFpsSpin->setValue(fpsToShow);
        }
        m_timelineFpsSpin->setEnabled(!isAlias);

        if (m_animPlaying) {
#ifndef Q_OS_WASM
            m_animTimer->setInterval(1000 / qMax(1, fpsToShow));
#endif
            m_animElapsed.restart(); // reset timing to avoid frame skip on first tick
        }

        // Frames list: read-only for aliases
        m_timelineFramesList->setReadOnly(isAlias);

        refreshTimelineFrames();
        m_animFrameIndex = 0;
        m_timelineEditorContainer->setVisible(true);
        fitAnimationToViewport();
        refreshAnimationTest();
        return;
    }
    m_session->selectedTimelineIndex = -1;
    m_timelineNameEdit->clear();
    m_timelineNameEdit->setEnabled(false);
    {
        const QSignalBlocker blocker(m_timelineFpsSpin);
        m_timelineFpsSpin->setValue(8);
    }
    m_timelineFpsSpin->setEnabled(false);
    if (m_timelineAliasLabel) m_timelineAliasLabel->setVisible(false);
    if (m_timelineFlipLabel)  m_timelineFlipLabel->setVisible(false);
    if (m_timelineFlipCombo)  m_timelineFlipCombo->setVisible(false);
    m_timelineEditorContainer->setVisible(false);
    m_timelineDragHintLabel->setVisible(false);
    m_timelineFramesList->clear();
    m_timelineFramesList->setReadOnly(false);
    refreshAnimationTest();
}

void MainWindow::onTimelineNameChanged() {
    const int idx = m_session->selectedTimelineIndex;
    if (idx < 0 || idx >= m_session->timelines.size()) return;
    const QString oldName = m_session->timelines[idx].name;
    const QString newName = m_timelineNameEdit->text();
    if (oldName == newName) return;
    m_session->timelines[idx].name = newName;
    // Cascade rename to any aliases pointing to the old name
    for (auto& tl : m_session->timelines) {
        if (tl.aliasOf == oldName) tl.aliasOf = newName;
    }
    refreshTimelineList();
    m_undoStack->push(new SetTimelineNameCommand(
        &m_session->timelines, idx, oldName, newName,
        [this, idx]() {
            refreshTimelineList();
            if (m_session->selectedTimelineIndex == idx
                    && idx >= 0 && idx < m_session->timelines.size()) {
                const QSignalBlocker blocker(m_timelineNameEdit);
                m_timelineNameEdit->setText(m_session->timelines[idx].name);
            }
        }
    ));
}

void MainWindow::onTimelineFpsChanged(int fps) {
    const int idx = m_session->selectedTimelineIndex;
    if (idx < 0 || idx >= m_session->timelines.size()) return;
    const int oldFps = m_session->timelines[idx].fps;
    if (oldFps == fps) return;
    m_session->timelines[idx].fps = fps;
    if (m_animPlaying) {
#ifndef Q_OS_WASM
        m_animTimer->setInterval(1000 / fps);
#endif
        m_animElapsed.restart();
    }
    refreshTimelineList();
    refreshAnimationTest();
    m_undoStack->push(new SetTimelineFpsCommand(
        &m_session->timelines, idx, oldFps, fps,
        [this, idx]() {
            if (m_session->selectedTimelineIndex == idx
                    && idx >= 0 && idx < m_session->timelines.size()) {
                const int curFps = m_session->timelines[idx].fps;
                {
                    const QSignalBlocker blocker(m_timelineFpsSpin);
                    m_timelineFpsSpin->setValue(curFps);
                }
                if (m_animPlaying) {
#ifndef Q_OS_WASM
                    m_animTimer->setInterval(1000 / curFps);
#endif
                    m_animElapsed.restart();
                }
            }
            refreshTimelineList();
            refreshAnimationTest();
        }
    ));
}

void MainWindow::onTimelineCreateAlias() {
    const int idx = m_session->selectedTimelineIndex;
    if (idx < 0 || idx >= m_session->timelines.size()) return;
    const auto& source = m_session->timelines[idx];
    if (!source.aliasOf.isEmpty()) return; // Cannot alias an alias

    const QString sourceName = source.name;

    AnimationTimeline alias;
    alias.name = sourceName + "_alias";
    alias.aliasOf = sourceName;
    alias.fps = source.fps;

    m_session->timelines.append(alias);
    m_session->selectedTimelineIndex = m_session->timelines.size() - 1;

    refreshTimelineList();
    m_timelineList->setCurrentRow(m_session->selectedTimelineIndex);

    m_undoStack->push(new TimelineAddCommand(
        &m_session->timelines,
        alias,
        &m_session->selectedTimelineIndex,
        [this]() {
            refreshTimelineList();
            if (m_session->selectedTimelineIndex >= 0) {
                m_timelineList->setCurrentRow(m_session->selectedTimelineIndex);
            } else {
                onTimelineSelectionChanged();
            }
            refreshTimelineFrames();
            refreshAnimationTest();
        }
    ));
}

void MainWindow::onTimelineFlipChanged(int index) {
    const int idx = m_session->selectedTimelineIndex;
    if (idx < 0 || idx >= m_session->timelines.size()) return;
    auto& tl = m_session->timelines[idx];
    const bool oldH = tl.hFlip;
    const bool oldV = tl.vFlip;
    const bool newH = (index == 1 || index == 3);
    const bool newV = (index == 2 || index == 3);
    if (oldH == newH && oldV == newV) return;
    tl.hFlip = newH;
    tl.vFlip = newV;
    refreshAnimationTest();
    m_undoStack->push(new SetTimelineFlipCommand(
        &m_session->timelines, idx, oldH, oldV, newH, newV,
        [this]() { refreshAnimationTest(); }
    ));
}
