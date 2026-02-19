#pragma once

#include <QObject>
#include <QTimer>
#include "models.h"

class QLabel;

class AnimationManager : public QObject {
    Q_OBJECT
public:
    AnimationManager(QObject* parent = nullptr);
    ~AnimationManager() override;

    void setAnimationData(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex);
    void startAnimation();
    void stopAnimation();
    void setFps(int fps);

signals:
    void frameChanged(const QString& framePath);

private slots:
    void onTimerTimeout();

private:
    QTimer* m_timer;
    QVector<AnimationTimeline> m_timelines;
    int m_selectedTimelineIndex = -1;
    int m_currentFrameIndex = 0;
};