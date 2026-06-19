#include "MainWindow.h"
#include "SpriteEditorPanel.h"
#include "FrameAnimationWorkspace.h"
#include "TimelineEditorPanel.h"

#include "SpriteSelectionPresenter.h"

void MainWindow::refreshTimelineList() {
    if (m_frameAnimWorkspace) m_frameAnimWorkspace->refreshTimelineList();
}

void MainWindow::refreshTimelineFrames() {
    if (m_frameAnimWorkspace) m_frameAnimWorkspace->refreshTimelineFrames();
}

void MainWindow::onGenerateTimelinesFromFrames() {
    auto* tp = m_frameAnimWorkspace ? m_frameAnimWorkspace->timelinePanel() : nullptr;
    if (tp) tp->onGenerateTimelinesFromFrames();
}

void MainWindow::refreshAnimationTest() {
    if (m_frameAnimWorkspace) m_frameAnimWorkspace->refreshAnimationTest();
}

void MainWindow::fitAnimationToViewport() {
    if (m_frameAnimWorkspace) m_frameAnimWorkspace->fitAnimationToViewport();
}

void MainWindow::saveAnimationToFile() {
    if (m_frameAnimWorkspace) m_frameAnimWorkspace->saveAnimationToFile();
}

bool MainWindow::exportAnimation(const QString& outPath) {
    return m_frameAnimWorkspace ? m_frameAnimWorkspace->exportAnimation(outPath) : false;
}

void MainWindow::refreshHandleCombo() {
    SpriteSelectionPresenter::refreshHandleCombo(
        m_atlasWorkspace ? m_atlasWorkspace->spriteEditorPanel()->handleCombo() : nullptr,
        m_session->selectedSprite, m_session->selectedPointName);
    if (m_frameAnimWorkspace) m_frameAnimWorkspace->refreshHandleCombo();
}
