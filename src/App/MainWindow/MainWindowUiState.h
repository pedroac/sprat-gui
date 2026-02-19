#pragma once

class QAction;
class QComboBox;
class QSpinBox;
class QCheckBox;

class MainWindowUiState {
public:
    static void apply(
        bool cliReady,
        bool isLoading,
        bool hasSprites,
        QAction* loadAction,
        QComboBox* profileCombo,
        QSpinBox* paddingSpin,
        QCheckBox* trimCheck,
        QAction* saveAction);
};
