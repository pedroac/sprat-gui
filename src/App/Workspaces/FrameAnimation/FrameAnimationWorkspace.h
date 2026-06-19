#pragma once
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QPointF>
#include <QWidget>
#include "AppSettings.h"
#include "ProjectModels.h"
#include "IWorkspace.h"

class NavigatorPanel;
class AnimationPreviewPanel;
class ProjectSession;
class TimelineEditorPanel;
class QTimer;

/**
 * @class FrameAnimationWorkspace
 * @brief Widget that owns a NavigatorPanel for the Frame Animation workspace
 *        and drives animation playback / export state.
 *
 * The NavigatorPanel is configured with the atlas combo visible and the
 * show-hidden checkbox hidden.
 */
class FrameAnimationWorkspace : public QWidget, public IWorkspace {
    Q_OBJECT
public:
    /**
     * @param sharedNavigator  The atlas-dock navigator panel owned by AtlasWorkspace.
     *                         enter() / leave() configure it for animation / atlas mode.
     * @param animPanel        Animation preview panel (buttons, canvas, zoom spin).
     * @param timelinePanel    Timeline editor panel (signals for frame selection, fps, etc.).
     * @param session          Project session (non-owning).
     * @param settings         App settings pointer (non-owning); used for onion-skin opacity.
     * @param parent           Parent widget.
     */
    explicit FrameAnimationWorkspace(NavigatorPanel*        sharedNavigator,
                                      AnimationPreviewPanel* animPanel,
                                      TimelineEditorPanel*   timelinePanel,
                                      ProjectSession*        session,
                                      AppSettings*           settings,
                                      QWidget*               parent = nullptr);

    NavigatorPanel*        navigatorPanel() const { return m_ownNavigator; }
    AnimationPreviewPanel* animPanel()      const { return m_animPanel; }
    TimelineEditorPanel*   timelinePanel()  const { return m_timelineEditorPanel; }

    /** Stores a zoom/center to be restored when this workspace is next entered. */
    void setRestoreZoom(double zoom, QPointF center = {}) { m_savedAnimZoom = zoom; m_savedAnimCenter = center; }

    bool firstLoad() const { return m_firstLoad; }
    void clearFirstLoad() { m_firstLoad = false; }

    // IWorkspace
    void enter()  override;
    void leave()  override;
    QWidget* widget() override { return nullptr; }  // dock-only workspace

    // ── Navigator helpers ────────────────────────────────────────────────────
    void updateAtlasCombo(const QVector<AtlasEntry>& atlases, int activeSessionIndex);
    void refreshNavigator(const ProjectSession* session);

    // ── Animation state accessors (for project save / load) ──────────────────
    int  animFrameIndex()   const { return m_animFrameIndex; }
    bool isAnimPlaying()    const { return m_animPlaying; }
    void setAnimFrameIndex(int index) { m_animFrameIndex = index; }

    // ── Animation playback ──────────────────────────────────────────────────
    void refreshAnimationTest();
    void fitAnimationToViewport();

    // ── Export ──────────────────────────────────────────────────────────────
    void saveAnimationToFile();
    bool exportAnimation(const QString& outPath);

    // ── Timeline helpers ─────────────────────────────────────────────────────
    void refreshTimelineList();
    void refreshTimelineFrames();

public slots:
    void onAnimPrevClicked();
    void onAnimPlayPauseClicked();
    void onAnimNextClicked();
    void onAnimZoomChanged(double value);

    /** Selects the sprite corresponding to the current animation frame in the navigator tree. */
    void syncSelectedSpriteToAnimFrame();

    /** Syncs the animation-panel handle combo to the newly selected marker. */
    void onMarkerSelectedFromCanvas(const QString& name);

signals:
    /** Emitted when the user selects a different atlas in the combo. */
    void atlasChanged(int sessionAtlasIndex);

    /** Emitted when the current animation frame index changes (play/prev/next/select). */
    void frameChanged();

    /** Relay of status-bar text produced during playback / export. */
    void statusMessage(const QString& text);

    /** Set the main-window loading state (true = export in progress). */
    void loadingStateChanged(bool loading);

    /** Emitted when the animation frame sync needs to select a sprite in the navigator. */
    void spriteSelectionRequested(SpritePtr sprite);

    /** Emitted when the handle combo selection changes; cross-workspace sync. */
    void markerSelectionChanged(const QString& name);

public:
    /** Refreshes the animation-panel handle combo to match the current overlay sprite. */
    void refreshHandleCombo();

    /** Handles window resize: fits animation viewport if not manually zoomed. */
    void handleResize();

private slots:
    void onAnimTimerTimeout();
    void onAnimExportFinished();
    void onHandleComboChanged(int index);

private:
    NavigatorPanel*        m_ownNavigator        = nullptr;  // FrameAnim's own navigator
    NavigatorPanel*        m_sharedNavigator      = nullptr;  // AtlasWorkspace's navigator to configure
    AnimationPreviewPanel* m_animPanel            = nullptr;
    TimelineEditorPanel*   m_timelineEditorPanel  = nullptr;  // non-owning
    ProjectSession*        m_session              = nullptr;  // non-owning
    AppSettings*           m_settings             = nullptr;  // non-owning

    // ── Playback state ──────────────────────────────────────────────────────
    QTimer*       m_animTimer      = nullptr;
    int           m_animFrameIndex = 0;
    bool          m_animPlaying    = false;
    QElapsedTimer m_animElapsed;

    // ── Export state ─────────────────────────────────────────────────────────
    QFutureWatcher<bool> m_animExportWatcher;
    QString              m_animExportOutPath;

    // Saved animation canvas zoom/scroll position (for enter/leave)
    double  m_savedAnimZoom   = -1.0;
    QPointF m_savedAnimCenter;
    bool    m_firstLoad       = true;
};
