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

private:
    QWidget* createCliPathWidget(QLineEdit* edit, QPushButton* btn);
    void browseCliBinary(QLineEdit* target);
    void setupUi();
    QPushButton* createColorButton(const QColor& color);
    void updateColorButton(QPushButton* btn, const QColor& color);

    AppSettings m_settings;
    CliPaths m_cliPaths;
    
    QPushButton* m_canvasColorBtn;
    QPushButton* m_frameColorBtn;
    QPushButton* m_borderColorBtn;
    QComboBox* m_borderStyleCombo;
    QLineEdit* m_layoutPathEdit;
    QLineEdit* m_packPathEdit;
    QLineEdit* m_convertPathEdit;
    QPushButton* m_installCliBtn;
};
