#pragma once
#include <QDialog>
#include "models.h"

class QLineEdit;
class QComboBox;
class QVBoxLayout;
class QDoubleSpinBox;

/**
 * @brief Dialog for configuring project export settings.
 * 
 * Allows the user to select destination, format, and output scales.
 */
class SaveDialog : public QDialog {
    Q_OBJECT
public:
    /**
     * @brief Constructs the SaveDialog.
     * @param defaultPath The default destination path.
     * @param parent The parent widget.
     */
    explicit SaveDialog(const QString& defaultPath, QWidget* parent = nullptr);
    /**
     * @brief Retrieves the configuration entered by the user.
     * @return A SaveConfig struct containing the settings.
     */
    SaveConfig getConfig() const;

private slots:
    void onBrowseFolder();
    void onBrowseFile();
    void onAddScale();

private:
    void setupUi();
    /**
     * @brief Adds a row for configuring a scale.
     * @param name The initial name of the scale.
     * @param value The initial scale value.
     */
    void addScaleRow(const QString& name, double value);
    /**
     * @brief Updates the enabled state of remove buttons.
     */
    void updateRemoveButtonsState();

    QLineEdit* m_destEdit;
    QComboBox* m_transformCombo;
    QVBoxLayout* m_scalesLayout;
};