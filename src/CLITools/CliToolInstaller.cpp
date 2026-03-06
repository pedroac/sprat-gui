#include "CliToolInstaller.h"
#include "CliToolsConfig.h"
#include <QStandardPaths>
#include <QDir>
#include <QProcess>
#include <QMessageBox>
#include <QApplication>
#include <QTemporaryFile>
#include <QFileInfo>
#include <QNetworkReply>

CliToolInstaller::CliToolInstaller(QObject* parent) 
    : QObject(parent), 
      m_installProcess(new QProcess(this)),
      m_networkManager(new QNetworkAccessManager(this)) {
    connect(m_installProcess, &QProcess::finished, this, &CliToolInstaller::onInstallProcessFinished);
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &CliToolInstaller::onDownloadFinished);
}

CliToolInstaller::~CliToolInstaller() {
    delete m_installProcess;
    delete m_networkManager;
}

bool CliToolInstaller::resolveCliBinaries(QStringList& missing) {
    QStringList tools = {"spratlayout", "spratpack", "spratconvert", "spratframes", "spratunpack"};
    for (const auto& tool : tools) {
        if (CliToolsConfig::resolveBinary(tool).isEmpty()) {
            missing << tool;
        }
    }
    return missing.isEmpty();
}

void CliToolInstaller::installCliTools() {
    emit installStarted();

#ifdef Q_OS_LINUX
    installOnLinux();
#else
    QString osSuffix;
#ifdef Q_OS_WIN
    osSuffix = "windows-x64.zip";
#elif defined(Q_OS_MACOS)
    osSuffix = "macos-x64.dmg";
#endif

    if (osSuffix.isEmpty()) {
        QMessageBox::critical(nullptr, "Unsupported OS", "Automatic installation is not supported on this operating system.");
        emit installFinished(-1, QProcess::CrashExit);
        return;
    }

    QUrl url(QString("https://github.com/pedroac/sprat-cli/releases/download/%1/sprat-cli-%2")
             .arg(m_cliVersion, osSuffix));
    startDownload(url);
#endif
}

void CliToolInstaller::installOnLinux() {
    bool hasCpp = !QStandardPaths::findExecutable("g++").isEmpty() || !QStandardPaths::findExecutable("c++").isEmpty();
    QStringList deps = {"git", "cmake", "make"};
    QStringList missing;
    for (const auto& dep : deps) {
        if (QStandardPaths::findExecutable(dep).isEmpty()) missing << dep;
    }
    if (!hasCpp) missing << "g++";

    if (!missing.isEmpty()) {
        QMessageBox::critical(nullptr, "Missing Dependencies",
                              "Cannot install sprat-cli. Missing build tools: " + missing.join(", "));
        emit installFinished(-1, QProcess::CrashExit);
        return;
    }

    QString script = QString(R"(
set -euo pipefail
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT
cd "$workdir"
git clone --depth 1 --branch %1 https://github.com/pedroac/sprat-cli.git
cd sprat-cli
cmake -DSPRAT_DOWNLOAD_STB=ON .
make -j$(nproc 2>/dev/null || echo 1)
mkdir -p "$HOME/.local/bin"
install -m 0755 spratlayout spratpack spratconvert spratframes spratunpack "$HOME/.local/bin/"
)").arg(m_cliVersion);

    m_installProcess->start("bash", QStringList() << "-c" << script);
}

void CliToolInstaller::startDownload(const QUrl& url) {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, &CliToolInstaller::onDownloadProgress);
}

void CliToolInstaller::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    emit downloadProgress(bytesReceived, bytesTotal);
}

void CliToolInstaller::onDownloadFinished(QNetworkReply* reply) {
    if (reply->error() != QNetworkReply::NoError) {
        QMessageBox::critical(nullptr, "Download Failed", "Could not download sprat-cli: " + reply->errorString());
        emit installFinished(-1, QProcess::CrashExit);
        reply->deleteLater();
        return;
    }

    QTemporaryFile* tempFile = new QTemporaryFile(this);
    if (!tempFile->open()) {
        QMessageBox::critical(nullptr, "Install Failed", "Could not create a temporary file for downloaded installer.");
        emit installFinished(-1, QProcess::CrashExit);
        reply->deleteLater();
        return;
    }
    tempFile->write(reply->readAll());
    QString tempPath = tempFile->fileName();
    tempFile->close();
    installFromDownloadedFile(tempPath);
    reply->deleteLater();
}

void CliToolInstaller::installFromDownloadedFile(const QString& filePath) {
    QString appDir = QApplication::applicationDirPath();
    
#ifdef Q_OS_WIN
    // Use PowerShell to extract ZIP
    QString script = QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force").arg(filePath, appDir);
    m_installProcess->start("powershell", QStringList() << "-Command" << script);
#elif defined(Q_OS_MACOS)
    // Use hdiutil to mount and cp
    QString script = QString("MOUNT_POINT=$(hdiutil mount \"%1\" | grep -o '/Volumes/.*' | head -n 1)\n"
                             "cp \"$MOUNT_POINT\"/* \"%2/\"\n"
                             "hdiutil unmount \"$MOUNT_POINT\"").arg(filePath, appDir);
    m_installProcess->start("bash", QStringList() << "-c" << script);
#endif
}

void CliToolInstaller::onInstallProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    emit installFinished(exitCode, exitStatus);
}
