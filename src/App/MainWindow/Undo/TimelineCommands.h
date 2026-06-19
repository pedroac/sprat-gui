#pragma once
#include <QUndoCommand>
#include <QVector>
#include <QStringList>
#include <QPair>
#include <functional>
#include <QApplication>
#include "../../../Core/AnimationModels.h"

// ---------------------------------------------------------------------------
// (1002) TimelineFrameDropCommand
// ---------------------------------------------------------------------------
class TimelineFrameDropCommand : public QUndoCommand {
public:
    TimelineFrameDropCommand(QVector<AnimationTimeline>* timelines, int timelineIndex,
                              const QString& path, int insertedIndex,
                              std::function<void()> postExecute,
                              QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Drop Frame"), parent)
        , m_timelines(timelines)
        , m_timelineIndex(timelineIndex)
        , m_path(path)
        , m_insertedIndex(insertedIndex)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_timelineIndex < 0 || m_timelineIndex >= m_timelines->size()) return;
        auto& frames = (*m_timelines)[m_timelineIndex].frames;
        int idx = qMin(m_insertedIndex, (int)frames.size());
        frames.insert(idx, m_path);
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        if (m_timelineIndex < 0 || m_timelineIndex >= m_timelines->size()) return;
        auto& frames = (*m_timelines)[m_timelineIndex].frames;
        if (m_insertedIndex >= 0 && m_insertedIndex < frames.size()) {
            frames.removeAt(m_insertedIndex);
        }
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1002; }

private:
    QVector<AnimationTimeline>* m_timelines;
    int m_timelineIndex;
    QString m_path;
    int m_insertedIndex;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1003) TimelineFrameMoveCommand
// ---------------------------------------------------------------------------
class TimelineFrameMoveCommand : public QUndoCommand {
public:
    TimelineFrameMoveCommand(QVector<AnimationTimeline>* timelines, int timelineIndex,
                              int from, int to,
                              const QStringList& savedFramesBefore,
                              std::function<void()> postExecute,
                              QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Move Frame"), parent)
        , m_timelines(timelines)
        , m_timelineIndex(timelineIndex)
        , m_from(from)
        , m_to(to)
        , m_savedFramesBefore(savedFramesBefore)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_timelineIndex < 0 || m_timelineIndex >= m_timelines->size()) return;
        auto& frames = (*m_timelines)[m_timelineIndex].frames;
        if (m_from < 0 || m_from >= frames.size()) return;
        QString path = frames.takeAt(m_from);
        int to = m_to;
        if (to > m_from) to--;
        to = qBound(0, to, (int)frames.size());
        frames.insert(to, path);
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        if (m_timelineIndex < 0 || m_timelineIndex >= m_timelines->size()) return;
        (*m_timelines)[m_timelineIndex].frames = m_savedFramesBefore;
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1003; }

private:
    QVector<AnimationTimeline>* m_timelines;
    int m_timelineIndex;
    int m_from, m_to;
    QStringList m_savedFramesBefore;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1004) TimelineFrameRemoveCommand
// ---------------------------------------------------------------------------
class TimelineFrameRemoveCommand : public QUndoCommand {
public:
    TimelineFrameRemoveCommand(QVector<AnimationTimeline>* timelines, int timelineIndex,
                                const QVector<QPair<int,QString>>& removed,
                                std::function<void()> postExecute,
                                QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Remove Frame(s)"), parent)
        , m_timelines(timelines)
        , m_timelineIndex(timelineIndex)
        , m_removed(removed)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_timelineIndex < 0 || m_timelineIndex >= m_timelines->size()) return;
        auto& frames = (*m_timelines)[m_timelineIndex].frames;
        for (int i = m_removed.size() - 1; i >= 0; --i) {
            int idx = m_removed[i].first;
            if (idx >= 0 && idx < frames.size()) {
                frames.removeAt(idx);
            }
        }
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        if (m_timelineIndex < 0 || m_timelineIndex >= m_timelines->size()) return;
        auto& frames = (*m_timelines)[m_timelineIndex].frames;
        for (const auto& pair : m_removed) {
            int idx = qMin(pair.first, (int)frames.size());
            frames.insert(idx, pair.second);
        }
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1004; }

private:
    QVector<AnimationTimeline>* m_timelines;
    int m_timelineIndex;
    QVector<QPair<int,QString>> m_removed;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1005) TimelineFrameDuplicateCommand
