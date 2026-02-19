#pragma once

#include <QDialog>
#include <QVector>

#include "SpratProfilesConfig.h"

class QListWidget;
class QPushButton;
class QDialogButtonBox;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;

class ProfilesDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProfilesDialog(const QVector<SpratProfile>& profiles, QWidget* parent = nullptr);
    QVector<SpratProfile> profiles() const;

private slots:
    void onAddProfile();
    void onRemoveProfile();
    void onCurrentProfileChanged(int row);
    void accept() override;

private:
    bool hasDuplicateName(const QString& name, int exceptRow = -1) const;
    void saveEditorsToProfile(int row);
    void loadEditorsFromProfile(int row);
    QString uniqueProfileName(const QString& base) const;

    QListWidget* m_listWidget = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;

    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_modeCombo = nullptr;
    QComboBox* m_optimizeCombo = nullptr;
    QCheckBox* m_useMaxWidthCheck = nullptr;
    QSpinBox* m_maxWidthSpin = nullptr;
    QCheckBox* m_useMaxHeightCheck = nullptr;
    QSpinBox* m_maxHeightSpin = nullptr;
    QSpinBox* m_paddingSpin = nullptr;
    QSpinBox* m_maxCombinationsSpin = nullptr;
    QDoubleSpinBox* m_scaleSpin = nullptr;
    QCheckBox* m_trimTransparentCheck = nullptr;

    QVector<SpratProfile> m_profiles;
    int m_currentRow = -1;
    bool m_updatingEditors = false;
};
