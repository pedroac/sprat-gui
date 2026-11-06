#pragma once
#ifndef Q_OS_WASM

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>

class UpdateInstaller : public QObject {
    Q_OBJECT
public:
    explicit UpdateInstaller(QObject* parent = nullptr);
    ~UpdateInstaller() override = default;

    /// Starts async download; emits downloadProgress during transfer,
    /// extractionDone on success, or error on failure.
    /// @param sha256 Expected lower-case hex SHA-256; a download that doesn't match is rejected
    void download(const QString& url, const QString& sha256);

    // Writes a platform-specific replacement script, launches it detached,
    // then calls QCoreApplication::quit().
    void applyAndRestart();

signals:
    void downloadProgress(qint64 received, qint64 total);
    void extractionDone();
    void error(const QString& message);

private slots:
    void onDownloadFinished(QNetworkReply* reply);
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);

private:
    /// Writes body (after waiting for this process to exit) to a script and starts it.
    bool launchShellScript(const QString& body);

    QNetworkAccessManager* m_networkManager;
    QTemporaryDir m_tempDir;
    QString m_extractedDir;
    QString m_downloadedAppImage;
    QString m_expectedSha256;
};

#endif // Q_OS_WASM
