#include "FrameAnimationWorkspace.h"
#include "NavigatorPanel.h"
#include "NavigatorTreeWidget.h"
#include "AnimationPreviewPanel.h"
#include "AnimationCanvas.h"
#include "TimelineEditorPanel.h"
#include "TimelineListWidget.h"
#include "ProjectSession.h"
#include "MessageDialog.h"
#include "SpriteSelectionPresenter.h"

#include "AnimationExportService.h"
#include "AnimationPlaybackService.h"
#include "AnimationPreviewService.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QListWidgetItem>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidgetItemIterator>
#include <QWidget>
#include <QtConcurrent>

namespace {
int selectedTimelineFps(const QVector<AnimationTimeline>& timelines, int selectedTimelineIndex) {
    if (selectedTimelineIndex < 0 || selectedTimelineIndex >= timelines.size()) {
        return 8;
    }
    return qMax(1, timelines[selectedTimelineIndex].fps);
}
} // namespace

FrameAnimationWorkspace::FrameAnimationWorkspace(NavigatorPanel*        sharedNavigator,
                                                  AnimationPreviewPanel* animPanel,
                                                  TimelineEditorPanel*   timelinePanel,
                                                  ProjectSession*        session,
                                                  AppSettings*           settings,
                                                  QWidget*               parent)
    : QWidget(parent)
    , m_sharedNavigator(sharedNavigator)
    , m_animPanel(animPanel)
    , m_timelineEditorPanel(timelinePanel)
    , m_session(session)
    , m_settings(settings)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Left: own navigator panel
    m_ownNavigator = new NavigatorPanel(this);
    m_ownNavigator->setAtlasComboVisible(true);
    m_ownNavigator->setShowHiddenVisible(false);
    m_ownNavigator->setCheckboxesEnabled(true);
    layout->addWidget(m_ownNavigator, 0);

    // Forward atlas selection changes
    connect(m_ownNavigator, &NavigatorPanel::atlasIndexChanged,
            this, &FrameAnimationWorkspace::atlasChanged);

    // ── Animation timer ──────────────────────────────────────────────────────
    m_animTimer = new QTimer(this);
    connect(m_animTimer, &QTimer::timeout,
            this, &FrameAnimationWorkspace::onAnimTimerTimeout);

    // ── Animation panel buttons / controls ───────────────────────────────────
    if (m_animPanel) {
        connect(m_animPanel->prevButton(),      &QPushButton::clicked,
                this, &FrameAnimationWorkspace::onAnimPrevClicked);
        connect(m_animPanel->playPauseButton(), &QPushButton::clicked,
                this, &FrameAnimationWorkspace::onAnimPlayPauseClicked);
        connect(m_animPanel->nextButton(),      &QPushButton::clicked,
                this, &FrameAnimationWorkspace::onAnimNextClicked);
        connect(m_animPanel->zoomSpin(), QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &FrameAnimationWorkspace::onAnimZoomChanged);
        connect(m_animPanel->overlayButton(), &QToolButton::toggled, this, [this](bool checked) {
            if (m_animPanel && m_animPanel->animCanvas()) {
                m_animPanel->animCanvas()->setOverlayVisible(checked);
                if (checked)
                    m_animPanel->animCanvas()->setOverlayEditable(!m_animPlaying);
            }
        });
        connect(m_animPanel->onionSkinButton(), &QToolButton::toggled, this, [this](bool) {
            refreshAnimationTest();
        });
    }

    // ── TimelineEditorPanel signals ──────────────────────────────────────────
    if (m_timelineEditorPanel) {
        connect(m_timelineEditorPanel, &TimelineEditorPanel::animPlaybackIntervalChanged, this,
            [this](int fps) {
                if (m_animPlaying) {
#ifndef Q_OS_WASM
                    m_animTimer->setInterval(1000 / qMax(1, fps));
#endif
                    m_animElapsed.restart();
                }
            });
        connect(m_timelineEditorPanel, &TimelineEditorPanel::animFrameReset, this,
            [this]() {
                m_animFrameIndex = 0;
                fitAnimationToViewport();
                refreshAnimationTest();
                emit frameChanged();
            });
        connect(m_timelineEditorPanel, &TimelineEditorPanel::animFrameIndexSelected, this,
            [this](int index) {
                if (!m_animPlaying) {
                    m_animFrameIndex = index;
                    refreshAnimationTest();
                    emit frameChanged();
                }
            });
        connect(m_timelineEditorPanel, &TimelineEditorPanel::animZoomResetAndFitRequested, this,
            [this]() {
                if (m_animPanel && m_animPanel->animCanvas())
                    m_animPanel->animCanvas()->setZoomManual(false);
                fitAnimationToViewport();
                refreshAnimationTest();
            });
        connect(m_timelineEditorPanel, &TimelineEditorPanel::animationDataChanged,
                this, &FrameAnimationWorkspace::refreshAnimationTest);
    }

    // Wire handle combo internally
    if (m_animPanel) {
        connect(m_animPanel->handleCombo(), QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &FrameAnimationWorkspace::onHandleComboChanged);
    }
    // Wire frame changes to sprite sync
    connect(this, &FrameAnimationWorkspace::frameChanged,
            this, &FrameAnimationWorkspace::syncSelectedSpriteToAnimFrame);
}

