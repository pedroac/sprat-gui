#include "MainWindowUiState.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>

void MainWindowUiState::apply(
    bool cliReady,
    bool isLoading,
    bool hasSprites,
    QAction* loadAction,
    QComboBox* profileCombo,
    QSpinBox* paddingSpin,
    QCheckBox* trimCheck,
    QAction* saveAction) {
    bool enabled = cliReady && !isLoading;
    loadAction->setEnabled(enabled);
    profileCombo->setEnabled(enabled);
    paddingSpin->setEnabled(enabled);
    trimCheck->setEnabled(enabled);
    saveAction->setEnabled(enabled && hasSprites);
}
