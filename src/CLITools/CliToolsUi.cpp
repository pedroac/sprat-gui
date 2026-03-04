#include "CliToolsUi.h"
#include "CliToolsConfig.h"

#include <QMessageBox>
#include <QPushButton>

MissingCliAction CliToolsUi::askMissingCliAction(QWidget* parent, const QStringList& missing) {
    QMessageBox msgBox(parent);
    msgBox.setWindowTitle("Missing CLI Tools");
    msgBox.setText("The following sprat commands were not found:\n" + missing.join(", "));
    msgBox.setInformativeText(QString("Do you want to download and install them to %1, or provide a path?")
                              .arg(CliToolsConfig::defaultInstallDir()));

    QPushButton* installBtn = msgBox.addButton("Install", QMessageBox::ActionRole);
    QPushButton* pathBtn = msgBox.addButton("Provide Path", QMessageBox::ActionRole);
    QPushButton* quitBtn = msgBox.addButton("Quit", QMessageBox::DestructiveRole);
    msgBox.exec();

    if (msgBox.clickedButton() == installBtn) {
        return MissingCliAction::Install;
    }
    if (msgBox.clickedButton() == pathBtn) {
        return MissingCliAction::ProvidePath;
    }
    Q_UNUSED(quitBtn);
    return MissingCliAction::Quit;
}

bool CliToolsUi::askUpgrade(QWidget* parent, const QString& currentVersion, const QString& requiredVersion) {
    QMessageBox msgBox(parent);
    msgBox.setWindowTitle("CLI Version Mismatch");
    msgBox.setText(QString("An outdated version of sprat-cli was found (%1).\nRequired version: %2")
                   .arg(currentVersion, requiredVersion));
    msgBox.setInformativeText(QString("Would you like to upgrade the version at %1 now?")
                              .arg(CliToolsConfig::defaultInstallDir()));

    QPushButton* upgradeBtn = msgBox.addButton("Upgrade", QMessageBox::ActionRole);
    QPushButton* ignoreBtn = msgBox.addButton("Ignore (Risky)", QMessageBox::RejectRole);
    msgBox.exec();

    return msgBox.clickedButton() == upgradeBtn;
}
