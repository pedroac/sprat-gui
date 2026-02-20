#pragma once
#include <QDialog>
#include "models.h"
#include "SpratProfilesConfig.h"

class QLineEdit;
class QComboBox;
class QVBoxLayout;
class QCheckBox;

/**
 * @brief Dialog for configuring project export settings.
 * 
 * Allows the user to select destination, format, and output profiles.
 */
class SaveDialog : public QDialog {
    Q_OBJECT
public:
    /**
     * @brief Constructs the SaveDialog.
     * @param defaultPath The default destination path.
     * @param availableProfiles Profiles available for export.
     * @param selectedProfileName Name currently selected in layout UI.
     * @param parent The parent widget.
     */
    explicit SaveDialog(const QString& defaultPath,
                        const QVector<SpratProfile>& availableProfiles,
                        const QString& selectedProfileName,
                        QWidget* parent = nullptr);
    /**
     * @brief Retrieves the configuration entered by the user.
     * @return A SaveConfig struct containing the settings.
     */
    SaveConfig getConfig() const;

private slots:
    void onBrowseFolder();
    void onBrowseFile();

private:
    void setupUi();
    void updateProfileSelectionState();

    QLineEdit* m_destEdit;
    QComboBox* m_transformCombo;
    QVBoxLayout* m_profilesLayout;
    QVector<QCheckBox*> m_profileChecks;
};
