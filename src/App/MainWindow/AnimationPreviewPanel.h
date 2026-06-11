#pragma once
#include <QWidget>

class QDoubleSpinBox;
class QPushButton;
class QLabel;
class QToolButton;
class AnimationCanvas;

/**
 * @class AnimationPreviewPanel
 * @brief Widget that owns the animation preview area.
 *
 * Creates and lays out the animation playback controls (prev/play/next,
 * overlay toggle, zoom spin) and the AnimationCanvas.  The internal
 * zoom-to-spin sync is wired inside the panel; all other signal connections
 * (overlay pivot/marker events, button clicks) are made by MainWindow via the
 * accessor methods.
 */
class AnimationPreviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit AnimationPreviewPanel(QWidget* parent = nullptr);

    // ── Widget accessors ─────────────────────────────────────────────────────
    QPushButton*    prevButton()       const { return m_prevBtn; }
    QPushButton*    playPauseButton()  const { return m_playPauseBtn; }
    QPushButton*    nextButton()       const { return m_nextBtn; }
    QToolButton*    overlayButton()    const { return m_overlayBtn; }
    QDoubleSpinBox* zoomSpin()         const { return m_zoomSpin; }
    QLabel*         statusLabel()      const { return m_statusLabel; }
    AnimationCanvas* animCanvas()      const { return m_animCanvas; }

private:
    QPushButton*    m_prevBtn      = nullptr;
    QPushButton*    m_playPauseBtn = nullptr;
    QPushButton*    m_nextBtn      = nullptr;
    QToolButton*    m_overlayBtn   = nullptr;
    QDoubleSpinBox* m_zoomSpin     = nullptr;
    QLabel*         m_statusLabel  = nullptr;
    AnimationCanvas* m_animCanvas  = nullptr;
};
