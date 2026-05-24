#include "MainWindowUiState.h"

#include <QAction>
#include <QComboBox>

void MainWindowUiState::apply(
    bool cliReady,
    bool isLoading,
    bool hasSprites,
    QAction* loadAction,
    QComboBox* profileCombo,
    QAction* saveAction,
    QAction* exportAction,
    QAction* exportAsAction,
    QAction* saveAsAction) {
    bool cliEnabled = cliReady && !isLoading;
    loadAction->setEnabled(cliEnabled);
    profileCombo->setEnabled(cliEnabled);
    saveAction->setEnabled(!isLoading && hasSprites);
    if (saveAsAction)   saveAsAction->setEnabled(!isLoading && hasSprites);
    if (exportAction)   exportAction->setEnabled(cliEnabled && hasSprites);
    if (exportAsAction) exportAsAction->setEnabled(cliEnabled && hasSprites);
}
