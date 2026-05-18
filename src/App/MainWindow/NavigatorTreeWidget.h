#pragma once
#include <QTreeWidget>

class NavigatorTreeWidget : public QTreeWidget {
    Q_OBJECT
public:
    explicit NavigatorTreeWidget(QWidget* parent = nullptr);

signals:
    void deleteRequested(const QStringList& paths);

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QTreeWidgetItem* m_checkboxAnchor = nullptr;

    // Sets the check state for all visible items between 'from' and 'to' (inclusive, visual order).
    void setCheckStateRange(QTreeWidgetItem* from, QTreeWidgetItem* to, Qt::CheckState state);
};
