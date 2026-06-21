#ifndef Q_OS_WASM

#include "UpdateDialog.h"
#include "UpdateInstaller.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

UpdateDialog::UpdateDialog(const UpdateChecker::ReleaseInfo& info, QWidget* parent)
    : QDialog(parent),
      m_installer(new UpdateInstaller(this)),
      m_downloadUrl(info.downloadUrl) {
    setupUi(info);

    connect(m_installer, &UpdateInstaller::downloadProgress,
            this, &UpdateDialog::onDownloadProgress);
    connect(m_installer, &UpdateInstaller::extractionDone,
            this, &UpdateDialog::onExtractionDone);
    connect(m_installer, &UpdateInstaller::error,
            this, &UpdateDialog::onInstallError);
}

void UpdateDialog::setupUi(const UpdateChecker::ReleaseInfo& info) {
    setWindowTitle(tr("Update Available — %1").arg(info.version));
    setModal(true);
    setMinimumWidth(480);

    m_stack = new QStackedWidget(this);

    // ---- Page 1: Info ----
    m_infoPage = new QWidget;
    {
        auto* layout = new QVBoxLayout(m_infoPage);

        auto* versionLabel = new QLabel(
            tr("A new version is available: <b>%1</b><br>"
               "Current version: %2")
                .arg(info.version, QString(SPRAT_GUI_VERSION)),
            m_infoPage);
        versionLabel->setTextFormat(Qt::RichText);
        layout->addWidget(versionLabel);

        if (!info.releaseNotes.isEmpty()) {
            auto* notes = new QTextBrowser(m_infoPage);
            notes->setPlainText(info.releaseNotes);
            notes->setMinimumHeight(180);
            layout->addWidget(notes);
        }

        auto* btnRow = new QHBoxLayout;
        auto* downloadBtn = new QPushButton(tr("Download && Install"), m_infoPage);
        auto* laterBtn = new QPushButton(tr("Later"), m_infoPage);
        btnRow->addStretch();
        btnRow->addWidget(downloadBtn);
        btnRow->addWidget(laterBtn);
        layout->addLayout(btnRow);

        connect(downloadBtn, &QPushButton::clicked, this, &UpdateDialog::onDownloadClicked);
        connect(laterBtn, &QPushButton::clicked, this, &QDialog::reject);
    }

    // ---- Page 2: Downloading ----
    m_downloadPage = new QWidget;
    {
        auto* layout = new QVBoxLayout(m_downloadPage);

        auto* label = new QLabel(tr("Downloading update…"), m_downloadPage);
        layout->addWidget(label);

        m_progressBar = new QProgressBar(m_downloadPage);
        m_progressBar->setRange(0, 0); // indeterminate until we have a total
        layout->addWidget(m_progressBar);

        auto* btnRow = new QHBoxLayout;
        auto* cancelBtn = new QPushButton(tr("Cancel"), m_downloadPage);
        btnRow->addStretch();
        btnRow->addWidget(cancelBtn);
        layout->addLayout(btnRow);

        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    }

    // ---- Page 3: Applying ----
    m_applyingPage = new QWidget;
    {
        auto* layout = new QVBoxLayout(m_applyingPage);
        auto* label = new QLabel(
            tr("Applying update…\nThe application will restart shortly."),
            m_applyingPage);
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
    }

    m_stack->addWidget(m_infoPage);
    m_stack->addWidget(m_downloadPage);
    m_stack->addWidget(m_applyingPage);
    m_stack->setCurrentWidget(m_infoPage);

    auto* topLayout = new QVBoxLayout(this);
    topLayout->addWidget(m_stack);
}

void UpdateDialog::onDownloadClicked() {
    m_stack->setCurrentWidget(m_downloadPage);
    m_installer->download(m_downloadUrl);
}

void UpdateDialog::onDownloadProgress(qint64 received, qint64 total) {
    if (total > 0) {
        m_progressBar->setRange(0, static_cast<int>(total));
        m_progressBar->setValue(static_cast<int>(received));
    } else {
        m_progressBar->setRange(0, 0); // indeterminate
    }
}

void UpdateDialog::onExtractionDone() {
    m_stack->setCurrentWidget(m_applyingPage);
    m_installer->applyAndRestart();
}

void UpdateDialog::onInstallError(const QString& message) {
    QMessageBox::critical(this, tr("Update Failed"), message);
    reject();
}

#endif // Q_OS_WASM
