#include "MainWindowUiState.h"

#include <QAction>
#include <QComboBox>

void MainWindowUiState::apply(
    bool cliReady,
    bool isLoading,
    bool hasSprites,
    bool hasExportPath,
    QAction* loadAction,
    QComboBox* profileCombo,
    QAction* saveAction,
    QAction* exportAction,
    QAction* saveAsAction) {
    bool cliEnabled = cliReady && !isLoading;
    loadAction->setEnabled(cliEnabled);
    profileCombo->setEnabled(cliEnabled);
    saveAction->setEnabled(!isLoading && hasSprites);
    if (saveAsAction)   saveAsAction->setEnabled(!isLoading && hasSprites);
    if (exportAction) {
        exportAction->setEnabled(cliEnabled && hasSprites && hasExportPath);
        exportAction->setToolTip(
            (cliEnabled && hasSprites && !hasExportPath)
            ? QObject::tr("No export path set — open the Exportation workspace first")
            : QObject::tr("Re-run the export pipeline to the last-used destination"));
    }
}
