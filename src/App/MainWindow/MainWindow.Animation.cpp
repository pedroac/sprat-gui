#include "MainWindow.h"
#include "MessageDialog.h"
#include "AnimationCanvas.h"
#include "UndoCommands.h"

#include "AnimationExportService.h"
#include "AnimationPlaybackService.h"
#include "AnimationPreviewService.h"
#include "AnimationTimelineOps.h"
#include "SpriteSelectionPresenter.h"
#include "TimelineGenerationService.h"
#include "TimelineUi.h"

#include <QDoubleSpinBox>
#include <QApplication>
#include <QStyle>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QScrollBar>
#include <QSpinBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QtGlobal>
#include <algorithm>
#include <numeric>

namespace {
int selectedTimelineFps(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return 8;
    }
    return qMax(1, timelines[selectedTimelineIndex].fps);
}
}

QTreeWidgetItem* MainWindow::timelineItemForIndex(int idx) const {
    QTreeWidgetItemIterator it(m_timelineList);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).isValid() &&
            (*it)->data(0, Qt::UserRole).toInt() == idx) return *it;
        ++it;
    }
    return nullptr;
}

void MainWindow::refreshTimelineList() {
    const int savedIndex = m_session->selectedTimelineIndex;
    m_timelineList->blockSignals(true);
    m_timelineList->clear();

    // Build sorted order by timeline name (case-insensitive)
    const int count = m_session->timelines.size();
    QVector<int> sortedIndices(count);
    std::iota(sortedIndices.begin(), sortedIndices.end(), 0);
    std::sort(sortedIndices.begin(), sortedIndices.end(), [&](int a, int b) {
        return m_session->timelines[a].name.compare(
            m_session->timelines[b].name, Qt::CaseInsensitive) < 0;
    });

    const QIcon folderIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);

    // Find or create folder nodes along a path like ["hero", "walk"]
    auto findOrCreateFolderPath = [&](const QStringList& parts) -> QTreeWidgetItem* {
        QTreeWidgetItem* current = nullptr;
        for (int pi = 0; pi < parts.size(); ++pi) {
            const QString& part = parts[pi];
            QTreeWidgetItem* found = nullptr;
            int childCount = current ? current->childCount() : m_timelineList->topLevelItemCount();
            for (int i = 0; i < childCount; ++i) {
                QTreeWidgetItem* child = current ? current->child(i) : m_timelineList->topLevelItem(i);
                if (child->text(0) == part && !child->data(0, Qt::UserRole).isValid()) {
                    found = child;
                    break;
                }
            }
            if (!found) {
                found = current ? new QTreeWidgetItem(current) : new QTreeWidgetItem(m_timelineList);
                found->setText(0, part);
                found->setIcon(0, folderIcon);
                found->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                found->setCheckState(0, Qt::Unchecked);
                found->setToolTip(0, parts.mid(0, pi + 1).join('/'));
            }
            current = found;
        }
        return current;
    };

    for (int idx : sortedIndices) {
        const auto& timeline = m_session->timelines[idx];

        // Split name on '/' → folder path + leaf name
        QStringList parts = timeline.name.split('/');
        const QString leafName = parts.last();
        parts.removeLast();

        // Find or create folder hierarchy; nullptr means top-level
        QTreeWidgetItem* parent = nullptr;
        if (!parts.isEmpty()) {
            parent = findOrCreateFolderPath(parts);
        }

        // For aliases, display source frames
        const QStringList* displayFrames = &timeline.frames;
        if (!timeline.aliasOf.isEmpty()) {
            for (const auto& src : m_session->timelines) {
                if (src.name == timeline.aliasOf && src.aliasOf.isEmpty()) {
                    displayFrames = &src.frames;
                    break;
                }
            }
        }

        QIcon icon;
        if (!displayFrames->isEmpty()) {
            int middleIndex = displayFrames->size() / 2;
            const QString middlePath = (*displayFrames)[middleIndex];
            icon = m_timelineListIconCache.value(middlePath);
            if (icon.isNull()) {
                icon = QIcon(middlePath);
                m_timelineListIconCache.insert(middlePath, icon);
            }
        }

        const QString displayName = timeline.aliasOf.isEmpty()
            ? leafName
            : QStringLiteral("[alias] %1").arg(leafName);

        QTreeWidgetItem* item = parent
            ? new QTreeWidgetItem(parent)
            : new QTreeWidgetItem(m_timelineList);
        item->setIcon(0, icon);
        item->setText(0, QStringLiteral("%1 | %2 frames | %3 fps")
            .arg(displayName)
            .arg(displayFrames->size())
            .arg(timeline.fps));
        item->setData(0, Qt::UserRole, idx);
        item->setToolTip(0, timeline.name);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Unchecked);
    }

    m_timelineList->expandAll();
    m_timelineList->setCurrentItem(timelineItemForIndex(savedIndex));
    m_timelineList->blockSignals(false);
    m_timelineList->setVisible(!m_session->timelines.isEmpty());
    // setCurrentItem() above was called with signals blocked, so
    // onTimelineSelectionChanged() never fired. Call it explicitly so the
    // editor container visibility is always in sync with the selection.
    onTimelineSelectionChanged();
}

