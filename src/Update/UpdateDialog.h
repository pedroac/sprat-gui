#pragma once
#ifndef Q_OS_WASM

#include <QDialog>
#include "UpdateChecker.h"

class QLabel;
class QTextBrowser;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class UpdateInstaller;

class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    explicit UpdateDialog(const UpdateChecker::ReleaseInfo& info,
                          QWidget* parent = nullptr);
    ~UpdateDialog() override = default;

private slots:
    void onDownloadClicked();
    void onDownloadProgress(qint64 received, qint64 total);
    void onExtractionDone();
    void onInstallError(const QString& message);

private:
    void setupUi(const UpdateChecker::ReleaseInfo& info);

    UpdateInstaller* m_installer = nullptr;
    QString m_downloadUrl;

    // Pages inside m_stack
    QWidget* m_infoPage = nullptr;
    QWidget* m_downloadPage = nullptr;
    QWidget* m_applyingPage = nullptr;

    QStackedWidget* m_stack = nullptr;
    QProgressBar* m_progressBar = nullptr;
};

#endif // Q_OS_WASM
