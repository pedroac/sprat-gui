#pragma once
#include <QUndoCommand>
#include <QPair>
#include <QVector>
#include <QSet>
#include <QStringList>
#include <QImage>
#include <QFile>
#include <QRect>
#include <functional>
#include <algorithm>
#include "../../../Core/AnimationModels.h"
#include "../../../Core/LayoutModels.h"
#include "../AnimationPreviewService.h"

// ---------------------------------------------------------------------------
// (1001) SetPivotCommand
// ---------------------------------------------------------------------------
class SetPivotCommand : public QUndoCommand {
public:
    struct CoTarget {
        SpritePtr sprite;
        QPair<int, int> oldPos;
        QPair<int, int> newPos;
    };

    SetPivotCommand(SpritePtr sprite, int oldX, int oldY, int newX, int newY,
                    bool alreadyApplied = false,
                    QVector<CoTarget> coTargets = {},
                    QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Set Pivot"), parent)
        , m_sprite(sprite)
        , m_oldX(oldX), m_oldY(oldY), m_newX(newX), m_newY(newY)
        , m_skipFirstRedo(alreadyApplied)
        , m_coTargets(std::move(coTargets))
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_sprite) {
            m_sprite->pivotX = m_newX;
            m_sprite->pivotY = m_newY;
        }
        for (const auto& target : m_coTargets) {
            if (target.sprite) {
                target.sprite->pivotX = target.newPos.first;
                target.sprite->pivotY = target.newPos.second;
            }
        }
        AnimationPreviewService::invalidateBounds();
    }

    void undo() override {
        if (m_sprite) {
            m_sprite->pivotX = m_oldX;
            m_sprite->pivotY = m_oldY;
        }
        for (const auto& target : m_coTargets) {
            if (target.sprite) {
                target.sprite->pivotX = target.oldPos.first;
                target.sprite->pivotY = target.oldPos.second;
            }
        }
        AnimationPreviewService::invalidateBounds();
    }

    int id() const override { return 1001; }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* o = static_cast<const SetPivotCommand*>(other);
        if (o->m_sprite != m_sprite) return false;
        if (o->m_coTargets.size() != m_coTargets.size()) return false;
        for (int i = 0; i < m_coTargets.size(); ++i) {
            if (m_coTargets[i].sprite != o->m_coTargets[i].sprite) return false;
        }
        m_newX = o->m_newX;
        m_newY = o->m_newY;
        for (int i = 0; i < m_coTargets.size(); ++i) {
            m_coTargets[i].newPos = o->m_coTargets[i].newPos;
        }
        return true;
    }

private:
    SpritePtr m_sprite;
    int m_oldX, m_oldY, m_newX, m_newY;
    mutable bool m_skipFirstRedo = false;
    QVector<CoTarget> m_coTargets;
};

