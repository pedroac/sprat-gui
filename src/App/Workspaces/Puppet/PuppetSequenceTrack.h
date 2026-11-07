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
 * a step and moves the playhead there, dragging moves the playhead, double-clicking opens a
 * step's pose, Delete or Backspace removes the selected step, and the context menu adds poses
 * (at the end, or before or after a step), moves, duplicates and deletes steps.
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
    /// The context menu for step @p step, or for empty space with -1. The caller owns it.
    QMenu* createContextMenu(int step);

    QSize sizeHint() const override;

signals:
    void stepSelected(int stepIndex);
    void stepActivated(int stepIndex);
    void playheadMoved(int frame);
    /// Add a step showing @p pose at index @p stepIndex.
    void addRequested(int stepIndex, const QString& pose);
    /// Move step @p from to index @p to (its index afterwards).
    void moveRequested(int from, int to);
    void duplicateRequested(int stepIndex);
    void removeRequested(int stepIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    int blockLeft(int step) const;
    int blockWidth(int step) const;
    int frameAtX(int x) const;
    int totalFrames() const;
    /// A menu of the poses, each adding a step showing it at @p stepIndex.
    QMenu* posesMenu(const QString& title, int stepIndex, QWidget* parent);

    QVector<Block> m_blocks;
    QStringList m_poseNames;
    int m_selected = -1;
    int m_playhead = -1;
    bool m_scrubbing = false;
};
