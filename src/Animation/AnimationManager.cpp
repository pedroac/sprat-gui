#include "AnimationManager.h"
#include <QTimer>

AnimationManager::AnimationManager(QObject* parent) : QObject(parent), m_timer(new QTimer(this)) {
    connect(m_timer, &QTimer::timeout, this, &AnimationManager::onTimerTimeout);
}

AnimationManager::~AnimationManager() {
    delete m_timer;
}

void AnimationManager::setAnimationData(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex) {
    m_timelines = timelines;
    m_selectedTimelineIndex = selectedTimelineIndex;
    m_currentFrameIndex = 0;
}

void AnimationManager::startAnimation() {
    if (m_timelines.isEmpty() || m_selectedTimelineIndex < 0 || m_selectedTimelineIndex >= m_timelines.size()) return;

    m_timer->start();
}

void AnimationManager::stopAnimation() {
    m_timer->stop();
}

void AnimationManager::setFps(int fps) {
    m_timer->setInterval(1000 / fps);
}

void AnimationManager::onTimerTimeout() {
    if (m_timelines.isEmpty() || m_selectedTimelineIndex < 0 || m_selectedTimelineIndex >= m_timelines.size()) return;

    const auto& timeline = m_timelines[m_selectedTimelineIndex];
    if (timeline.frames.isEmpty()) return;

    m_currentFrameIndex = (m_currentFrameIndex + 1) % timeline.frames.size();
    emit frameChanged(timeline.frames[m_currentFrameIndex]);
}