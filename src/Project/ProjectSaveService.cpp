#include "ProjectSaveService.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>

bool ProjectSaveService::save(
    QWidget* parent,
    SaveConfig config,
    const QString& currentFolder,
    const QString& profile,
    int padding,
    bool trimTransparent,
    const QString& spratLayoutBin,
    const QString& spratPackBin,
    const QString& spratConvertBin,
    const QJsonObject& projectPayload,
    QString& savedDestination,
    const std::function<void(bool)>& setLoading,
    const std::function<void(const QString&)>& setStatus) {
    constexpr int kProcessTimeoutMs = 120000;

    struct LoadingGuard {
        const std::function<void(bool)>& setLoading;
        bool active = true;
        ~LoadingGuard() {
            if (active) {
                setLoading(false);
            }
        }
    };

    QFileInfo destInfo(config.destination);
    bool isZip = config.destination.endsWith(".zip", Qt::CaseInsensitive);
    if (!isZip) {
        if (destInfo.exists() && destInfo.isDir()) {
            isZip = false;
        } else {
            config.destination += ".zip";
            isZip = true;
        }
    }

    QString zipBin;
    if (isZip) {
        zipBin = QStandardPaths::findExecutable("zip");
        if (zipBin.isEmpty()) {
            QMessageBox::critical(parent, "Error", "The 'zip' command line tool is required to save .zip projects but was not found.");
            return false;
        }
    }

    QTemporaryDir tempZipDir;
    QString workingPath;
    if (isZip) {
        if (!tempZipDir.isValid()) {
            QMessageBox::critical(parent, "Error", "Could not create temporary directory.");
            return false;
        }
        workingPath = tempZipDir.path();
    } else {
        workingPath = config.destination;
        QDir d(workingPath);
        if (!d.exists()) {
            d.mkpath(".");
        }
    }

    setLoading(true);
    LoadingGuard loadingGuard{setLoading};
    setStatus("Saving...");
    QApplication::processEvents();

    QDir destDir(workingPath);
    if (!destDir.exists()) {
        if (!destDir.mkpath(".")) {
            QMessageBox::critical(parent, "Error", "Could not create destination directory.");
            return false;
        }
    }

    QFile projectFile(destDir.filePath("project.spart.json"));
    if (!projectFile.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(parent, "Error", "Could not write project.spart.json.");
        return false;
    }
    if (projectFile.write(QJsonDocument(projectPayload).toJson()) < 0) {
        QMessageBox::critical(parent, "Error", "Failed to write project.spart.json.");
        return false;
    }
    projectFile.close();

    QJsonObject markersInfo = projectPayload["spritemarkers"].toObject();
    QJsonObject animInfo = projectPayload["animations"].toObject();
    QTemporaryFile markersTemp;
    if (!markersTemp.open()) {
        QMessageBox::critical(parent, "Error", "Could not create temporary markers file.");
        return false;
    }
    if (markersTemp.write(QJsonDocument(markersInfo).toJson()) < 0 || !markersTemp.flush()) {
        QMessageBox::critical(parent, "Error", "Could not write temporary markers file.");
        return false;
    }
    markersTemp.close();
    QTemporaryFile animTemp;
    if (!animTemp.open()) {
        QMessageBox::critical(parent, "Error", "Could not create temporary animation file.");
        return false;
    }
    if (animTemp.write(QJsonDocument(animInfo).toJson()) < 0 || !animTemp.flush()) {
        QMessageBox::critical(parent, "Error", "Could not write temporary animation file.");
        return false;
    }
    animTemp.close();

    if (config.scales.isEmpty()) {
        QMessageBox::critical(parent, "Error", "No output scales configured.");
        return false;
    }

    auto readStdErr = [](QProcess& process) {
        return QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    };
    auto runProcess = [&](QProcess& process, const QString& tool, const QStringList& args, const QString& step) -> bool {
        process.start(tool, args);
        if (!process.waitForStarted()) {
            QMessageBox::critical(parent, "Error", QString("%1: failed to start '%2'.").arg(step, tool));
            return false;
        }
        if (!process.waitForFinished(kProcessTimeoutMs)) {
            process.kill();
            process.waitForFinished();
            QMessageBox::critical(parent, "Error", QString("%1 timed out.").arg(step));
            return false;
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            const QString err = readStdErr(process);
            QMessageBox::critical(parent, "Error", err.isEmpty() ? QString("%1 failed.").arg(step)
                                                                  : QString("%1 failed:\n%2").arg(step, err));
            return false;
        }
        return true;
    };

    for (const auto& scale : config.scales) {
        QDir scaleDir(destDir.filePath(scale.name));
        if (!scaleDir.exists()) {
            if (!scaleDir.mkpath(".")) {
                QMessageBox::critical(parent, "Error", QString("Could not create scale directory: %1").arg(scale.name));
                return false;
            }
        }

        QProcess layoutProc;
        QStringList layoutArgs;
        layoutArgs << currentFolder;
        layoutArgs << "--profile" << profile;
        layoutArgs << "--padding" << QString::number(padding);
        layoutArgs << "--scale" << QString::number(scale.value);
        if (trimTransparent) {
            layoutArgs << "--trim-transparent";
        }
        if (!runProcess(layoutProc, spratLayoutBin, layoutArgs, QString("Layout generation failed for scale '%1'").arg(scale.name))) {
            return false;
        }
        QByteArray layoutData = layoutProc.readAllStandardOutput();

        QProcess packProc;
        packProc.setStandardInputFile(QProcess::nullDevice());
        packProc.start(spratPackBin, QStringList());
        if (!packProc.waitForStarted()) {
            QMessageBox::critical(parent, "Error", QString("Packing failed for scale '%1': could not start spratpack.").arg(scale.name));
            return false;
        }
        packProc.write(layoutData);
        packProc.closeWriteChannel();
        if (!packProc.waitForFinished(kProcessTimeoutMs)) {
            packProc.kill();
            packProc.waitForFinished();
            QMessageBox::critical(parent, "Error", QString("Packing timed out for scale '%1'.").arg(scale.name));
            return false;
        }
        if (packProc.exitStatus() != QProcess::NormalExit || packProc.exitCode() != 0) {
            const QString err = readStdErr(packProc);
            QMessageBox::critical(parent, "Error", err.isEmpty()
                                                      ? QString("Packing failed for scale '%1'.").arg(scale.name)
                                                      : QString("Packing failed for scale '%1':\n%2").arg(scale.name, err));
            return false;
        }
        QByteArray imageData = packProc.readAllStandardOutput();

        QFile imgFile(scaleDir.filePath("spritesheet.png"));
        if (!imgFile.open(QIODevice::WriteOnly) || imgFile.write(imageData) < 0) {
            QMessageBox::critical(parent, "Error", QString("Could not write spritesheet for scale '%1'.").arg(scale.name));
            return false;
        }
        imgFile.close();

        if (config.transform != "none" && !spratConvertBin.isEmpty()) {
            QProcess convProc;
            QStringList convArgs;
            convArgs << "--transform" << config.transform;
            convArgs << "--markers" << markersTemp.fileName();
            convArgs << "--animations" << animTemp.fileName();
            convProc.start(spratConvertBin, convArgs);
            if (!convProc.waitForStarted()) {
                QMessageBox::critical(parent, "Error", QString("Format conversion failed for scale '%1': could not start spratconvert.").arg(scale.name));
                return false;
            }
            convProc.write(layoutData);
            convProc.closeWriteChannel();
            if (!convProc.waitForFinished(kProcessTimeoutMs)) {
                convProc.kill();
                convProc.waitForFinished();
                QMessageBox::critical(parent, "Error", QString("Format conversion timed out for scale '%1'.").arg(scale.name));
                return false;
            }
            if (convProc.exitStatus() != QProcess::NormalExit || convProc.exitCode() != 0) {
                const QString err = readStdErr(convProc);
                QMessageBox::critical(parent, "Error", err.isEmpty()
                                                          ? QString("Format conversion failed for scale '%1'.").arg(scale.name)
                                                          : QString("Format conversion failed for scale '%1':\n%2").arg(scale.name, err));
                return false;
            }
            QByteArray convData = convProc.readAllStandardOutput();
            QString ext = config.transform == "css" ? "css" : (config.transform == "xml" ? "xml" : (config.transform == "csv" ? "csv" : "json"));
            QFile convFile(scaleDir.filePath("layout_formatted." + ext));
            if (!convFile.open(QIODevice::WriteOnly) || convFile.write(convData) < 0) {
                QMessageBox::critical(parent, "Error", QString("Could not write converted layout for scale '%1'.").arg(scale.name));
                return false;
            }
            convFile.close();
        }
    }

    if (isZip) {
        QProcess zipProc;
        zipProc.setWorkingDirectory(workingPath);
        QString absDest = QFileInfo(config.destination).absoluteFilePath();
        QDir().mkpath(QFileInfo(absDest).path());
        QFile::remove(absDest);
        if (!runProcess(zipProc, zipBin, QStringList() << "-r" << absDest << ".", "Failed to create zip archive")) {
            return false;
        }
    }

    loadingGuard.active = false;
    setLoading(false);
    savedDestination = config.destination;
    return true;
}
