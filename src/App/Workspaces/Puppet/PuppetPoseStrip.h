#pragma once
#include <QListWidget>
#include <QPixmap>
#include <QStringList>
#include <QVector>

class QMenu;

/**
 * @brief The Puppet workspace's film strip: a thumbnail of each of the puppet's poses, in
 *        order, the Rest pose first.
 *
 * It only shows them and asks for changes, like an animation's frame list: clicking a frame
 * emits frameSelected, double-clicking a pose renames it in place, dragging a pose reorders
 * it, Delete or Backspace removes it, and the context menu adds poses before or after one,
 * moves, duplicates and deletes them. The Rest pose (the first frame) can only be duplicated
 * or have poses added after it. PuppetWorkspace applies the changes and calls showFrames() again.
 */
class PuppetPoseStrip : public QListWidget {
    Q_OBJECT
public:
    /// Thumbnails are square, this many pixels a side.
    static constexpr int THUMBNAIL_SIZE = 72;

    explicit PuppetPoseStrip(QWidget* parent = nullptr);


    /// Shows the poses (names and thumbnails, in order, the Rest pose first), with pose
    /// @p selected selected (-1 for none).
    void showFrames(const QStringList& poseNames, const QVector<QPixmap>& thumbnails, int selected);
    /// The selected pose index, or -1 for none.
    int selectedFrame() const;
    /// The context menu for pose @p frame, or -1 for empty space. The caller owns it.
    QMenu* createContextMenu(int frame);

signals:
    void frameSelected(int poseIndex);
    void renameRequested(int poseIndex, const QString& name);
    /// Move pose @p from to index @p to (its index afterwards).
    void moveRequested(int from, int to);
    void removeRequested(int poseIndex);
    /// Add an empty pose at index @p poseIndex.
    void addRequested(int poseIndex);
    void duplicateRequested(int poseIndex);

protected:
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    QSize sizeHint() const override;

private:
    /// The pose index of @p item, or -1 for none.
    static int poseOf(const QListWidgetItem* item);
    /// Whether pose @p poseIndex is the Rest pose, which stays first and can't go.
    bool isRest(int poseIndex) const;

    bool m_rebuilding = false;
    QStringList m_names;  ///< As shown, to put a refused rename back
};