// ---------------------------------------------------------------------------
class TimelineFrameDuplicateCommand : public QUndoCommand {
public:
    TimelineFrameDuplicateCommand(QVector<AnimationTimeline>* timelines, int timelineIndex,
                                   int index, const QString& path,
                                   std::function<void()> postExecute,
                                   QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Duplicate Frame"), parent)
        , m_timelines(timelines)
        , m_timelineIndex(timelineIndex)
        , m_index(index)
        , m_path(path)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_timelineIndex < 0 || m_timelineIndex >= m_timelines->size()) return;
        auto& frames = (*m_timelines)[m_timelineIndex].frames;
        if (m_index < 0 || m_index >= frames.size()) return;
        frames.insert(m_index + 1, m_path);
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        if (m_timelineIndex < 0 || m_timelineIndex >= m_timelines->size()) return;
        auto& frames = (*m_timelines)[m_timelineIndex].frames;
        int dupIdx = m_index + 1;
        if (dupIdx >= 0 && dupIdx < frames.size()) {
            frames.removeAt(dupIdx);
        }
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1005; }

private:
    QVector<AnimationTimeline>* m_timelines;
    int m_timelineIndex;
    int m_index;
    QString m_path;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1006) TimelineAddCommand
// ---------------------------------------------------------------------------
class TimelineAddCommand : public QUndoCommand {
public:
    TimelineAddCommand(QVector<AnimationTimeline>* timelines,
                        const AnimationTimeline& timeline,
                        int* selectedTimelineIndex,
                        std::function<void()> postExecute,
                        QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Add Timeline"), parent)
        , m_timelines(timelines)
        , m_timeline(timeline)
        , m_selectedTimelineIndex(selectedTimelineIndex)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {
        m_addedIndex = m_timelines->size() - 1;
    }

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        m_addedIndex = m_timelines->size();
        m_timelines->append(m_timeline);
        *m_selectedTimelineIndex = m_timelines->size() - 1;
        if (m_postExecute) {
            QMetaObject::invokeMethod(QApplication::instance(), m_postExecute, Qt::QueuedConnection);
        }
    }

    void undo() override {
        if (m_addedIndex >= 0 && m_addedIndex < m_timelines->size()) {
            m_timelines->removeAt(m_addedIndex);
        }
        *m_selectedTimelineIndex = qMax(-1, m_timelines->size() - 1);
        if (m_postExecute) {
            QMetaObject::invokeMethod(QApplication::instance(), m_postExecute, Qt::QueuedConnection);
        }
    }

    int id() const override { return 1006; }

private:
    QVector<AnimationTimeline>* m_timelines;
    AnimationTimeline m_timeline;
    int* m_selectedTimelineIndex;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
    int m_addedIndex = -1;
};

// ---------------------------------------------------------------------------
// (1007) TimelinesUpdateCommand
// ---------------------------------------------------------------------------
class TimelinesUpdateCommand : public QUndoCommand {
public:
    TimelinesUpdateCommand(QVector<AnimationTimeline>* timelines,
                          const QVector<AnimationTimeline>& oldState,
                          const QVector<AnimationTimeline>& newState,
                          int oldSelection,
                          int newSelection,
                          int* selectedIndexPtr,
                          std::function<void()> postExecute,
                          const QString& text = QObject::tr("Update Timelines"),
                          QUndoCommand* parent = nullptr)
        : QUndoCommand(text, parent)
        , m_timelines(timelines)
        , m_oldState(oldState)
        , m_newState(newState)
        , m_oldSelection(oldSelection)
        , m_newSelection(newSelection)
        , m_selectedIndexPtr(selectedIndexPtr)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        *m_timelines = m_newState;
        *m_selectedIndexPtr = m_newSelection;
        if (m_postExecute) {
            QMetaObject::invokeMethod(QApplication::instance(), m_postExecute, Qt::QueuedConnection);
        }
    }

    void undo() override {
        *m_timelines = m_oldState;
        *m_selectedIndexPtr = m_oldSelection;
        if (m_postExecute) {
            QMetaObject::invokeMethod(QApplication::instance(), m_postExecute, Qt::QueuedConnection);
        }
    }

    int id() const override { return 1007; }

private:
    QVector<AnimationTimeline>* m_timelines;
    QVector<AnimationTimeline> m_oldState;
    QVector<AnimationTimeline> m_newState;
    int m_oldSelection;
    int m_newSelection;
    int* m_selectedIndexPtr;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1008) TimelineRemoveCommand
