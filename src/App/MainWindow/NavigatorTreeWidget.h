#pragma once
#include <QTreeWidget>

class NavigatorTreeWidget : public QTreeWidget {
    Q_OBJECT
public:
    explicit NavigatorTreeWidget(QWidget* parent = nullptr);

protected:
    void startDrag(Qt::DropActions supportedActions) override;
};
