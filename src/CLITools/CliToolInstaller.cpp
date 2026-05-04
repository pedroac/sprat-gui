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
#ifndef SPRAT_EMBEDDED_CLI
      m_installProcess(new QProcess(this)),
#endif
      m_networkManager(new QNetworkAccessManager(this)) {
#ifndef SPRAT_EMBEDDED_CLI
    m_installProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_installProcess, &QProcess::finished, this, &CliToolInstaller::onInstallProcessFinished);
    connect(m_installProcess, &QProcess::readyReadStandardOutput, this, &CliToolInstaller::onInstallProcessOutput);
#endif
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &CliToolInstaller::onDownloadFinished);
}

CliToolInstaller::~CliToolInstaller() {
#ifndef SPRAT_EMBEDDED_CLI
    delete m_installProcess;
#endif
    delete m_networkManager;
}

bool CliToolInstaller::resolveCliBinaries(QStringList& missing) {
#ifdef SPRAT_EMBEDDED_CLI
    Q_UNUSED(missing);
    return true; // Embedded CLI tools are always "present"
#else
    QStringList tools = {"spratlayout", "spratpack", "spratconvert", "spratframes", "spratunpack"};
    for (const auto& tool : tools) {
        if (CliToolsConfig::resolveBinary(tool).isEmpty()) {
            missing << tool;
        }
    }
    return missing.isEmpty();
#endif
}

void CliToolInstaller::installCliTools() {
    emit installStarted();

#ifdef SPRAT_EMBEDDED_CLI
    emit installFinished(0, 0);
#else
    emit installLog(QString("Installing sprat-cli %1").arg(m_cliVersion));
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
    emit installLog(QString("Downloading release %1 (%2)").arg(m_cliVersion, osSuffix));
    startDownload(url);
#endif
#endif
}

void CliToolInstaller::installOnLinux() {
#ifndef SPRAT_EMBEDDED_CLI
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

    emit installLog(QString("Cloning sprat-cli (%1)...").arg(m_cliVersion));
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
mkdir -p "$HOME/.local/share/sprat/transforms"
cp spratprofiles.cfg "$HOME/.local/share/sprat/"
cp -r transforms/* "$HOME/.local/share/sprat/transforms/"
)").arg(m_cliVersion);

    m_installProcess->start("bash", QStringList() << "-c" << script);
#endif
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
#ifndef SPRAT_EMBEDDED_CLI
        emit installFinished(-1, QProcess::CrashExit);
#else
        emit installFinished(-1, -1);
#endif
        reply->deleteLater();
        return;
    }

    QTemporaryFile* tempFile = new QTemporaryFile(this);
    if (!tempFile->open()) {
        QMessageBox::critical(nullptr, "Install Failed", "Could not create a temporary file for downloaded installer.");
#ifndef SPRAT_EMBEDDED_CLI
        emit installFinished(-1, QProcess::CrashExit);
#else
        emit installFinished(-1, -1);
#endif
        reply->deleteLater();
        return;
    }
    tempFile->write(reply->readAll());
    QString tempPath = tempFile->fileName();
    tempFile->close();
    emit installLog("Download finished. Extracting installer...");
    installFromDownloadedFile(tempPath);
    reply->deleteLater();
}

void CliToolInstaller::installFromDownloadedFile(const QString& filePath) {
#ifndef SPRAT_EMBEDDED_CLI
    QString appDir = QApplication::applicationDirPath();
    
#ifdef Q_OS_WIN
    emit installLog("Extracting ZIP...");
    // Extract ZIP into the CLI directory
    QString destDir = CliToolsConfig::defaultInstallDir();
    QDir().mkpath(destDir);
    QString script = QString("$ErrorActionPreference = 'Stop'; "
                             "Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
                             .arg(QString(filePath).replace("'", "''"), QString(destDir).replace("'", "''"));
    m_installProcess->start("powershell", QStringList() << "-Command" << script);
#elif defined(Q_OS_MACOS)
    emit installLog("Mounting DMG and copying files...");
    // Use hdiutil to mount and cp while preserving structure
    QString script = QString("MOUNT_POINT=$(hdiutil mount \"%1\" | grep -o '/Volumes/.*' | head -n 1)\n"
                             "if [ -z \"$MOUNT_POINT\" ]; then echo \"Failed to mount DMG\"; exit 1; fi\n"
                             "cp -R \"$MOUNT_POINT\"/ \"%2/cli/\"\n"
                             "hdiutil unmount \"$MOUNT_POINT\"").arg(filePath, appDir);
    m_installProcess->start("bash", QStringList() << "-c" << script);
#endif
#else
    Q_UNUSED(filePath);
#endif
}

#ifndef SPRAT_EMBEDDED_CLI
void CliToolInstaller::onInstallProcessOutput() {
    if (!m_installProcess) {
        return;
    }
    m_installOutputBuffer.append(m_installProcess->readAllStandardOutput());
    int newlineIndex = -1;
    while ((newlineIndex = m_installOutputBuffer.indexOf('\n')) != -1) {
        QByteArray line = m_installOutputBuffer.left(newlineIndex);
        m_installOutputBuffer.remove(0, newlineIndex + 1);
        const QString text = QString::fromLocal8Bit(line).trimmed();
        if (!text.isEmpty()) {
            emit installLog(text);
        }
    }
    if (m_installOutputBuffer.size() > 8192) {
        const QString text = QString::fromLocal8Bit(m_installOutputBuffer).trimmed();
        if (!text.isEmpty()) {
            emit installLog(text);
        }
        m_installOutputBuffer.clear();
    }
}

void CliToolInstaller::onInstallProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (!m_installOutputBuffer.isEmpty()) {
        const QString text = QString::fromLocal8Bit(m_installOutputBuffer).trimmed();
        if (!text.isEmpty()) {
            emit installLog(text);
        }
        m_installOutputBuffer.clear();
    }
    emit installFinished(exitCode, exitStatus);
}
#endif
