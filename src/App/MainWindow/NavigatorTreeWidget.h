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
};