// ---------------------------------------------------------------------------
// IWorkspace
// ---------------------------------------------------------------------------

void FrameAnimationWorkspace::enter()
{
    // Configure the shared atlas-dock navigator for animation mode.
    if (m_sharedNavigator) {
        m_sharedNavigator->setAtlasComboVisible(true);
        m_sharedNavigator->setShowHiddenVisible(false);
        m_sharedNavigator->setCheckboxesEnabled(false);
        m_sharedNavigator->setAddSourceButtonVisible(false);
    }

    // Restore animation canvas zoom/center saved from the previous leave().
    if (m_animPanel && m_animPanel->animCanvas() && m_savedAnimZoom > 0.0) {
        m_animPanel->animCanvas()->setPendingRestore(m_savedAnimZoom, m_savedAnimCenter);
    }
}

void FrameAnimationWorkspace::leave()
{
    // Save animation canvas zoom/center so enter() can restore them next time.
    if (m_animPanel && m_animPanel->animCanvas()) {
        auto* canvas = m_animPanel->animCanvas();
        const double z = canvas->zoom();
        if (z > 0.0) {
            m_savedAnimZoom   = z;
            m_savedAnimCenter = canvas->mapToScene(canvas->viewport()->rect().center());
        }
    }
}

// ---------------------------------------------------------------------------
// Navigator helpers
// ---------------------------------------------------------------------------

void FrameAnimationWorkspace::updateAtlasCombo(const QVector<AtlasEntry>& atlases,
                                                int activeSessionIndex)
{
    if (m_ownNavigator)
        m_ownNavigator->updateAtlasCombo(atlases, activeSessionIndex);
}

void FrameAnimationWorkspace::refreshNavigator(const ProjectSession* session)
{
    if (!m_ownNavigator || !session) return;
    m_ownNavigator->refresh(session, /*showHidden=*/false, /*atlasFilter=*/-1);
}

// ---------------------------------------------------------------------------
// Animation playback
// ---------------------------------------------------------------------------

void FrameAnimationWorkspace::onAnimPrevClicked()
{
    if (!m_session || !m_animPanel) return;
    if (!AnimationPlaybackService::prev(
            m_session->activeAtlas().timelines,
            m_session->selectedTimelineIndex,
            m_animFrameIndex,
            m_animPlaying,
            m_animTimer)) {
        return;
    }
    if (m_animPanel->playPauseButton()) {
        static const QIcon kPlay = QApplication::style()->standardIcon(QStyle::SP_MediaPlay);
        m_animPanel->playPauseButton()->setIcon(kPlay);
    }
    refreshAnimationTest();
    emit frameChanged();
}

void FrameAnimationWorkspace::onAnimPlayPauseClicked()
{
    if (!m_session || !m_animPanel) return;
    const bool wasPlaying = m_animPlaying;
    AnimationPlaybackService::togglePlayPause(
        m_session->activeAtlas().timelines,
        m_session->selectedTimelineIndex,
        selectedTimelineFps(m_session->activeAtlas().timelines, m_session->selectedTimelineIndex),
        m_animPlaying,
        m_animTimer);
    if (m_animPanel->playPauseButton()) {
        static const QIcon kPlay  = QApplication::style()->standardIcon(QStyle::SP_MediaPlay);
        static const QIcon kPause = QApplication::style()->standardIcon(QStyle::SP_MediaPause);
        m_animPanel->playPauseButton()->setIcon(m_animPlaying ? kPause : kPlay);
    }
    if (m_animPlaying && !wasPlaying) {
        m_animElapsed.start();
    } else if (wasPlaying && !m_animPlaying) {
        emit frameChanged();
    }
    auto* animCanvas = m_animPanel->animCanvas();
    if (animCanvas)
        animCanvas->setOverlayEditable(!m_animPlaying);
}

