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
    QString script = QString(R"SCRIPT(
set -euo pipefail
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

# Try to use local sprat-cli copy first (sibling directory)
local_sprat_cli="$(cd "$(dirname "%1")/.." 2>/dev/null && pwd)/sprat-cli" 2>/dev/null || true
if [ -d "$local_sprat_cli" ] && [ -f "$local_sprat_cli/CMakeLists.txt" ]; then
  echo "Found local sprat-cli at $local_sprat_cli, using it instead of cloning"
  cp -r "$local_sprat_cli" "$workdir/sprat-cli"
  # Clean up build artifacts to avoid CMakeCache.txt conflicts
  echo "Cleaning up build artifacts..."
  rm -rf "$workdir/sprat-cli/build" "$workdir/sprat-cli/build_test" "$workdir/sprat-cli/build_debug" "$workdir/sprat-cli/build_release" "$workdir/sprat-cli/build_win"
  rm -rf "$workdir/sprat-cli/CMakeCache.txt" "$workdir/sprat-cli/CMakeFiles" "$workdir/sprat-cli/cmake_install.cmake" "$workdir/sprat-cli/compile_commands.json"
else
  echo "No local sprat-cli found, cloning from GitHub..."
  cd "$workdir"
  git clone --depth 1 --branch %1 https://github.com/pedroac/sprat-cli.git 2>&1 | grep -E "(Cloning|fatal|error)" || true
fi

cd "$workdir/sprat-cli"
echo "VERSION file content:"
cat VERSION
echo "---"
cmake -DSPRAT_DOWNLOAD_STB=ON .
make -j$(nproc 2>/dev/null || echo 1)
mkdir -p "$HOME/.local/bin"
install -m 0755 spratlayout spratpack spratconvert spratframes spratunpack "$HOME/.local/bin/"
mkdir -p "$HOME/.local/share/sprat/transforms"
cp spratprofiles.cfg "$HOME/.local/share/sprat/"
cp -r transforms/* "$HOME/.local/share/sprat/transforms/"

echo "Checking installed CLI tool versions:"
echo "Expected version: %1"
echo "---"
for tool in spratlayout spratpack spratconvert spratframes spratunpack; do
  installed_version=$("$HOME/.local/bin/$tool" --version 2>&1 | grep -oP 'v\d+\.\d+\.\d+(?:[.\-][a-zA-Z0-9]+)*' | head -1)
  echo "$tool: $installed_version"
  if [ "$installed_version" != "%1" ]; then
    echo "ERROR: Version mismatch for $tool! Expected %1 but got $installed_version" >&2
    exit 1
  fi
done
echo "---"
echo "Successfully installed all sprat-cli tools with version %1"
)SCRIPT").arg(m_cliVersion);

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
    QString expectedVersion = m_cliVersion;

#ifdef Q_OS_WIN
    emit installLog("Extracting ZIP...");
    // Extract ZIP into the CLI directory
    QString destDir = CliToolsConfig::defaultInstallDir();
    QDir().mkpath(destDir);
    QString script = QString("$ErrorActionPreference = 'Stop'; "
                             "Expand-Archive -Path '%1' -DestinationPath '%2' -Force; "
                             "Write-Output 'Checking installed CLI tool versions:'; "
                             "Write-Output 'Expected version: %3'; "
                             "Write-Output '---'; "
                             "foreach ($tool in @('spratlayout', 'spratpack', 'spratconvert', 'spratframes', 'spratunpack')) { "
                             "  $exe = '%2\\' + $tool + '.exe'; "
                             "  $installed_version = & $exe --version 2>$null | Select-String -Pattern 'v\\d+\\.\\d+\\.\\d+' -AllMatches | ForEach-Object { $_.Matches[0].Value }; "
                             "  Write-Output \"$tool`: $installed_version\"; "
                             "  if ($installed_version -ne '%3') { throw \"Version mismatch for $tool! Expected %3 but got $installed_version\" } "
                             "}; "
                             "Write-Output '---'; "
                             "Write-Output 'Successfully installed all sprat-cli tools with version %3'")
                             .arg(QString(filePath).replace("'", "''"), QString(destDir).replace("'", "''"), expectedVersion);
    m_installProcess->start("powershell", QStringList() << "-Command" << script);
#elif defined(Q_OS_MACOS)
    emit installLog("Mounting DMG and copying files...");
    // Use hdiutil to mount and cp while preserving structure
    QString script = QString("set -euo pipefail\n"
                             "MOUNT_POINT=$(hdiutil mount \"%1\" | grep -o '/Volumes/.*' | head -n 1)\n"
                             "if [ -z \"$MOUNT_POINT\" ]; then echo \"Failed to mount DMG\"; exit 1; fi\n"
                             "mkdir -p \"%2/cli\"\n"
                             "cp -R \"$MOUNT_POINT\"/* \"%2/cli/\" 2>/dev/null || true\n"
                             "hdiutil unmount \"$MOUNT_POINT\"\n"
                             "echo \"Checking installed CLI tool versions:\"\n"
                             "echo \"Expected version: %3\"\n"
                             "echo \"---\"\n"
                             "for tool in spratlayout spratpack spratconvert spratframes spratunpack; do\n"
                             "  installed_version=$(\"%2/cli/$tool\" --version 2>&1 | grep -oP 'v\\d+\\.\\d+\\.\\d+(?:[.\\-][a-zA-Z0-9]+)*' | head -1)\n"
                             "  echo \"$tool: $installed_version\"\n"
                             "  if [ \"$installed_version\" != \"%3\" ]; then\n"
                             "    echo \"ERROR: Version mismatch for $tool! Expected %3 but got $installed_version\" >&2\n"
                             "    exit 1\n"
                             "  fi\n"
                             "done\n"
                             "echo \"---\"\n"
                             "echo \"Successfully installed all sprat-cli tools with version %3\"").arg(filePath, appDir, expectedVersion);
    m_installProcess->start("bash", QStringList() << "-c" << script);
#endif
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
