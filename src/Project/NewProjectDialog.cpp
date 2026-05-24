#include "NewProjectDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QDir>

NewProjectDialog::NewProjectDialog(const QString& defaultLocation, QWidget* parent)
    : QDialog(parent), m_defaultLocation(defaultLocation)
{
    setWindowTitle(tr("New Project"));
    resize(500, 250);
    setupUi();
}

void NewProjectDialog::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);

    auto* descLabel = new QLabel(tr("Enter a name and choose where to save the project."), this);
    descLabel->setWordWrap(true);
    mainLayout->addWidget(descLabel);

    // Project Name
    QVBoxLayout* nameLayout = new QVBoxLayout();
    nameLayout->setSpacing(5);
    nameLayout->addWidget(new QLabel(tr("Project Name:")));
    m_nameEdit = new QLineEdit();
    m_nameEdit->setText(tr("Character Sheet"));
    m_nameEdit->selectAll();
    nameLayout->addWidget(m_nameEdit);
    mainLayout->addLayout(nameLayout);

    // Save Location
    QVBoxLayout* locationLayout = new QVBoxLayout();
    locationLayout->setSpacing(5);
    locationLayout->addWidget(new QLabel(tr("Save Location:")));
    
    QHBoxLayout* pathRow = new QHBoxLayout();
    m_pathDisplay = new QLabel(m_defaultLocation);
    m_pathDisplay->setStyleSheet("color: #666; font-style: italic;");
    pathRow->addWidget(m_pathDisplay, 1);
    
    QPushButton* browseBtn = new QPushButton(tr("Change Location..."));
    connect(browseBtn, &QPushButton::clicked, this, &NewProjectDialog::onBrowseClicked);
    pathRow->addWidget(browseBtn);
    locationLayout->addLayout(pathRow);
    mainLayout->addLayout(locationLayout);

    // Options
    m_subfolderCheck = new QCheckBox(tr("Create subfolder with project name"));
    m_subfolderCheck->setChecked(true);
    mainLayout->addWidget(m_subfolderCheck);

    mainLayout->addStretch();

    // Buttons
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Create Project"));
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void NewProjectDialog::onBrowseClicked() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Save Location"), m_pathDisplay->text());
    if (!dir.isEmpty()) {
        m_pathDisplay->setText(dir);
    }
}

void NewProjectDialog::setProjectName(const QString& name) {
    m_nameEdit->setText(name);
    m_nameEdit->selectAll();
}

void NewProjectDialog::setLocation(const QString& path) {
    m_pathDisplay->setText(path);
}

void NewProjectDialog::setCreateSubfolder(bool enabled) {
    m_subfolderCheck->setChecked(enabled);
}

NewProjectDialog::Result NewProjectDialog::getResult() const {
    return { m_nameEdit->text().trimmed(), m_pathDisplay->text(), m_subfolderCheck->isChecked() };
}
