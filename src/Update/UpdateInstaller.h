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

    // Starts async download; emits downloadProgress during transfer,
    // extractionDone on success, or error on failure.
    void download(const QString& url);

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
    QNetworkAccessManager* m_networkManager;
    QTemporaryDir m_tempDir;
    QString m_extractedDir;
};

#endif // Q_OS_WASM
