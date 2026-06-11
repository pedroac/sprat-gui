#pragma once
#include <QDialog>
#include "models.h"

class QPushButton;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QScrollArea;
class QSpinBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    enum class Section {
        Spritesheet,
        FramesEditor,
        AtlasLayout,
        Exportation,
#ifndef Q_OS_WASM
        CliTools
#endif
    };

    explicit SettingsDialog(const AppSettings& settings, const CliPaths& cliPaths, QWidget* parent = nullptr, Section section = Section::FramesEditor);
    AppSettings getSettings() const;
    CliPaths getCliPaths() const;

signals:
    void installCliToolsRequested();
    void syncNowRequested();

private slots:
    void pickColor(QPushButton* btn, QColor& color);
    void resetToDefaults();
#ifndef Q_OS_WASM
    void pickCliBaseDir();
#endif
    void onSyncModeChanged(int index);
    void onSyncNowClicked();

private:
    void setupUi();
    void focusSection(Section section);
    QPushButton* createColorButton(const QColor& color);
    void updateColorButton(QPushButton* btn, const QColor& color);

    AppSettings m_settings;
    CliPaths m_cliPaths;
    Section m_initialSection = Section::FramesEditor;

    QScrollArea* m_scrollArea = nullptr;
    QGroupBox* m_spritesheetGroup = nullptr;
    QGroupBox* m_framesEditorGroup = nullptr;
    QGroupBox* m_atlasLayoutGroup = nullptr;
    QGroupBox* m_exportationGroup = nullptr;
#ifndef Q_OS_WASM
    QGroupBox* m_cliGroup = nullptr;
#endif
    
    QPushButton* m_canvasColorBtn;
    QPushButton* m_frameColorBtn;
    QCheckBox* m_checkerboardCheck;
    QPushButton* m_borderColorBtn;
    QPushButton* m_detectionSelectedColorBtn;
    QComboBox* m_borderStyleCombo;
    QComboBox* m_deduplicateModeCombo;

#ifndef Q_OS_WASM
    QLineEdit* m_cliBaseDirEdit;
    QPushButton* m_cliBaseDirBtn;
#endif

    // Sync controls
    QComboBox* m_syncModeCombo;
    QPushButton* m_syncNowBtn;

    // Frames Editor controls
    QSpinBox* m_onionSkinOpacitySpin = nullptr;
    QCheckBox* m_propagateEditsCheck = nullptr;
    QComboBox* m_flipbookModeCombo = nullptr;
    QComboBox* m_frameZoomModeCombo = nullptr;

    // Atlas Layout controls
    QComboBox* m_layoutZoomOnChangeCombo = nullptr;
    QComboBox* m_layoutLabelModeCombo = nullptr;

    // Exportation controls
    QComboBox* m_exportZoomOnChangeCombo = nullptr;
    QLineEdit* m_exportDefaultFolderEdit = nullptr;
    QComboBox* m_exportDefaultFormatCombo = nullptr;
    QComboBox* m_exportDefaultScaleFilterCombo = nullptr;
};