void MainWindow::refreshTimelineFrames() {
    if (m_timelineFrameIconCache.size() > 4096) {
        // Evict roughly half the cache to avoid sudden stutter from full clear
        auto it = m_timelineFrameIconCache.begin();
        int toRemove = m_timelineFrameIconCache.size() / 2;
        while (toRemove > 0 && it != m_timelineFrameIconCache.end()) {
            it = m_timelineFrameIconCache.erase(it);
            --toRemove;
        }
    }
    m_timelineFramesList->setUpdatesEnabled(false);
    m_timelineFramesList->clear();
    if (m_session->selectedTimelineIndex < 0 || m_session->selectedTimelineIndex >= m_session->timelines.size()) {
        m_timelineFramesList->setUpdatesEnabled(true);
        return;
    }

    const auto& timeline = m_session->timelines[m_session->selectedTimelineIndex];

    // Resolve effective frames for display (alias support)
    const QStringList* framesToShow = &timeline.frames;
    if (!timeline.aliasOf.isEmpty()) {
        for (const auto& src : m_session->timelines) {
            if (src.name == timeline.aliasOf && src.aliasOf.isEmpty()) {
                framesToShow = &src.frames;
                break;
            }
        }
    }
    m_timelineDragHintLabel->setVisible(framesToShow->isEmpty() && timeline.aliasOf.isEmpty());
    for (const QString& path : *framesToShow) {
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

    // Compute actual inserted index (dropFrame clamps negative/overflowing indices to end)
    const auto& frames = m_session->timelines[m_session->selectedTimelineIndex].frames;
    const int insertedIdx = (index < 0 || index >= (int)frames.size())
                                ? (int)frames.size() - 1
                                : index;

    // Refresh UI for the already-applied drop
    if (m_animCanvas) m_animCanvas->setZoomManual(false);
    refreshTimelineFrames();
    refreshTimelineList();
    fitAnimationToViewport();
    refreshAnimationTest();

    m_undoStack->push(new TimelineFrameDropCommand(
        &m_session->timelines,
        m_session->selectedTimelineIndex,
        path,
        insertedIdx,
        [this]() {
            refreshTimelineFrames();
            refreshTimelineList();
            refreshAnimationTest();
        }
    ));
}

void MainWindow::onFrameMoved(int from, int to) {
    if (m_session->selectedTimelineIndex < 0 ||
        m_session->selectedTimelineIndex >= m_session->timelines.size()) {
        return;
    }
    // Capture frames before the move for undo
    QStringList savedFramesBefore = m_session->timelines[m_session->selectedTimelineIndex].frames;

    if (!AnimationTimelineOps::moveFrame(m_session->timelines, m_session->selectedTimelineIndex, from, to)) {
        return;
    }
    refreshTimelineFrames();
    refreshTimelineList();
    refreshAnimationTest();

    m_undoStack->push(new TimelineFrameMoveCommand(
        &m_session->timelines,
        m_session->selectedTimelineIndex,
        from,
        to,
        savedFramesBefore,
        [this]() {
            refreshTimelineFrames();
            refreshTimelineList();
            refreshAnimationTest();
        }
    ));
}

void MainWindow::onFrameDuplicateRequested(int index) {
    if (m_session->selectedTimelineIndex < 0 ||
        m_session->selectedTimelineIndex >= m_session->timelines.size()) {
        return;
    }
    // Capture path for command (needed for subsequent redo after undo)
    const auto& frames = m_session->timelines[m_session->selectedTimelineIndex].frames;
    if (index < 0 || index >= frames.size()) return;
    const QString path = frames[index];

    if (!AnimationTimelineOps::duplicateFrame(m_session->timelines, m_session->selectedTimelineIndex, index)) {
        return;
    }
    refreshTimelineFrames();
    refreshTimelineList();
    refreshAnimationTest();

    m_undoStack->push(new TimelineFrameDuplicateCommand(
        &m_session->timelines,
        m_session->selectedTimelineIndex,
        index,
        path,
        [this]() {
            refreshTimelineFrames();
            refreshTimelineList();
            refreshAnimationTest();
        }
    ));
}

void MainWindow::onFrameRemoveRequested() {
    if (m_session->selectedTimelineIndex < 0 ||
        m_session->selectedTimelineIndex >= m_session->timelines.size()) {
        return;
    }
    QList<QListWidgetItem*> items = m_timelineFramesList->selectedItems();
    if (items.isEmpty()) return;

    QVector<int> rows;
    for (auto* item : items) {
        rows.append(m_timelineFramesList->row(item));
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());

    // Capture (index, path) pairs ascending for the undo command
    const auto& frames = m_session->timelines[m_session->selectedTimelineIndex].frames;
    QVector<QPair<int,QString>> removed;
    removed.reserve(rows.size());
    for (int i = rows.size() - 1; i >= 0; --i) {
        int row = rows[i];
        if (row >= 0 && row < frames.size()) {
            removed.append({row, frames[row]});
        }
    }

    AnimationTimelineOps::removeFrames(m_session->timelines, m_session->selectedTimelineIndex, rows);
    refreshTimelineFrames();
    refreshTimelineList();
    refreshAnimationTest();

    if (!removed.isEmpty()) {
        m_undoStack->push(new TimelineFrameRemoveCommand(
            &m_session->timelines,
            m_session->selectedTimelineIndex,
            removed,
            [this]() {
                refreshTimelineFrames();
                refreshTimelineList();
                refreshAnimationTest();
            }
        ));
    }
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
    if (m_session->layoutModels.isEmpty() || m_session->layoutModels.first().sprites.isEmpty()) {
        MessageDialog::information(this, tr("Generate Timelines"), tr("Load or generate a layout before creating timelines."));
        return;
    }

    const QVector<AnimationTimeline> oldState = m_session->timelines;
    const int oldSelection = m_session->selectedTimelineIndex;

    // Build sprites with names relative to their cachedFolderPath so that
    // timeline names are clean and never include temp-dir path components.
    QVector<SpritePtr> renamedSprites;
    const bool multiSource = m_session->sources.size() > 1;
    for (const auto& model : m_session->layoutModels) {
        for (const auto& sprite : model.sprites) {
            if (!sprite) continue;
            const QString cp = QDir::cleanPath(sprite->path);

            int bestMatchLen = -1;
            QString localName;
            QString srcDisplayName;
            for (const auto& src : m_session->sources) {
                if (src.cachedFolderPath.isEmpty()) continue;
                const QString cleanedCache = QDir::cleanPath(src.cachedFolderPath);
                if (cp.startsWith(cleanedCache + QLatin1Char('/'))
                        && cleanedCache.length() > bestMatchLen) {
                    bestMatchLen = cleanedCache.length();
                    const QString rel  = cp.mid(cleanedCache.length() + 1);
                    const QString dir  = QFileInfo(rel).path();
                    const QString base = QFileInfo(rel).baseName();
                    localName      = (dir.isEmpty() || dir == QLatin1String("."))
                                     ? base : dir + QLatin1Char('/') + base;
                    srcDisplayName = src.name;
                }
            }

            if (localName.isEmpty())
                localName = QFileInfo(sprite->path).baseName(); // fallback

            // When multiple sources are loaded, prefix with source name to avoid
            // cross-source animation name collisions.
            const QString finalName = (multiSource && !srcDisplayName.isEmpty())
                                      ? srcDisplayName + QLatin1Char('/') + localName
                                      : localName;

            auto namedSprite = std::make_shared<Sprite>(*sprite);
            namedSprite->name = finalName;
            renamedSprites.append(namedSprite);
        }
    }

    int focusIndex = -1;
    QString status;
    bool changed = TimelineGenerationService::generateFromSprites(
        renamedSprites,
        m_session->timelines,
        focusIndex,
        [this](const QString& timelineName) {
            return TimelineUi::askTimelineConflictResolution(this, timelineName);
        },
        status);

    if (changed) {
        if (focusIndex >= 0) {
            m_session->selectedTimelineIndex = focusIndex;
        }

        auto postExecute = [this]() {
            refreshTimelineList();
            if (m_session->selectedTimelineIndex >= 0)
                m_timelineList->setCurrentItem(timelineItemForIndex(m_session->selectedTimelineIndex));
            else
                onTimelineSelectionChanged();

            if (m_animCanvas) m_animCanvas->setZoomManual(false);
            fitAnimationToViewport();
            refreshAnimationTest();
        };

        m_undoStack->push(new TimelinesUpdateCommand(
            &m_session->timelines,
            oldState,
            m_session->timelines,
            oldSelection,
            m_session->selectedTimelineIndex,
            &m_session->selectedTimelineIndex,
            postExecute,
            tr("Auto-create Timelines")
        ));

        postExecute();
        m_statusLabel->setText(status);
    } else {
        MessageDialog::information(this, tr("Generate Timelines"), status);
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
    const bool wasPlaying = m_animPlaying;
    AnimationPlaybackService::togglePlayPause(
        m_session->timelines,
        m_session->selectedTimelineIndex,
        selectedTimelineFps(m_session->timelines, m_session->selectedTimelineIndex),
        m_animPlaying,
        m_animTimer,
        m_animPlayPauseBtn);
    if (m_animPlaying && !wasPlaying)
        m_animElapsed.start();
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
    const int fps = selectedTimelineFps(m_session->timelines, m_session->selectedTimelineIndex);
#ifdef Q_OS_WASM
    // Timer fires on every RAF callback (~16 ms). Skip until a full frame-duration
    // has elapsed so the animation plays at the correct fps rather than at 60 fps.
    if (m_animElapsed.elapsed() < static_cast<qint64>(1000.0 / fps))
        return;
#endif
    const qint64 elapsed = m_animElapsed.restart();
    if (!AnimationPlaybackService::tick(m_session->timelines, m_session->selectedTimelineIndex,
                                        m_animFrameIndex, elapsed, fps)) {
        return;
    }
    refreshAnimationTest();
}

bool MainWindow::exportAnimation(const QString& outPath) {
    return AnimationExportService::exportAnimation(
        this,
        m_session->timelines,
        m_session->selectedTimelineIndex,
        m_session->layoutModels,
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
    // Ensure all frames are in QPixmapCache before the first tick.
    // preloadTimeline() is a no-op when the timeline has not changed.
    if (m_session->selectedTimelineIndex >= 0
            && m_session->selectedTimelineIndex < m_session->timelines.size()) {
        const auto& selTl = m_session->timelines[m_session->selectedTimelineIndex];
        const QStringList* preloadFrames = &selTl.frames;
        if (!selTl.aliasOf.isEmpty()) {
            for (const auto& src : m_session->timelines) {
                if (src.name == selTl.aliasOf && src.aliasOf.isEmpty()) {
                    preloadFrames = &src.frames;
                    break;
                }
            }
        }
        AnimationPreviewService::preloadTimeline(*preloadFrames);
    }

    QString statusText;
    bool hasFrames = false;
    bool playing = m_animPlaying;
    QPixmap pixmap = AnimationPreviewService::refresh(
        m_session->timelines,
        m_session->selectedTimelineIndex,
        m_animFrameIndex,
        m_session->layoutModels,
        statusText,
        hasFrames,
        playing,
        m_animTimer);

    if (playing != m_animPlaying) {
        m_animPlaying = playing;
    }
    // Update button icon to reflect current playing state
    if (m_animPlayPauseBtn) {
        static const QIcon kPlayIcon = QApplication::style()->standardIcon(QStyle::SP_MediaPlay);
        static const QIcon kPauseIcon = QApplication::style()->standardIcon(QStyle::SP_MediaPause);
        m_animPlayPauseBtn->setIcon(m_animPlaying ? kPauseIcon : kPlayIcon);
    }

    m_animStatusLabel->setText(statusText);
    m_animStatusLabel->setAlignment(hasFrames ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignCenter);
    m_animStatusLabel->setStyleSheet(hasFrames
        ? "color: #808080;"
        : "color: #808080; font-style: italic; padding: 8px 0;");
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