// ---------------------------------------------------------------------------
// (1014) SplitSpriteCommand
// ---------------------------------------------------------------------------
class SplitSpriteCommand : public QUndoCommand {
public:
    SplitSpriteCommand(QStringList* activeFramePaths,
                        const QString& originalPath,
                        const QString& pathA,
                        const QString& pathB,
                        int insertedIdx,
                        Qt::Orientation orientation,
                        int localPos,
                        bool rotated,
                        const QRect& trimRect,
                        std::function<bool()> ensureFrameList,
                        std::function<void()> postExecute,
                        QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Split Sprite"), parent)
        , m_activeFramePaths(activeFramePaths)
        , m_originalPath(originalPath)
        , m_pathA(pathA)
        , m_pathB(pathB)
        , m_insertedIdx(insertedIdx)
        , m_orientation(orientation)
        , m_localPos(localPos)
        , m_rotated(rotated)
        , m_trimRect(trimRect)
        , m_ensureFrameList(std::move(ensureFrameList))
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        QImage originalImage(m_originalPath);
        if (originalImage.isNull()) return;

        int l = m_trimRect.x();
        int t = m_trimRect.y();
        int trimmedH = originalImage.height() - t - m_trimRect.height();

        QImage imgA, imgB;
        if (!m_rotated) {
            if (m_orientation == Qt::Horizontal) {
                int origY = qBound(1, m_localPos + t, originalImage.height() - 1);
                imgA = originalImage.copy(0, 0, originalImage.width(), origY);
                imgB = originalImage.copy(0, origY, originalImage.width(), originalImage.height() - origY);
            } else {
                int origX = qBound(1, m_localPos + l, originalImage.width() - 1);
                imgA = originalImage.copy(0, 0, origX, originalImage.height());
                imgB = originalImage.copy(origX, 0, originalImage.width() - origX, originalImage.height());
            }
        } else {
            if (m_orientation == Qt::Horizontal) {
                int origX = qBound(1, m_localPos + l, originalImage.width() - 1);
                imgA = originalImage.copy(0, 0, origX, originalImage.height());
                imgB = originalImage.copy(origX, 0, originalImage.width() - origX, originalImage.height());
            } else {
                int origY = qBound(1, (trimmedH - 1 - m_localPos) + t, originalImage.height() - 1);
                imgA = originalImage.copy(0, 0, originalImage.width(), origY);
                imgB = originalImage.copy(0, origY, originalImage.width(), originalImage.height() - origY);
            }
        }
        if (imgA.isNull() || imgB.isNull()) return;
        imgA.save(m_pathA);
        imgB.save(m_pathB);

        m_activeFramePaths->removeAll(m_originalPath);
        int idx = qMin(m_insertedIdx, (int)m_activeFramePaths->size());
        m_activeFramePaths->insert(idx, m_pathB);
        m_activeFramePaths->insert(idx, m_pathA);

        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        QFile::remove(m_pathA);
        QFile::remove(m_pathB);
        m_activeFramePaths->removeAll(m_pathA);
        m_activeFramePaths->removeAll(m_pathB);
        int idx = qMin(m_insertedIdx, (int)m_activeFramePaths->size());
        m_activeFramePaths->insert(idx, m_originalPath);
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1015; }

private:
    QStringList* m_activeFramePaths;
    QString m_originalPath;
    QString m_pathA;
    QString m_pathB;
    int m_insertedIdx;
    Qt::Orientation m_orientation;
    int m_localPos;
    bool m_rotated;
    QRect m_trimRect;
    std::function<bool()> m_ensureFrameList;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};

// ---------------------------------------------------------------------------
// (1023) SetSpriteNamesCommand
// ---------------------------------------------------------------------------
class SetSpriteNamesCommand : public QUndoCommand {
public:
    SetSpriteNamesCommand(SpritePtr sprite,
                          const QString& oldName,    const QStringList& oldAliases,
                          const QString& newName,    const QStringList& newAliases,
                          std::function<void()> postExecute,
                          QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Edit Sprite Name"), parent)
        , m_sprite(sprite)
        , m_oldName(oldName), m_newName(newName)
        , m_oldAliases(oldAliases), m_newAliases(newAliases)
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        if (m_sprite) {
            m_sprite->name    = m_newName;
            m_sprite->aliases = m_newAliases;
        }
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        if (m_sprite) {
            m_sprite->name    = m_oldName;
            m_sprite->aliases = m_oldAliases;
        }
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1023; }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* o = static_cast<const SetSpriteNamesCommand*>(other);
        if (o->m_sprite != m_sprite) return false;
        m_newName    = o->m_newName;
        m_newAliases = o->m_newAliases;
        return true;
    }

private:
    SpritePtr m_sprite;
    QString m_oldName, m_newName;
    QStringList m_oldAliases, m_newAliases;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo = true;
};

