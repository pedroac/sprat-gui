#pragma once

class QAction;
class QComboBox;

class MainWindowUiState {
public:
    static void apply(
        bool cliReady,
        bool isLoading,
        bool hasSprites,
        bool hasExportPath,
        QAction* loadAction,
        QComboBox* profileCombo,
        QAction* saveAction,
        QAction* exportAction = nullptr,
        QAction* saveAsAction = nullptr);
};
