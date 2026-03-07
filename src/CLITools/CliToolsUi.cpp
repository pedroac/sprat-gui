#include "CliToolsUi.h"
#include "CliToolsConfig.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QPushButton>

namespace {
QString trCliToolsUi(const char* text) {
    return QCoreApplication::translate("CliToolsUi", text);
}
}

MissingCliAction CliToolsUi::askMissingCliAction(QWidget* parent, const QStringList& missing) {
    QMessageBox msgBox(parent);
    msgBox.setWindowTitle(trCliToolsUi("Missing CLI Tools"));
    msgBox.setText(trCliToolsUi("The following sprat commands were not found:\n") + missing.join(", "));
    
    QString informativeText;
#ifdef Q_OS_WIN
    informativeText = trCliToolsUi("The application expects CLI tools in a 'cli' folder next to the executable.\n");
#endif
    informativeText += QString(trCliToolsUi("Do you want to download and install them to %1, or provide a path?"))
                       .arg(CliToolsConfig::defaultInstallDir());
                       
    msgBox.setInformativeText(informativeText);

    QPushButton* installBtn = msgBox.addButton(trCliToolsUi("Install"), QMessageBox::ActionRole);
    QPushButton* pathBtn = msgBox.addButton(trCliToolsUi("Provide Path"), QMessageBox::ActionRole);
    QPushButton* quitBtn = msgBox.addButton(trCliToolsUi("Quit"), QMessageBox::DestructiveRole);
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
    msgBox.setWindowTitle(trCliToolsUi("CLI Version Mismatch"));
    msgBox.setText(QString(trCliToolsUi("An outdated version of sprat-cli was found (%1).\nRequired version: %2"))
                   .arg(currentVersion, requiredVersion));
    msgBox.setInformativeText(QString(trCliToolsUi("Would you like to upgrade the version at %1 now?"))
                              .arg(CliToolsConfig::defaultInstallDir()));

    QPushButton* upgradeBtn = msgBox.addButton(trCliToolsUi("Upgrade"), QMessageBox::ActionRole);
    msgBox.addButton(trCliToolsUi("Ignore (Risky)"), QMessageBox::RejectRole);
    msgBox.exec();

    return msgBox.clickedButton() == upgradeBtn;
}
