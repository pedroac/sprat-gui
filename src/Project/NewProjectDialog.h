#pragma once
#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;
class QCheckBox;

class NewProjectDialog : public QDialog {
    Q_OBJECT
public:
    struct Result {
        QString name;
        QString location;
        bool createSubfolder;
    };

    explicit NewProjectDialog(const QString& defaultLocation, QWidget* parent = nullptr);

    void setProjectName(const QString& name);
    void setLocation(const QString& path);
    void setCreateSubfolder(bool enabled);

    Result getResult() const;

private slots:
    void onBrowseClicked();

private:
    void setupUi();

    QString m_defaultLocation;
    QLineEdit* m_nameEdit;
    QLabel* m_pathDisplay;
    QCheckBox* m_subfolderCheck;
};
