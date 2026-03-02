#pragma once

#include <QStringList>

class QWidget;

enum class MissingCliAction {
    Install,
    Upgrade,
    ProvidePath,
    Quit
};

class CliToolsUi {
public:
    static MissingCliAction askMissingCliAction(QWidget* parent, const QStringList& missing);
    static bool askUpgrade(QWidget* parent, const QString& currentVersion, const QString& requiredVersion);
};
