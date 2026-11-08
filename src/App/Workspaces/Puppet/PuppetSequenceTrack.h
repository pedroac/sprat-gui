#pragma once
#include <QPixmap>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QMenu;

/**
 * @brief The selected puppet animation's steps, left to right: each a block as wide as the
 *        frames it lasts, a ramp over its `in` frames (reaching its pose) then flat for its
 *        `hold` frames, with the pose's thumbnail and name; a frame ruler and a playhead.
 *
 * It only shows them and asks for changes, like an animation's frame list: clicking selects
 * a step (the workspace puts the playhead where its pose arrives), dragging moves the
 * playhead, dragging the round handle under a step moves the step (a line shows where it
 * will land, Escape cancels), double-clicking opens a step's pose, Delete or Backspace
 * removes the selected step, and the context menu adds new frames (this animation's own poses) or poses (at the
 * end, or before or after a step), saves a step's pose to the Poses tab, moves, duplicates
 * and deletes steps. Steps playing a shared pose are drawn with a dashed border.
 * PuppetWorkspace applies the changes and calls showSteps().
 */
class PuppetSequenceTrack : public QWidget {
    Q_OBJECT
public:
    /// Screen pixels a frame takes.
    static constexpr int FRAME_WIDTH = 14;

    struct Block {
        QString pose;
        int in = 0;
        int hold = 0;
        bool snap = false;
        QPixmap thumbnail;
        bool missing = false;  ///< The puppet has no pose of that name
        bool shared = false;   ///< A pose of the Poses tab, not one of this animation's own
    };

    explicit PuppetSequenceTrack(QWidget* parent = nullptr);

    /// Shows @p blocks with step @p selected selected (-1 for none), offering @p poseNames to add.
    void showSteps(const QVector<Block>& blocks, int selected, const QStringList& poseNames);
    /// Puts the playhead on frame @p frame of the steps (-1 hides it).
    void setPlayhead(int frame);
    int selectedStep() const { return m_selected; }
    int playhead() const { return m_playhead; }
    /// The step at @p x, or -1.
    int stepAtX(int x) const;
    /// The step whose drag handle is at @p pos, or -1.
    int handleAt(const QPoint& pos) const;
    /// Where the handle of step @p step is drawn (its middle).
    QPoint handleCenter(int step) const;
    /// The context menu for step @p step, or for empty space with -1. The caller owns it.
    QMenu* createContextMenu(int step);

    QSize sizeHint() const override;

signals:
    void stepSelected(int stepIndex);
    void stepActivated(int stepIndex);
    void playheadMoved(int frame);
    /// Add a step showing @p pose at index @p stepIndex.
    void addRequested(int stepIndex, const QString& pose);
    /// Add a step at index @p stepIndex playing a new pose of the animation, a copy of step
    /// @p copyOf's pose (-1 for an empty pose).
    void newFrameRequested(int stepIndex, int copyOf);
    /// Copy step @p stepIndex's pose to the Poses tab.
    void savePoseRequested(int stepIndex);
    /// Move step @p from to index @p to (its index afterwards).
    void moveRequested(int from, int to);
    void duplicateRequested(int stepIndex);
    void removeRequested(int stepIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    int blockLeft(int step) const;
    int blockWidth(int step) const;
    int frameAtX(int x) const;
    int totalFrames() const;
    /// The index the dragged step would have if dropped with the pointer at @p x.
    int dropIndexAt(int x) const;
    /// Where the line showing a drop at index @p to is drawn.
    int dropLineX(int to) const;
    void cancelStepDrag();
    /// A menu of the poses, each adding a step showing it at @p stepIndex.
    QMenu* posesMenu(const QString& title, int stepIndex, QWidget* parent);

    QVector<Block> m_blocks;
    QStringList m_poseNames;
    int m_selected = -1;
    int m_playhead = -1;
    bool m_scrubbing = false;
    int m_dragStep = -1;      ///< The step whose handle is held, -1 for none
    int m_dragX = 0;          ///< The pointer's x while a step is dragged
    int m_dragGrabX = 0;      ///< The pointer's x from the dragged block's left when grabbed
    bool m_dragMoved = false; ///< Moved far enough to be a drag rather than a click
    int m_hoveredHandle = -1;
};
