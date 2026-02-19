#include "MainWindowUiState.h"

#include <QAction>
#include <QComboBox>

void MainWindowUiState::apply(
    bool cliReady,
    bool isLoading,
    bool hasSprites,
    QAction* loadAction,
    QComboBox* profileCombo,
    QAction* saveAction) {
    bool enabled = cliReady && !isLoading;
    loadAction->setEnabled(enabled);
    profileCombo->setEnabled(enabled);
    saveAction->setEnabled(enabled && hasSprites);
}
