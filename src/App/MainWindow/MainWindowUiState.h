#pragma once

class QAction;
class QComboBox;

class MainWindowUiState {
public:
    static void apply(
        bool cliReady,
        bool isLoading,
        bool hasSprites,
        QAction* loadAction,
        QComboBox* profileCombo,
        QAction* saveAction);
};
