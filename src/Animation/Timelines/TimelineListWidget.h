#pragma once
#include <QListWidget>

/**
 * @brief Custom QListWidget for managing animation frames.
 * 
 * Supports drag and drop reordering and external drops.
 */
class TimelineListWidget : public QListWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs the TimelineListWidget.
     * @param parent The parent widget.
     */
    explicit TimelineListWidget(QWidget* parent = nullptr);

signals:
    /**
     * @brief Emitted when a frame is dropped from outside.
     */
    void frameDropped(const QString& path, int index);
    /**
     * @brief Emitted when a frame is moved internally.
     */
    void frameMoved(int fromIndex, int toIndex);
    /**
     * @brief Emitted when deletion is requested (Delete key).
     */
    void removeSelectedRequested();
    /**
     * @brief Emitted when duplication is requested.
     * @param index Index of the frame to duplicate.
     */
    void duplicateFrameRequested(int index);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    Qt::DropActions supportedDropActions() const override;
    void startDrag(Qt::DropActions supportedActions) override;

private slots:
    void onCustomContextMenuRequested(const QPoint& pos);

private:
    QListWidgetItem* m_placeholderItem = nullptr;
    void updatePlaceholder(const QPoint& pos);
    void clearPlaceholder();
};