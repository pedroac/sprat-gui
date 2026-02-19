#include "CliToolInstaller.h"
#include <QStandardPaths>
#include <QSettings>
#include <QDir>
#include <QProcess>
#include <QMessageBox>
#include <QStandardPaths>
#include <QApplication>

CliToolInstaller::CliToolInstaller(QObject* parent) : QObject(parent), m_installProcess(new QProcess(this)) {
    connect(m_installProcess, &QProcess::finished, this, &CliToolInstaller::onInstallProcessFinished);
}

CliToolInstaller::~CliToolInstaller() {
    delete m_installProcess;
}

bool CliToolInstaller::resolveCliBinaries(QStringList& missing) {
    QString configPath = QDir::homePath() + "/.config/sprat/sprat.conf";
    QSettings settings(configPath, QSettings::IniFormat);
    QString binDir = settings.value("cli/bin_dir").toString();

    auto findBin = [&](const QString& name) -> QString {
        // 1. Check PATH
        QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty()) {
            return path;
        }

        // 2. Check config bin_dir
        if (!binDir.isEmpty()) {
            QFileInfo fi(QDir(binDir).filePath(name));
            if (fi.exists() && fi.isExecutable()) {
                return fi.absoluteFilePath();
            }
        }

        // 3. Check ~/.local/bin
        QString localBin = QDir::homePath() + "/.local/bin/" + name;
        if (QFile::exists(localBin) && QFileInfo(localBin).isExecutable()) {
            return localBin;
        }
        return QString();
    };

    QString m_spratLayoutBin = findBin("spratlayout");
    if (m_spratLayoutBin.isEmpty()) {
        missing << "spratlayout";
    }

    QString m_spratPackBin = findBin("spratpack");
    if (m_spratPackBin.isEmpty()) {
        missing << "spratpack";
    }

    QString m_spratConvertBin = findBin("spratconvert");
    if (m_spratConvertBin.isEmpty()) {
        missing << "spratconvert";
    }

    return missing.isEmpty();
}

void CliToolInstaller::installCliTools() {
    // Check for g++ or c++
    bool hasCpp = !QStandardPaths::findExecutable("g++").isEmpty() || !QStandardPaths::findExecutable("c++").isEmpty();
    QStringList deps = {"git", "cmake", "make"};
    QStringList missing;
    for (const auto& dep : deps) {
        if (QStandardPaths::findExecutable(dep).isEmpty()) {
            missing << dep;
        }
    }

    if (!hasCpp) {
        missing << "g++";
    }

    if (!missing.isEmpty()) {
        QMessageBox::critical(nullptr, "Missing Dependencies",
                              "Cannot install sprat-cli. Missing build tools: " + missing.join(", "));
        return;
    }

    emit installStarted();

#ifdef Q_OS_LINUX
    QString script = R"(
set -euo pipefail
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT
cd "$workdir"
git clone https://github.com/pedroac/sprat-cli.git
cd sprat-cli
cmake -DSPRAT_DOWNLOAD_STB=ON .
make -j$(nproc)
mkdir -p "$HOME/.local/bin"
install -m 0755 spratlayout spratpack spratconvert "$HOME/.local/bin/"
echo 'Installation Complete.  You may need to restart this application.'
)";

    m_installProcess->start("bash", QStringList() << "-c" << script);
#endif

}

void CliToolInstaller::onInstallProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    emit installFinished(exitCode, exitStatus);
}