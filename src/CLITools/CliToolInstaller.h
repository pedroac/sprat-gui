#pragma once

#include <QObject>
#include <QStringList>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>

/**
 * @class CliToolInstaller
 * @brief Manages installation and resolution of CLI tools.
 * 
 * This class handles the detection, installation, and management
 * of external CLI tools required by the application.
 */
class CliToolInstaller : public QObject {
    Q_OBJECT
public:
    explicit CliToolInstaller(QObject* parent = nullptr);
    ~CliToolInstaller() override;

    bool resolveCliBinaries(QStringList& missing);
    void installCliTools();

signals:
#ifndef SPRAT_EMBEDDED_CLI
    void installFinished(int exitCode, QProcess::ExitStatus exitStatus);
#else
    void installFinished(int exitCode, int exitStatus);
#endif
    void installStarted();
    void installLog(const QString& message);
    void cliToolsResolved(bool ready);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);

private slots:
#ifndef SPRAT_EMBEDDED_CLI
    void onInstallProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onInstallProcessOutput();
#endif
    void onDownloadFinished(QNetworkReply* reply);
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);

private:
    void startDownload(const QUrl& url);
    void installFromDownloadedFile(const QString& filePath);
    void installOnLinux();

#ifndef SPRAT_EMBEDDED_CLI
    QProcess* m_installProcess = nullptr;
    QByteArray m_installOutputBuffer;
#endif
    QNetworkAccessManager* m_networkManager = nullptr;
    const QString m_cliVersion = SPRAT_CLI_VERSION; // Target version for compatibility
};
