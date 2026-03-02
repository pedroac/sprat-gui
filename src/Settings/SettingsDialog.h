#pragma once
#include <QDialog>
#include "models.h"

class QPushButton;
class QLineEdit;
class QCheckBox;
class QComboBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const AppSettings& settings, const CliPaths& cliPaths, QWidget* parent = nullptr);
    AppSettings getSettings() const;
    CliPaths getCliPaths() const;

signals:
    void installCliToolsRequested();

private slots:
    void pickColor(QPushButton* btn, QColor& color);
    void resetToDefaults();
    void pickCliBaseDir();

private:
    void setupUi();
    QPushButton* createColorButton(const QColor& color);
    void updateColorButton(QPushButton* btn, const QColor& color);
    void updateCliUi();

    AppSettings m_settings;
    CliPaths m_cliPaths;
    
    QPushButton* m_canvasColorBtn;
    QPushButton* m_frameColorBtn;
    QCheckBox* m_checkerboardCheck;
    QPushButton* m_borderColorBtn;
    QPushButton* m_detectionSelectedColorBtn;
    QComboBox* m_borderStyleCombo;
    
    QLineEdit* m_cliBaseDirEdit;
    QPushButton* m_cliBaseDirBtn;
    QPushButton* m_installCliBtn;
};