// ---------------------------------------------------------------------------
// (1013) RemoveSpritesCommand
// ---------------------------------------------------------------------------
class RemoveSpritesCommand : public QUndoCommand {
public:
    RemoveSpritesCommand(QStringList* activeFramePaths,
                          QVector<AnimationTimeline>* timelines,
                          int* selectedTimelineIndex,
                          QVector<LayoutModel>* layoutModels,
                          const QStringList& targets,
                          const QStringList& savedActivePaths,
                          const QVector<AnimationTimeline>& savedTimelines,
                          int savedTimelineIdx,
                          const QVector<LayoutModel>& savedLayoutModels,
                          std::function<bool()> ensureFrameList,
                          std::function<void()> postExecuteRedo,
                          std::function<void()> postExecuteUndo,
                          QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Exclude from Layout"), parent)
        , m_activeFramePaths(activeFramePaths)
        , m_timelines(timelines)
        , m_selectedTimelineIndex(selectedTimelineIndex)
        , m_layoutModels(layoutModels)
        , m_targets(targets)
        , m_savedActivePaths(savedActivePaths)
        , m_savedTimelines(savedTimelines)
        , m_savedTimelineIdx(savedTimelineIdx)
        , m_savedLayoutModels(savedLayoutModels)
        , m_ensureFrameList(std::move(ensureFrameList))
        , m_postExecuteRedo(std::move(postExecuteRedo))
        , m_postExecuteUndo(std::move(postExecuteUndo))
    {}

    void redo() override {
        for (const QString& path : m_targets) {
            m_activeFramePaths->removeAll(path);
        }
        const QSet<QString> targetSet(m_targets.begin(), m_targets.end());
        for (auto& timeline : *m_timelines) {
            for (int i = timeline.frames.size() - 1; i >= 0; --i) {
                if (targetSet.contains(timeline.frames[i])) {
                    timeline.frames.removeAt(i);
                }
            }
        }
        for (int i = m_timelines->size() - 1; i >= 0; --i) {
            if ((*m_timelines)[i].frames.isEmpty()) {
                m_timelines->removeAt(i);
                if (*m_selectedTimelineIndex > i) {
                    --(*m_selectedTimelineIndex);
                } else if (*m_selectedTimelineIndex == i) {
                    *m_selectedTimelineIndex = -1;
                }
            }
        }
        for (auto& model : *m_layoutModels) {
            model.sprites.erase(
                std::remove_if(model.sprites.begin(), model.sprites.end(),
                    [&targetSet](const SpritePtr& s) {
                        return s && targetSet.contains(s->path);
                    }),
                model.sprites.end());
        }
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecuteRedo) m_postExecuteRedo();
    }

    void undo() override {
        *m_activeFramePaths = m_savedActivePaths;
        *m_timelines = m_savedTimelines;
        *m_selectedTimelineIndex = m_savedTimelineIdx;
        *m_layoutModels = m_savedLayoutModels;
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecuteUndo) m_postExecuteUndo();
    }

    int id() const override { return 1014; }

private:
    QStringList* m_activeFramePaths;
    QVector<AnimationTimeline>* m_timelines;
    int* m_selectedTimelineIndex;
    QVector<LayoutModel>* m_layoutModels;
    QStringList m_targets;
    QStringList m_savedActivePaths;
    QVector<AnimationTimeline> m_savedTimelines;
    int m_savedTimelineIdx;
    QVector<LayoutModel> m_savedLayoutModels;
    std::function<bool()> m_ensureFrameList;
    std::function<void()> m_postExecuteRedo;
    std::function<void()> m_postExecuteUndo;
};

