#include "CliToolsUi.h"

#include <QMessageBox>
#include <QPushButton>

MissingCliAction CliToolsUi::askMissingCliAction(QWidget* parent, const QStringList& missing) {
    QMessageBox msgBox(parent);
    msgBox.setWindowTitle("Missing CLI Tools");
    msgBox.setText("The following sprat commands were not found:\n" + missing.join(", "));
    msgBox.setInformativeText("Do you want to download and install them, or provide a path?");

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
