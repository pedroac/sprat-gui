#pragma once
#include <QStringList>
#include <QTreeWidget>

class TimelineTreeWidget : public QTreeWidget {
    Q_OBJECT
public:
    explicit TimelineTreeWidget(QWidget* parent = nullptr);

signals:
    void deleteKeyPressed();
    void dropCompleted(int draggedTimelineIndex,
                       const QString& draggedFolderPath,
                       const QString& targetFolderPath);
    void spritesDroppedToCreate(const QStringList& paths, const QString& targetFolderPath);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    QString folderNodePath(QTreeWidgetItem* item) const;
    bool isAncestorOf(QTreeWidgetItem* ancestor, QTreeWidgetItem* descendant) const;

    QTreeWidgetItem* m_draggedItem = nullptr;
    bool m_draggedIsLeaf = false;
    int m_draggedIndex = -1;
    QString m_draggedFolderPath;
};
