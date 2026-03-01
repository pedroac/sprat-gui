#include "MainWindow.h"

#include <QLineEdit>
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

    m_timelineCreateEdit->clear();
    refreshTimelineList();
    m_timelineList->setCurrentRow(m_session->timelines.size() - 1);
}

void MainWindow::onTimelineRemoveClicked() {
    if (m_session->selectedTimelineIndex < 0 || m_session->selectedTimelineIndex >= m_session->timelines.size()) {
        return;
    }
    m_session->timelines.removeAt(m_session->selectedTimelineIndex);
    refreshTimelineList();
    const int newIndex = qMin(m_session->selectedTimelineIndex, m_session->timelines.size() - 1);
    if (newIndex >= 0) {
        m_timelineList->setCurrentRow(newIndex);
    }
}

void MainWindow::onTimelineSelectionChanged() {
    const int row = m_timelineList->currentRow();
    if (row >= 0 && row < m_session->timelines.size()) {
        m_session->selectedTimelineIndex = row;
        m_timelineNameEdit->setText(m_session->timelines[row].name);
        m_timelineNameEdit->setEnabled(true);
        {
            const QSignalBlocker blocker(m_timelineFpsSpin);
            m_timelineFpsSpin->setValue(m_session->timelines[row].fps);
        }
        m_timelineFpsSpin->setEnabled(true);
        if (m_animPlaying) {
            m_animTimer->setInterval(1000 / m_session->timelines[row].fps);
        }
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
    m_timelineEditorContainer->setVisible(false);
    m_timelineFramesList->clear();
    refreshAnimationTest();
}

void MainWindow::onTimelineNameChanged() {
    if (m_session->selectedTimelineIndex < 0 || m_session->selectedTimelineIndex >= m_session->timelines.size()) {
        return;
    }
    const QString renamed = m_timelineNameEdit->text();
    m_session->timelines[m_session->selectedTimelineIndex].name = renamed;
    m_timelineList->item(m_session->selectedTimelineIndex)->setText(renamed);
}

void MainWindow::onTimelineFpsChanged(int fps) {
    if (m_session->selectedTimelineIndex < 0 || m_session->selectedTimelineIndex >= m_session->timelines.size()) {
        return;
    }
    m_session->timelines[m_session->selectedTimelineIndex].fps = fps;
    if (m_animPlaying) {
        m_animTimer->setInterval(1000 / fps);
    }
    refreshAnimationTest();
}
