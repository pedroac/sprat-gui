#include "MainWindow.h"

#include <QGroupBox>
#include <QLineEdit>
#include <QListWidget>

void MainWindow::onTimelineAddClicked() {
    QString name = m_timelineCreateEdit->text().trimmed();
    if (name.isEmpty()) {
        name = QString("Timeline %1").arg(m_timelines.size() + 1);
    }

    AnimationTimeline timeline;
    timeline.name = name;
    m_timelines.append(timeline);

    m_timelineCreateEdit->clear();
    refreshTimelineList();
    m_timelineList->setCurrentRow(m_timelines.size() - 1);
}

void MainWindow::onTimelineRemoveClicked() {
    if (m_selectedTimelineIndex < 0 || m_selectedTimelineIndex >= m_timelines.size()) {
        return;
    }
    m_timelines.removeAt(m_selectedTimelineIndex);
    refreshTimelineList();
    const int newIndex = qMin(m_selectedTimelineIndex, m_timelines.size() - 1);
    if (newIndex >= 0) {
        m_timelineList->setCurrentRow(newIndex);
    }
}

void MainWindow::onTimelineSelectionChanged() {
    const int row = m_timelineList->currentRow();
    if (row >= 0 && row < m_timelines.size()) {
        m_selectedTimelineIndex = row;
        m_timelineNameEdit->setText(m_timelines[row].name);
        m_timelineNameEdit->setEnabled(true);
        refreshTimelineFrames();
        m_animFrameIndex = 0;
        m_timelineEditorContainer->setVisible(true);
        refreshAnimationTest();
        return;
    }
    m_selectedTimelineIndex = -1;
    m_timelineNameEdit->clear();
    m_timelineNameEdit->setEnabled(false);
    m_timelineEditorContainer->setVisible(false);
    m_timelineFramesList->clear();
    refreshAnimationTest();
}

void MainWindow::onTimelineNameChanged() {
    if (m_selectedTimelineIndex < 0 || m_selectedTimelineIndex >= m_timelines.size()) {
        return;
    }
    const QString renamed = m_timelineNameEdit->text();
    m_timelines[m_selectedTimelineIndex].name = renamed;
    m_timelineList->item(m_selectedTimelineIndex)->setText(renamed);
}
