#ifndef Q_OS_WASM

#include "UpdateInstaller.h"
#include "ArchiveExtractor.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QProcess>

// Wraps s in single quotes and escapes embedded single quotes for POSIX sh.
static QString shellQuote(const QString& s) {
    QString escaped = s;
    escaped.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

UpdateInstaller::UpdateInstaller(QObject* parent)
    : QObject(parent),
      m_networkManager(new QNetworkAccessManager(this)) {
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &UpdateInstaller::onDownloadFinished);
}

void UpdateInstaller::download(const QString& url) {
    QUrl qurl(url);
    QNetworkRequest request(qurl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::downloadProgress,
            this, &UpdateInstaller::onDownloadProgress);
}

void UpdateInstaller::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    emit downloadProgress(bytesReceived, bytesTotal);
}

void UpdateInstaller::onDownloadFinished(QNetworkReply* reply) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit error("Download failed: " + reply->errorString());
        return;
    }

    if (!m_tempDir.isValid()) {
        emit error("Could not create temporary directory for update");
        return;
    }

    // Save archive to temp dir
    QString archiveName;
#if defined(Q_OS_LINUX)
    archiveName = QLatin1String("update.tar.gz");
#else
    archiveName = QLatin1String("update.zip");
#endif
    const QString archivePath = QDir(m_tempDir.path()).filePath(archiveName);
    QFile archiveFile(archivePath);
    if (!archiveFile.open(QIODevice::WriteOnly)) {
        emit error("Could not save downloaded archive");
        return;
    }
    archiveFile.write(reply->readAll());
    archiveFile.close();

    // Extract to a subdirectory so we know the exact layout
    m_extractedDir = QDir(m_tempDir.path()).filePath(QLatin1String("extracted"));
    QDir().mkpath(m_extractedDir);

    QString extractError;
    if (!ArchiveExtractor::extractToDirectory(archivePath, m_extractedDir, extractError)) {
        emit error("Extraction failed: " + extractError);
        return;
    }

    emit extractionDone();
}

void UpdateInstaller::applyAndRestart() {
    const QString installDir = QCoreApplication::applicationDirPath();
    const QString tempPath = m_tempDir.path();

    // Don't auto-remove — the script must outlive the process
    m_tempDir.setAutoRemove(false);

#if defined(Q_OS_LINUX)
    const QString newBinary =
        QDir(m_extractedDir).filePath(QLatin1String("sprat-gui"));
    const QString targetBinary =
        QDir(installDir).filePath(QLatin1String("sprat-gui"));
    const QString scriptPath =
        QDir(tempPath).filePath(QLatin1String("update.sh"));

    const QString script =
        QLatin1String("#!/bin/sh\n"
                      "sleep 2\n"
                      "cp ") + shellQuote(newBinary) +
        QLatin1String(" ") + shellQuote(targetBinary) +
        QLatin1String("\nchmod +x ") + shellQuote(targetBinary) +
        QLatin1String("\nexec ") + shellQuote(targetBinary) +
        QLatin1String("\n");

    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit error("Could not write update script");
        return;
    }
    scriptFile.write(script.toUtf8());
    scriptFile.close();
    scriptFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                              QFileDevice::ExeOwner);
    QProcess::startDetached(QLatin1String("sh"), {scriptPath});

#elif defined(Q_OS_MACOS)
    // applicationDirPath() == .../sprat-gui.app/Contents/MacOS
    // go up three levels to reach the directory containing sprat-gui.app
    const QString appBundleParent =
        QDir(installDir).absoluteFilePath(QLatin1String("../../.."));
    const QString scriptPath =
        QDir(tempPath).filePath(QLatin1String("update.sh"));

    const QString script =
        QLatin1String("#!/bin/sh\n"
                      "sleep 2\n"
                      "cp -rp ") +
        shellQuote(QDir(m_extractedDir).filePath(QLatin1String("sprat-gui.app"))) +
        QLatin1String(" ") + shellQuote(appBundleParent) +
        QLatin1String("/\nopen ") +
        shellQuote(QDir(appBundleParent).filePath(QLatin1String("sprat-gui.app"))) +
        QLatin1String("\n");

    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit error("Could not write update script");
        return;
    }
    scriptFile.write(script.toUtf8());
    scriptFile.close();
    scriptFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                              QFileDevice::ExeOwner);
    QProcess::startDetached(QLatin1String("sh"), {scriptPath});

#elif defined(Q_OS_WIN)
    const qint64 currentPid = QCoreApplication::applicationPid();
    const QString scriptPath =
        QDir(tempPath).filePath(QLatin1String("update.ps1"));

    // Use native separators for PowerShell paths
    const QString srcDir = QDir::toNativeSeparators(m_extractedDir);
    const QString dstDir = QDir::toNativeSeparators(installDir);

    const QString script =
        QString(QLatin1String(
            "$pid_to_wait = %1\r\n"
            "while (Get-Process -Id $pid_to_wait -ErrorAction SilentlyContinue)"
            " { Start-Sleep -Seconds 1 }\r\n"
            "Copy-Item -Recurse -Force \"%2\\*\" \"%3\"\r\n"
            "Start-Process \"%3\\sprat-gui.exe\"\r\n"))
            .arg(currentPid)
            .arg(srcDir, dstDir);

    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit error("Could not write update script");
        return;
    }
    scriptFile.write(script.toUtf8());
    scriptFile.close();
    QProcess::startDetached(
        QLatin1String("powershell"),
        {QLatin1String("-ExecutionPolicy"), QLatin1String("Bypass"),
         QLatin1String("-File"), scriptPath});
#endif

    QCoreApplication::quit();
}

#endif // Q_OS_WASM
