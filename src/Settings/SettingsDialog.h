#pragma once
#include <QDialog>
#include "models.h"

class QPushButton;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QScrollArea;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    enum class Section {
        Styles,
        Spritesheet,
        CliTools
    };

    explicit SettingsDialog(const AppSettings& settings, const CliPaths& cliPaths, QWidget* parent = nullptr, Section section = Section::Styles);
    AppSettings getSettings() const;
    CliPaths getCliPaths() const;

signals:
    void installCliToolsRequested();
    void syncNowRequested();

private slots:
    void pickColor(QPushButton* btn, QColor& color);
    void resetToDefaults();
    void pickCliBaseDir();
    void onSyncModeChanged(int index);
    void onSyncNowClicked();

private:
    void setupUi();
    void focusSection(Section section);
    QPushButton* createColorButton(const QColor& color);
    void updateColorButton(QPushButton* btn, const QColor& color);
    void updateCliUi();

    AppSettings m_settings;
    CliPaths m_cliPaths;
    Section m_initialSection = Section::Styles;

    QScrollArea* m_scrollArea = nullptr;
    QGroupBox* m_stylesGroup = nullptr;
    QGroupBox* m_spritesheetGroup = nullptr;
    QGroupBox* m_cliGroup = nullptr;
    
    QPushButton* m_canvasColorBtn;
    QPushButton* m_frameColorBtn;
    QCheckBox* m_checkerboardCheck;
    QPushButton* m_borderColorBtn;
    QPushButton* m_detectionSelectedColorBtn;
    QComboBox* m_borderStyleCombo;
    QComboBox* m_deduplicateModeCombo;

    QLineEdit* m_cliBaseDirEdit;
    QPushButton* m_cliBaseDirBtn;
    QPushButton* m_installCliBtn;

    // Sync controls
    QComboBox* m_syncModeCombo;
    QPushButton* m_syncNowBtn;
};