// ---------------------------------------------------------------------------
// (1028) RemoveFramesCommand
// ---------------------------------------------------------------------------
class RemoveFramesCommand : public QUndoCommand {
public:
    RemoveFramesCommand(QStringList* activeFramePaths,
                         QVector<AnimationTimeline>* timelines,
                         int* selectedTimelineIndex,
                         QVector<LayoutModel>* layoutModels,
                         const QStringList& targets,
                         const QStringList& savedActivePaths,
                         const QVector<AnimationTimeline>& savedTimelines,
                         int savedTimelineIdx,
                         const QVector<LayoutModel>& savedLayoutModels,
                         std::function<bool()> ensureFrameList,
                         std::function<void()> postExecuteRedo,
                         std::function<void()> postExecuteUndo,
                         QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Remove Frames"), parent)
        , m_activeFramePaths(activeFramePaths)
        , m_timelines(timelines)
        , m_selectedTimelineIndex(selectedTimelineIndex)
        , m_layoutModels(layoutModels)
        , m_targets(targets)
        , m_savedActivePaths(savedActivePaths)
        , m_savedTimelines(savedTimelines)
        , m_savedTimelineIdx(savedTimelineIdx)
        , m_savedLayoutModels(savedLayoutModels)
        , m_ensureFrameList(std::move(ensureFrameList))
        , m_postExecuteRedo(std::move(postExecuteRedo))
        , m_postExecuteUndo(std::move(postExecuteUndo))
    {}

    void redo() override {
        for (const QString& path : m_targets) {
            m_activeFramePaths->removeAll(path);
        }
        const QSet<QString> targetSet(m_targets.begin(), m_targets.end());
        for (auto& timeline : *m_timelines) {
            for (int i = timeline.frames.size() - 1; i >= 0; --i) {
                if (targetSet.contains(timeline.frames[i])) {
                    timeline.frames.removeAt(i);
                }
            }
        }
        for (int i = m_timelines->size() - 1; i >= 0; --i) {
            if ((*m_timelines)[i].frames.isEmpty()) {
                m_timelines->removeAt(i);
                if (*m_selectedTimelineIndex > i) {
                    --(*m_selectedTimelineIndex);
                } else if (*m_selectedTimelineIndex == i) {
                    *m_selectedTimelineIndex = -1;
                }
            }
        }
        for (auto& model : *m_layoutModels) {
            model.sprites.erase(
                std::remove_if(model.sprites.begin(), model.sprites.end(),
                    [&targetSet](const SpritePtr& s) {
                        return s && targetSet.contains(s->path);
                    }),
                model.sprites.end());
        }
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecuteRedo) m_postExecuteRedo();
    }

    void undo() override {
        *m_activeFramePaths = m_savedActivePaths;
        *m_timelines = m_savedTimelines;
        *m_selectedTimelineIndex = m_savedTimelineIdx;
        *m_layoutModels = m_savedLayoutModels;
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecuteUndo) m_postExecuteUndo();
    }

    int id() const override { return 1028; }

private:
    QStringList* m_activeFramePaths;
    QVector<AnimationTimeline>* m_timelines;
    int* m_selectedTimelineIndex;
    QVector<LayoutModel>* m_layoutModels;
    QStringList m_targets;
    QStringList m_savedActivePaths;
    QVector<AnimationTimeline> m_savedTimelines;
    int m_savedTimelineIdx;
    QVector<LayoutModel> m_savedLayoutModels;
    std::function<bool()> m_ensureFrameList;
    std::function<void()> m_postExecuteRedo;
    std::function<void()> m_postExecuteUndo;
};

// ---------------------------------------------------------------------------
// (1019) AddFramesCommand
// ---------------------------------------------------------------------------
class AddFramesCommand : public QUndoCommand {
public:
    AddFramesCommand(QStringList* activeFramePaths,
                      const QStringList& addedPaths,
                      const QStringList& oldPaths,
                      std::function<bool()> ensureFrameList,
                      std::function<void()> postExecute,
                      QUndoCommand* parent = nullptr)
        : QUndoCommand(QObject::tr("Add Frames"), parent)
        , m_activeFramePaths(activeFramePaths)
        , m_addedPaths(addedPaths)
        , m_oldPaths(oldPaths)
        , m_ensureFrameList(std::move(ensureFrameList))
        , m_postExecute(std::move(postExecute))
        , m_skipFirstRedo(true)
    {}

    void redo() override {
        if (m_skipFirstRedo) { m_skipFirstRedo = false; return; }
        *m_activeFramePaths = m_oldPaths;
        m_activeFramePaths->append(m_addedPaths);
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecute) m_postExecute();
    }

    void undo() override {
        *m_activeFramePaths = m_oldPaths;
        if (m_ensureFrameList) m_ensureFrameList();
        if (m_postExecute) m_postExecute();
    }

    int id() const override { return 1020; }

private:
    QStringList* m_activeFramePaths;
    QStringList m_addedPaths;
    QStringList m_oldPaths;
    std::function<bool()> m_ensureFrameList;
    std::function<void()> m_postExecute;
    mutable bool m_skipFirstRedo;
};