void FrameAnimationWorkspace::onAnimNextClicked()
{
    if (!m_session || !m_animPanel) return;
    if (!AnimationPlaybackService::next(
            m_session->activeAtlas().timelines,
            m_session->selectedTimelineIndex,
            m_animFrameIndex,
            m_animPlaying,
            m_animTimer)) {
        return;
    }
    if (m_animPanel->playPauseButton()) {
        static const QIcon kPlay = QApplication::style()->standardIcon(QStyle::SP_MediaPlay);
        m_animPanel->playPauseButton()->setIcon(kPlay);
    }
    refreshAnimationTest();
    emit frameChanged();
}

void FrameAnimationWorkspace::onAnimTimerTimeout()
{
    if (!m_session) return;
    const int fps = selectedTimelineFps(m_session->activeAtlas().timelines, m_session->selectedTimelineIndex);
#ifdef Q_OS_WASM
    if (m_animElapsed.elapsed() < static_cast<qint64>(1000.0 / fps))
        return;
#endif
    const qint64 elapsed = m_animElapsed.restart();
    if (!AnimationPlaybackService::tick(m_session->activeAtlas().timelines, m_session->selectedTimelineIndex,
                                        m_animFrameIndex, elapsed, fps)) {
        return;
    }
    refreshAnimationTest();
}