// ---------------------------------------------------------------------------
class TimelineRemoveCommand : public QUndoCommand {
public:
    TimelineRemoveCommand(QVector<AnimationTimeline>* timelines,
                           int index,
                           const AnimationTimeline& savedTimeline,
                           int* selectedTimelineIndex,
                           std::function<void()> postExecute,
                           QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Remove Timeline"), parent)
        , m_timelines(timelines)
        , m_index(index)
        , m_savedTimeline(savedTimeline)
        , m_selectedTimelineIndex(selectedTimelineIndex)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_index >= 0 && m_index < m_timelines->size()) {
            m_timelines->removeAt(m_index);
        }
        *m_selectedTimelineIndex = qMin(m_index, m_timelines->size() - 1);
        if (m_postExecute) {
            QMetaObject::invokeMethod(QApplication::instance(), m_postExecute, Qt::QueuedConnection);
        }
    }

    void undo() override {
        int insertIdx = qMin(m_index, (int)m_timelines->size());
        m_timelines->insert(insertIdx, m_savedTimeline);
        *m_selectedTimelineIndex = insertIdx;
        if (m_postExecute) {
            QMetaObject::invokeMethod(QApplication::instance(), m_postExecute, Qt::QueuedConnection);
        }
    }

    int id() const override { return 1008; }

private:
    QVector<AnimationTimeline>* m_timelines;
    int m_index;
    AnimationTimeline m_savedTimeline;
    int* m_selectedTimelineIndex;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1015) SetTimelineNameCommand
// ---------------------------------------------------------------------------
class SetTimelineNameCommand : public QUndoCommand {
public:
    SetTimelineNameCommand(QVector<AnimationTimeline>* timelines, int index,
                            const QString& oldName, const QString& newName,
                            std::function<void()> postExecute,
                            QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Rename Timeline"), parent)
        , m_timelines(timelines), m_index(index)
        , m_oldName(oldName), m_newName(newName)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_index >= 0 && m_index < m_timelines->size())
            (*m_timelines)[m_index].name = m_newName;
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        if (m_index >= 0 && m_index < m_timelines->size())
            (*m_timelines)[m_index].name = m_oldName;
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1016; }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* o = static_cast<const SetTimelineNameCommand*>(other);
        if (o->m_timelines != m_timelines || o->m_index != m_index) return false;
        m_newName = o->m_newName;
        return true;
    }

private:
    QVector<AnimationTimeline>* m_timelines;
    int m_index;
    QString m_oldName, m_newName;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1016) SetTimelineFpsCommand
// ---------------------------------------------------------------------------
class SetTimelineFpsCommand : public QUndoCommand {
public:
    SetTimelineFpsCommand(QVector<AnimationTimeline>* timelines, int index,
                           int oldFps, int newFps,
                           std::function<void()> postExecute,
                           QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Change Timeline FPS"), parent)
        , m_timelines(timelines), m_index(index)
        , m_oldFps(oldFps), m_newFps(newFps)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_index >= 0 && m_index < m_timelines->size())
            (*m_timelines)[m_index].fps = m_newFps;
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        if (m_index >= 0 && m_index < m_timelines->size())
            (*m_timelines)[m_index].fps = m_oldFps;
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1017; }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* o = static_cast<const SetTimelineFpsCommand*>(other);
        if (o->m_timelines != m_timelines || o->m_index != m_index) return false;
        m_newFps = o->m_newFps;
        return true;
    }

private:
    QVector<AnimationTimeline>* m_timelines;
    int m_index;
    int m_oldFps, m_newFps;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1022) SetTimelineFlipCommand
// ---------------------------------------------------------------------------
class SetTimelineFlipCommand : public QUndoCommand {
public:
    SetTimelineFlipCommand(QVector<AnimationTimeline>* timelines, int index,
                           bool oldH, bool oldV, bool newH, bool newV,
                           std::function<void()> postExecute,
                           QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Change Timeline Flip"), parent)
        , m_timelines(timelines), m_index(index)
        , m_oldH(oldH), m_oldV(oldV), m_newH(newH), m_newV(newV)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_index >= 0 && m_index < m_timelines->size()) {
            (*m_timelines)[m_index].hFlip = m_newH;
            (*m_timelines)[m_index].vFlip = m_newV;
        }
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        if (m_index >= 0 && m_index < m_timelines->size()) {
            (*m_timelines)[m_index].hFlip = m_oldH;
            (*m_timelines)[m_index].vFlip = m_oldV;
        }
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1022; }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* o = static_cast<const SetTimelineFlipCommand*>(other);
        if (o->m_timelines != m_timelines || o->m_index != m_index) return false;
        m_newH = o->m_newH;
        m_newV = o->m_newV;
        return true;
    }

private:
    QVector<AnimationTimeline>* m_timelines;
    int m_index;
    bool m_oldH, m_oldV, m_newH, m_newV;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};
