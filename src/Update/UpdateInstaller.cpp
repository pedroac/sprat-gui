#ifndef Q_OS_WASM

#include "UpdateInstaller.h"
#include "ArchiveExtractor.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QProcess>

namespace {

// Wraps s in single quotes and escapes embedded single quotes for POSIX sh.
QString shellQuote(const QString& s) {
    QString escaped = s;
    escaped.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

// Wraps s in single quotes for PowerShell, where nothing inside is interpolated.
QString powerShellQuote(const QString& s) {
    QString escaped = s;
    escaped.replace(QLatin1Char('\''), QLatin1String("''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

// Shell lines that wait for this process to exit, so its files can be replaced.
QString waitForThisProcess() {
    return QStringLiteral("while kill -0 %1 2>/dev/null; do sleep 0.5; done\n")
        .arg(QCoreApplication::applicationPid());
}

} // namespace

UpdateInstaller::UpdateInstaller(QObject* parent)
    : QObject(parent),
      m_networkManager(new QNetworkAccessManager(this)) {
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &UpdateInstaller::onDownloadFinished);
}

void UpdateInstaller::download(const QString& url, const QString& sha256) {
    m_expectedSha256 = sha256.toLower();
    QNetworkRequest request{QUrl(url)};
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

    const QByteArray data = reply->readAll();
    const QString actualSha256 = QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    if (m_expectedSha256.isEmpty() || actualSha256 != m_expectedSha256) {
        emit error("The downloaded update doesn't match its published SHA-256 checksum, "
                   "so it was not installed. Try again later or download it from the releases page.");
        return;
    }

    const bool isAppImage = qEnvironmentVariableIsSet("APPIMAGE");
    QString archiveName;
#if defined(Q_OS_LINUX)
    archiveName = isAppImage ? QLatin1String("sprat-gui.AppImage") : QLatin1String("update.tar.gz");
#else
    archiveName = QLatin1String("update.zip");
#endif
    const QString archivePath = QDir(m_tempDir.path()).filePath(archiveName);
    QFile archiveFile(archivePath);
    if (!archiveFile.open(QIODevice::WriteOnly) || archiveFile.write(data) != data.size()) {
        emit error("Could not save downloaded update: " + archiveFile.errorString());
        return;
    }
    archiveFile.close();

    if (isAppImage) {
        // The AppImage is the whole app; it replaces the running one as is.
        m_downloadedAppImage = archivePath;
        emit extractionDone();
        return;
    }

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

bool UpdateInstaller::launchShellScript(const QString& body) {
    const QString scriptPath = QDir(m_tempDir.path()).filePath(QLatin1String("update.sh"));
    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit error("Could not write update script");
        return false;
    }
    scriptFile.write((QStringLiteral("#!/bin/sh\nset -e\n") + waitForThisProcess() + body).toUtf8());
    scriptFile.close();
    scriptFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                              QFileDevice::ExeOwner);
    if (!QProcess::startDetached(QLatin1String("sh"), {scriptPath})) {
        emit error("Could not start the update script");
        return false;
    }
    return true;
}

void UpdateInstaller::applyAndRestart() {
    const QString installDir = QCoreApplication::applicationDirPath();

    // Don't auto-remove — the script must outlive the process
    m_tempDir.setAutoRemove(false);

#if defined(Q_OS_LINUX)
    QString body;
    if (!m_downloadedAppImage.isEmpty()) {
        // Write next to the AppImage first, then rename over it, so a failed copy leaves
        // the old one in place.
        const QString appImage = qEnvironmentVariable("APPIMAGE");
        const QString staged = appImage + QLatin1String(".new");
        body = QStringLiteral("cp %1 %2\nchmod +x %2\nmv -f %2 %3\nexec %3\n")
                   .arg(shellQuote(m_downloadedAppImage), shellQuote(staged), shellQuote(appImage));
    } else {
        // The archive holds the binary plus the profiles config and transforms it needs.
        const QString targetBinary = QDir(installDir).filePath(QLatin1String("sprat-gui"));
        body = QStringLiteral("cp -R %1/. %2/\nchmod +x %3\nexec %3\n")
                   .arg(shellQuote(m_extractedDir), shellQuote(installDir), shellQuote(targetBinary));
    }
    if (!launchShellScript(body))
        return;

#elif defined(Q_OS_MACOS)
    // applicationDirPath() == .../<name>.app/Contents/MacOS. Replace the whole bundle:
    // copying over it would keep files the new version no longer has, which breaks its
    // code signature. The old bundle is only removed once the new one is in place.
    const QString bundle = QDir::cleanPath(QDir(installDir).absoluteFilePath(QLatin1String("../..")));
    const QString newBundle = QDir(m_extractedDir).filePath(QLatin1String("sprat-gui.app"));
    const QString staged = bundle + QLatin1String(".new");
    const QString old = bundle + QLatin1String(".old");
    const QString body =
        QStringLiteral("rm -rf %2 %3\nditto %1 %2\nmv %4 %3\nmv %2 %4\nrm -rf %3\nopen %4\n")
            .arg(shellQuote(newBundle), shellQuote(staged), shellQuote(old), shellQuote(bundle));
    if (!launchShellScript(body))
        return;

#elif defined(Q_OS_WIN)
    const qint64 currentPid = QCoreApplication::applicationPid();
    const QString scriptPath =
        QDir(m_tempDir.path()).filePath(QLatin1String("update.ps1"));

    const QString srcDir = QDir::toNativeSeparators(m_extractedDir);
    const QString dstDir = QDir::toNativeSeparators(installDir);
    const QString exePath = QDir::toNativeSeparators(QDir(installDir).filePath(QLatin1String("sprat-gui.exe")));

    const QString script =
        QString(QLatin1String(
            "$ErrorActionPreference = 'Stop'\r\n"
            "$pid_to_wait = %1\r\n"
            "while (Get-Process -Id $pid_to_wait -ErrorAction SilentlyContinue)"
            " { Start-Sleep -Seconds 1 }\r\n"
            "Copy-Item -Recurse -Force -Path (Join-Path %2 '*') -Destination %3\r\n"
            "Start-Process -FilePath %4\r\n"))
            .arg(currentPid)
            .arg(powerShellQuote(srcDir), powerShellQuote(dstDir), powerShellQuote(exePath));

    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit error("Could not write update script");
        return;
    }
    scriptFile.write(script.toUtf8());
    scriptFile.close();
    if (!QProcess::startDetached(
            QLatin1String("powershell"),
            {QLatin1String("-ExecutionPolicy"), QLatin1String("Bypass"),
             QLatin1String("-File"), scriptPath})) {
        emit error("Could not start the update script");
        return;
    }
#endif

    QCoreApplication::quit();
}

#endif // Q_OS_WASM
