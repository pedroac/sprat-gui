#include "ProjectFileLoader.h"

#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>

namespace {
QString powerShellSingleQuoted(const QString& value) {
    QString out = value;
    out.replace("'", "''");
    return QString("'%1'").arg(out);
}
}

bool ProjectFileLoader::load(const QString& path, QJsonObject& root, QString& error) {
    constexpr int kZipReadTimeoutMs = 30000;
    QByteArray jsonData;
    if (path.endsWith(".zip", Qt::CaseInsensitive)) {
        QString unzipBin = QStandardPaths::findExecutable("unzip");
        bool usePowerShell = false;
        if (unzipBin.isEmpty()) {
#ifdef Q_OS_WIN
            unzipBin = QStandardPaths::findExecutable("powershell");
            if (!unzipBin.isEmpty()) {
                usePowerShell = true;
            } else {
                error = "Neither 'unzip' nor 'powershell' was found. Cannot load zip projects.";
                return false;
            }
#else
            error = "The 'unzip' command line tool is required to load .zip projects but was not found.";
            return false;
#endif
        }

        QProcess unzip;
        if (usePowerShell) {
            const QString escapedPath = powerShellSingleQuoted(path);
            QString script = QString("Expand-Archive -Path %1 -DestinationPath (Join-Path $env:TEMP 'sprat-unzip') -Force; Get-Content (Join-Path $env:TEMP 'sprat-unzip/project.spart.json') -Raw")
                                 .arg(escapedPath);
            unzip.start(unzipBin, QStringList() << "-Command" << script);
        } else {
            unzip.start(unzipBin, QStringList() << "-p" << path << "project.spart.json");
        }

        if (!unzip.waitForStarted()) {
            error = "Could not start archive reader.";
            return false;
        }
        if (!unzip.waitForFinished(kZipReadTimeoutMs) || unzip.exitStatus() != QProcess::NormalExit || unzip.exitCode() != 0) {
            error = "Could not read project.spart.json from zip.";
            return false;
        }
        jsonData = unzip.readAllStandardOutput();
    } else {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            error = "Could not open file.";
            return false;
        }
        jsonData = file.readAll();
    }

    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (doc.isNull() || !doc.isObject()) {
        error = "Invalid JSON data.";
        return false;
    }
    root = doc.object();
    return true;
}
