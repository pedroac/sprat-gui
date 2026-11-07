#pragma once
#include <QListWidget>
#include <QPixmap>
#include <QStringList>
#include <QVector>

/**
 * @brief The Puppet workspace's film strip: a thumbnail of the rest pose ("Rest", always
 *        first) and of each of the puppet's poses, in order.
 *
 * It only shows them and asks for changes: clicking a frame emits frameSelected (-1 for
 * Rest), double-clicking a pose renames it in place (renameRequested), dragging a pose
 * reorders it (moveRequested) and Delete asks to remove it. PuppetWorkspace applies them
 * and calls showFrames() again.
 */
class PuppetPoseStrip : public QListWidget {
    Q_OBJECT
public:
    /// Thumbnails are square, this many pixels a side.
    static constexpr int THUMBNAIL_SIZE = 72;

    explicit PuppetPoseStrip(QWidget* parent = nullptr);

    /// Shows the rest pose and the poses (names and thumbnails, in order), with frame
    /// @p selected selected: -1 for Rest.
    void showFrames(const QPixmap& rest, const QStringList& poseNames, const QVector<QPixmap>& thumbnails,
                    int selected);
    /// The selected frame: -1 for Rest (or nothing), else a pose index.
    int selectedFrame() const;

signals:
    void frameSelected(int poseIndex);
    void renameRequested(int poseIndex, const QString& name);
    void moveRequested(int from, int to);
    void removeRequested(int poseIndex);

protected:
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    QSize sizeHint() const override;

private:
    /// The pose index of @p item: -1 for Rest.
    static int poseOf(const QListWidgetItem* item);

    bool m_rebuilding = false;
    QStringList m_names;  ///< As shown, to put a refused rename back
};