void FrameAnimationWorkspace::refreshAnimationTest()
{
    if (!m_session || !m_animPanel) return;

    if (m_session->selectedTimelineIndex >= 0
            && m_session->selectedTimelineIndex < m_session->activeAtlas().timelines.size()) {
        const auto& selTl = m_session->activeAtlas().timelines[m_session->selectedTimelineIndex];
        const QStringList* preloadFrames = &selTl.frames;
        if (!selTl.aliasOf.isEmpty()) {
            for (const auto& src : m_session->activeAtlas().timelines) {
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
    const bool onionSkin = m_animPanel->onionSkinButton() && m_animPanel->onionSkinButton()->isChecked();
    const int onionOpacity = m_settings ? m_settings->onionSkinOpacity : 0;
    QPixmap pixmap = AnimationPreviewService::refresh(
        m_session->activeAtlas().timelines,
        m_session->selectedTimelineIndex,
        m_animFrameIndex,
        m_session->activeAtlas().layoutModels,
        statusText,
        hasFrames,
        playing,
        m_animTimer,
        onionSkin,
        onionOpacity);

    if (playing != m_animPlaying) {
        m_animPlaying = playing;
    }

    // Update play/pause button icon
    auto* playPauseBtn = m_animPanel->playPauseButton();
    if (playPauseBtn) {
        static const QIcon kPlayIcon = QApplication::style()->standardIcon(QStyle::SP_MediaPlay);
        static const QIcon kPauseIcon = QApplication::style()->standardIcon(QStyle::SP_MediaPause);
        playPauseBtn->setIcon(m_animPlaying ? kPauseIcon : kPlayIcon);
    }

    auto* animStatusLabel = m_animPanel->statusLabel();
    if (animStatusLabel) {
        animStatusLabel->setText(statusText);
        animStatusLabel->setAlignment(hasFrames ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignCenter);
        animStatusLabel->setStyleSheet(hasFrames
            ? "color: #808080;"
            : "color: #808080; font-style: italic; padding: 8px 0;");
    }
    auto* prevBtn = m_animPanel->prevButton();
    auto* nextBtn = m_animPanel->nextButton();
    if (prevBtn) prevBtn->setEnabled(hasFrames);
    if (playPauseBtn) playPauseBtn->setEnabled(hasFrames);
    if (nextBtn) nextBtn->setEnabled(hasFrames);

    auto* animCanvas = m_animPanel->animCanvas();
    if (animCanvas) {
        animCanvas->setPixmap(pixmap);
    }
}

void FrameAnimationWorkspace::fitAnimationToViewport()
{
    auto* animCanvas = m_animPanel ? m_animPanel->animCanvas() : nullptr;
    auto* zoomSpin   = m_animPanel ? m_animPanel->zoomSpin()   : nullptr;
    if (!animCanvas || !zoomSpin || animCanvas->isZoomManual()) {
        return;
    }
    const bool blocked = zoomSpin->blockSignals(true);
    animCanvas->initialFit();
    zoomSpin->blockSignals(blocked);
}

void FrameAnimationWorkspace::onAnimZoomChanged(double value)
{
    auto* animCanvas = m_animPanel ? m_animPanel->animCanvas() : nullptr;
    if (animCanvas) {
        if (m_animPanel && !m_animPanel->zoomSpin()->signalsBlocked()) {
            animCanvas->setZoomManual(true);
        }
        animCanvas->setZoom(value / 100.0);
    }
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

bool FrameAnimationWorkspace::exportAnimation(const QString& outPath)
{
    if (!m_session) return false;

    auto timelines    = m_session->activeAtlas().timelines;
    int  tlIndex      = m_session->selectedTimelineIndex;
    auto layoutModels = m_session->activeAtlas().layoutModels;
    int  fps          = selectedTimelineFps(timelines, tlIndex);

    AnimationExportService::ExportCallbacks cbs;
    cbs.setLoading = [this](bool v) {
        QMetaObject::invokeMethod(this, [this, v]() { emit loadingStateChanged(v); }, Qt::QueuedConnection);
    };
    cbs.setStatus = [this](const QString& s) {
        QMetaObject::invokeMethod(this, [this, s]() { emit statusMessage(s); }, Qt::QueuedConnection);
    };
    cbs.showError = [this](const QString& t, const QString& msg) {
        QMetaObject::invokeMethod(this, [this, t, msg]() {
            MessageDialog::critical(parentWidget(), t, msg);
        }, Qt::QueuedConnection);
    };

    m_animExportOutPath = outPath;
    emit loadingStateChanged(true);

    disconnect(&m_animExportWatcher, &QFutureWatcher<bool>::finished,
               this, &FrameAnimationWorkspace::onAnimExportFinished);
    connect(&m_animExportWatcher, &QFutureWatcher<bool>::finished,
            this, &FrameAnimationWorkspace::onAnimExportFinished);

    m_animExportWatcher.setFuture(
        QtConcurrent::run([timelines, tlIndex, layoutModels, fps, outPath, cbs]() {
            return AnimationExportService::exportAnimation(
                timelines, tlIndex, layoutModels, fps, outPath, cbs);
        }));

    return true;
}

void FrameAnimationWorkspace::onAnimExportFinished()
{
    const bool ok = m_animExportWatcher.result();
    if (ok)
        emit statusMessage(tr("Animation saved to %1").arg(m_animExportOutPath));
    // loadingStateChanged(false) is invoked by the export service via the queued callback.
}

void FrameAnimationWorkspace::saveAnimationToFile()
{
    if (m_animExportWatcher.isRunning()) return;
    QString path = AnimationExportService::chooseOutputPath(this);
    if (!path.isEmpty())
        exportAnimation(path);
}

// ---------------------------------------------------------------------------
// Timeline helpers
// ---------------------------------------------------------------------------

void FrameAnimationWorkspace::refreshTimelineList()
{
    if (m_timelineEditorPanel) m_timelineEditorPanel->refreshTimelineList();
}

void FrameAnimationWorkspace::refreshTimelineFrames()
{
    if (m_timelineEditorPanel) m_timelineEditorPanel->refreshTimelineFrames();
}

// ---------------------------------------------------------------------------
// Moved from MainWindow: sprite↔frame sync and handle combo sync
// ---------------------------------------------------------------------------

void FrameAnimationWorkspace::syncSelectedSpriteToAnimFrame()
{
    auto* animCanvas = m_animPanel ? m_animPanel->animCanvas() : nullptr;
    if (m_session->selectedTimelineIndex < 0 ||
        m_session->selectedTimelineIndex >= m_session->activeAtlas().timelines.size()) {
        if (animCanvas) animCanvas->setOverlaySprite(nullptr);
        return;
    }

    const auto& tl = m_session->activeAtlas().timelines[m_session->selectedTimelineIndex];
    const QStringList* frames = &tl.frames;
    if (!tl.aliasOf.isEmpty()) {
        for (const auto& src : m_session->activeAtlas().timelines) {
            if (src.name == tl.aliasOf && src.aliasOf.isEmpty()) {
                frames = &src.frames;
                break;
            }
        }
    }

    const int frameIdx = m_animFrameIndex;
    if (frameIdx < 0 || frameIdx >= frames->size()) {
        if (animCanvas) animCanvas->setOverlaySprite(nullptr);
        return;
    }
    const QString& path = (*frames)[frameIdx];

    auto* spriteTree = m_sharedNavigator ? m_sharedNavigator->tree() : nullptr;
    for (const auto& model : m_session->activeAtlas().layoutModels) {
        for (const auto& sprite : model.sprites) {
            if (!sprite || sprite->path != path) continue;
            emit spriteSelectionRequested(sprite);
            if (animCanvas) {
                animCanvas->setOverlaySprite(sprite);
                animCanvas->overlay()->setSelectedMarker(m_session->selectedPointName);
                refreshHandleCombo();
            }
            if (spriteTree) {
                QTreeWidgetItemIterator it(spriteTree);
                while (*it) {
                    if ((*it)->childCount() == 0) {
                        auto treeSprite = (*it)->data(0, Qt::UserRole).value<SpritePtr>();
                        if (treeSprite == sprite) {
                            spriteTree->blockSignals(true);
                            spriteTree->setCurrentItem(*it);
                            spriteTree->blockSignals(false);
                            spriteTree->scrollToItem(*it, QAbstractItemView::EnsureVisible);
                            break;
                        }
                    }
                    ++it;
                }
            }
            if (m_timelineEditorPanel) {
                auto* framesList = m_timelineEditorPanel->timelineFramesList();
                if (framesList && frameIdx < framesList->count()) {
                    framesList->blockSignals(true);
                    framesList->setCurrentRow(frameIdx);
                    framesList->blockSignals(false);
                    if (QListWidgetItem* item = framesList->item(frameIdx))
                        framesList->scrollToItem(item, QAbstractItemView::EnsureVisible);
                }
            }
            return;
        }
    }
    if (animCanvas) animCanvas->setOverlaySprite(nullptr);
}

void FrameAnimationWorkspace::onHandleComboChanged(int index)
{
    auto* animHandleCombo = m_animPanel ? m_animPanel->handleCombo() : nullptr;
    if (index <= 0) {
        m_session->selectedPointName.clear();
    } else if (animHandleCombo) {
        m_session->selectedPointName = animHandleCombo->itemText(index);
    }

    auto* animCanvas = m_animPanel ? m_animPanel->animCanvas() : nullptr;
    if (animCanvas && animCanvas->overlay())
        animCanvas->overlay()->setSelectedMarker(m_session->selectedPointName);

    emit markerSelectionChanged(m_session->selectedPointName);
}

void FrameAnimationWorkspace::onMarkerSelectedFromCanvas(const QString& name)
{
    auto* animHandleCombo = m_animPanel ? m_animPanel->handleCombo() : nullptr;
    if (!animHandleCombo) return;
    animHandleCombo->blockSignals(true);
    const int idx = name.isEmpty() ? 0 : animHandleCombo->findText(name);
    animHandleCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    animHandleCombo->blockSignals(false);
}

void FrameAnimationWorkspace::refreshHandleCombo()
{
    auto* animHandleCombo = m_animPanel ? m_animPanel->handleCombo() : nullptr;
    if (!animHandleCombo) return;
    auto* animCanvas = m_animPanel->animCanvas();
    SpriteSelectionPresenter::refreshHandleCombo(
        animHandleCombo,
        animCanvas ? animCanvas->overlaySprite() : nullptr,
        m_session->selectedPointName);
}

void FrameAnimationWorkspace::handleResize()
{
    auto* canvas = m_animPanel ? m_animPanel->animCanvas() : nullptr;
    if (canvas && !canvas->isZoomManual())
        QTimer::singleShot(0, this, &FrameAnimationWorkspace::fitAnimationToViewport);
    QTimer::singleShot(0, this, &FrameAnimationWorkspace::refreshAnimationTest);
}
