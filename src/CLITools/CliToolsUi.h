#pragma once

#include <QStringList>

class QWidget;

enum class MissingCliAction {
    Install,
    ProvidePath,
    Quit
};

class CliToolsUi {
public:
    static MissingCliAction askMissingCliAction(QWidget* parent, const QStringList& missing);
};
