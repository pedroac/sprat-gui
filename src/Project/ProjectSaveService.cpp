#include "ProjectSaveService.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextStream>
#include <QDateTime>
#include <cmath>

namespace {
QString trPS(const char* text) {
    return QCoreApplication::translate("ProjectSaveService", text);
}

QString normalizedMarkerName(QString name) {
    name = name.trimmed();
    if (name.compare("pivot", Qt::CaseInsensitive) == 0) {
        return "pivot";
    }
    return name;
}
}

bool ProjectSaveService::save(
    QWidget* parent,
    SaveConfig config,
    const QString& layoutInputPath,
    const QStringList& framePaths,
    const QString& profile,
    int padding,
    bool trimTransparent,
    const QString& spratLayoutBin,
    const QString& spratPackBin,
    const QString& spratConvertBin,
    const QJsonObject& projectPayload,
    QString& savedDestination,
    const std::function<void(bool)>& setLoading,
    const std::function<void(const QString&)>& setStatus,
    const std::function<void(const QString&)>& debugLog) {
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
            QMessageBox::critical(parent, trPS("Error"), trPS("The 'zip' command line tool is required to save .zip projects but was not found."));
            return false;
        }
    }

    QTemporaryDir tempZipDir;
    QString workingPath;
    if (isZip) {
        if (!tempZipDir.isValid()) {
            QMessageBox::critical(parent, trPS("Error"), trPS("Could not create temporary directory."));
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
    setStatus(trPS("Saving..."));
    QApplication::processEvents();

    QDir destDir(workingPath);
    if (!destDir.exists()) {
        if (!destDir.mkpath(".")) {
            QMessageBox::critical(parent, trPS("Error"), trPS("Could not create destination directory."));
            return false;
        }
    }

    QFile projectFile(destDir.filePath("project.spart.json"));
    if (!projectFile.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(parent, trPS("Error"), trPS("Could not write project.spart.json."));
        return false;
    }
    if (projectFile.write(QJsonDocument(projectPayload).toJson()) < 0) {
        QMessageBox::critical(parent, trPS("Error"), trPS("Failed to write project.spart.json."));
        return false;
    }
    projectFile.close();

    QJsonObject layoutInfo = projectPayload["layout"].toObject();
    const QString cachedLayoutData = layoutInfo["output"].toString();
    const double cachedLayoutScale = layoutInfo["scale"].toDouble(1.0);
    debugLog(QString("Save requested: destination='%1' profile='%2' padding=%3 trim=%4 scales=%5")
                 .arg(config.destination,
                      profile,
                      QString::number(padding),
                      trimTransparent ? "true" : "false",
                      QString::number(config.scales.size())));

    QJsonObject markersInfo = projectPayload["spritemarkers"].toObject();
    QJsonObject spritesState = markersInfo["sprites"].toObject();
    for (auto it = spritesState.begin(); it != spritesState.end(); ++it) {
        QJsonObject spriteState = it.value().toObject();
        QJsonArray markersArr = spriteState["markers"].toArray();
        bool hasPivotMarker = false;
        for (auto markerIt = markersArr.begin(); markerIt != markersArr.end(); ++markerIt) {
            QJsonObject markerObj = markerIt->toObject();
            const QString markerName = normalizedMarkerName(markerObj["name"].toString());
            MarkerKind markerKind = markerKindFromString(markerObj["kind"].toString());
            if (markerObj["kind"].toString().isEmpty()) {
                markerKind = markerKindFromString(markerObj["type"].toString());
            }
            markerObj["kind"] = markerKindToString(markerKind);
            markerObj["type"] = markerKindToString(markerKind);
            markerObj["name"] = markerName;
            *markerIt = markerObj;
            if (markerName == "pivot") {
                hasPivotMarker = true;
            }
        }
        if (!hasPivotMarker) {
            QJsonObject pivotMarker;
            pivotMarker["name"] = "pivot";
            pivotMarker["kind"] = markerKindToString(MarkerKind::Point);
            pivotMarker["type"] = markerKindToString(MarkerKind::Point);
            pivotMarker["x"] = spriteState["pivot_x"].toInt();
            pivotMarker["y"] = spriteState["pivot_y"].toInt();
            markersArr.append(pivotMarker);
            spriteState["markers"] = markersArr;
            it.value() = spriteState;
        }
    }
    markersInfo["sprites"] = spritesState;
    QJsonObject animInfo = projectPayload["animations"].toObject();
    QTemporaryFile markersTemp;
    if (!markersTemp.open()) {
        QMessageBox::critical(parent, trPS("Error"), trPS("Could not create temporary markers file."));
        return false;
    }
    if (markersTemp.write(QJsonDocument(markersInfo).toJson()) < 0 || !markersTemp.flush()) {
        QMessageBox::critical(parent, trPS("Error"), trPS("Could not write temporary markers file."));
        return false;
    }
    markersTemp.close();
    QTemporaryFile animTemp;
    if (!animTemp.open()) {
        QMessageBox::critical(parent, trPS("Error"), trPS("Could not create temporary animation file."));
        return false;
    }
    if (animTemp.write(QJsonDocument(animInfo).toJson()) < 0 || !animTemp.flush()) {
        QMessageBox::critical(parent, trPS("Error"), trPS("Could not write temporary animation file."));
        return false;
    }
    animTemp.close();

    if (config.scales.isEmpty()) {
        QMessageBox::critical(parent, trPS("Error"), trPS("No output scales configured."));
        return false;
    }

    QString layoutPathForSave = layoutInputPath;
    QTemporaryFile saveFrameList;
    if (!framePaths.isEmpty()) {
        saveFrameList.setFileTemplate(QDir::temp().filePath("sprat-gui-save-frames-XXXXXX.txt"));
        if (!saveFrameList.open()) {
            QMessageBox::critical(parent, trPS("Error"), trPS("Could not create temporary frame list for save."));
            return false;
        }
        QTextStream out(&saveFrameList);
        for (const QString& path : framePaths) {
            out << path << "\n";
        }
        out.flush();
        saveFrameList.flush();
        layoutPathForSave = saveFrameList.fileName();
        saveFrameList.close();
    }

    auto readStdErr = [](QProcess& process) {
        return QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    };
    auto runProcess = [&](QProcess& process, const QString& tool, const QStringList& args, const QString& step) -> bool {
        process.start(tool, args);
        if (!process.waitForStarted()) {
            QMessageBox::critical(parent, trPS("Error"), QString(trPS("%1: failed to start '%2'.")).arg(step, tool));
            return false;
        }
        if (!process.waitForFinished(kProcessTimeoutMs)) {
            process.kill();
            process.waitForFinished();
            QMessageBox::critical(parent, trPS("Error"), QString(trPS("%1 timed out.")).arg(step));
            return false;
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            const QString err = readStdErr(process);
            QMessageBox::critical(parent, trPS("Error"), err.isEmpty() ? QString(trPS("%1 failed.")).arg(step)
                                                                        : QString(trPS("%1 failed:\n%2")).arg(step, err));
            return false;
        }
        return true;
    };
    auto dumpLayoutDataForDebug = [&](const QString& scaleName, const QByteArray& data) -> QString {
        QString safeScale = scaleName.isEmpty() ? "unknown" : scaleName;
        safeScale.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
        const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmss-zzz");
        const QString outPath = QDir::temp().filePath(QString("sprat-gui-layout-%1-%2.txt").arg(safeScale, stamp));
        QFile outFile(outPath);
        if (!outFile.open(QIODevice::WriteOnly)) {
            return QString();
        }
        if (outFile.write(data) < 0) {
            return QString();
        }
        outFile.close();
        return outPath;
    };

    constexpr double kScaleMatchTolerance = 1e-6;
    for (const auto& scale : config.scales) {
        QDir scaleDir(destDir.filePath(scale.name));
        if (!scaleDir.exists()) {
            if (!scaleDir.mkpath(".")) {
                QMessageBox::critical(parent, trPS("Error"), QString(trPS("Could not create scale directory: %1")).arg(scale.name));
                return false;
            }
        }

        QByteArray layoutData;
        bool usingCachedLayout = false;
        if (!cachedLayoutData.isEmpty() && std::abs(cachedLayoutScale - scale.value) < kScaleMatchTolerance) {
            layoutData = cachedLayoutData.toUtf8();
            if (layoutData.startsWith("atlas ")) {
                usingCachedLayout = true;
                debugLog(QString("[save:%1] using cached layout (scale=%2, bytes=%3)")
                             .arg(scale.name, QString::number(scale.value), QString::number(layoutData.size())));
            } else {
                layoutData.clear();
            }
        }
        if (!usingCachedLayout) {
            QProcess layoutProc;
            QStringList layoutArgs;
            layoutArgs << layoutPathForSave;
            layoutArgs << "--profile" << profile;
            layoutArgs << "--padding" << QString::number(padding);
            layoutArgs << "--scale" << QString::number(scale.value);
            if (trimTransparent) {
                layoutArgs << "--trim-transparent";
            }
            if (!runProcess(layoutProc, spratLayoutBin, layoutArgs, QString("Layout generation failed for scale '%1'").arg(scale.name))) {
                return false;
            }
            layoutData = layoutProc.readAllStandardOutput();
            debugLog(QString("[save:%1] generated layout with spratlayout (scale=%2, bytes=%3)")
                         .arg(scale.name, QString::number(scale.value), QString::number(layoutData.size())));
            if (!layoutData.startsWith("atlas ")) {
                const QString preview = QString::fromUtf8(layoutData.left(200)).trimmed();
                QMessageBox::critical(parent,
                                      trPS("Error"),
                                      QString(trPS("Layout generation produced invalid output for scale '%1'.\nInput: %2\nOutput preview:\n%3"))
                                          .arg(scale.name, layoutPathForSave, preview.isEmpty() ? QString("<empty>") : preview));
                return false;
            }
        }
        debugLog(QString("[save:%1] layout data begin\n%2\n[save:%1] layout data end")
                     .arg(scale.name, QString::fromUtf8(layoutData)));

        QProcess packProc;
        packProc.start(spratPackBin, QStringList());
        if (!packProc.waitForStarted()) {
            QMessageBox::critical(parent, trPS("Error"), QString(trPS("Packing failed for scale '%1': could not start spratpack.")).arg(scale.name));
            return false;
        }
        packProc.write(layoutData);
        packProc.closeWriteChannel();
        if (!packProc.waitForFinished(kProcessTimeoutMs)) {
            packProc.kill();
            packProc.waitForFinished();
            QMessageBox::critical(parent, trPS("Error"), QString(trPS("Packing timed out for scale '%1'.")).arg(scale.name));
            return false;
        }
        if (packProc.exitStatus() != QProcess::NormalExit || packProc.exitCode() != 0) {
            const QString err = readStdErr(packProc);
            const QString debugPath = dumpLayoutDataForDebug(scale.name, layoutData);
            QString details = err.isEmpty() ? QString(trPS("Packing failed for scale '%1'.")).arg(scale.name)
                                            : QString(trPS("Packing failed for scale '%1':\n%2")).arg(scale.name, err);
            if (!debugPath.isEmpty()) {
                details += QString("\n\nLayout debug dump:\n%1").arg(debugPath);
                debugLog(QString("[save:%1] pack failed; layout dump='%2'").arg(scale.name, debugPath));
            } else {
                details += trPS("\n\nLayout debug dump: failed to write debug file.");
                debugLog(QString("[save:%1] pack failed; layout dump write failed").arg(scale.name));
            }
            QMessageBox::critical(parent, trPS("Error"), details);
            return false;
        }
        QByteArray imageData = packProc.readAllStandardOutput();

        QFile imgFile(scaleDir.filePath("spritesheet.png"));
        if (!imgFile.open(QIODevice::WriteOnly) || imgFile.write(imageData) < 0) {
            QMessageBox::critical(parent, trPS("Error"), QString(trPS("Could not write spritesheet for scale '%1'.")).arg(scale.name));
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
                QMessageBox::critical(parent, trPS("Error"), QString(trPS("Format conversion failed for scale '%1': could not start spratconvert.")).arg(scale.name));
                return false;
            }
            convProc.write(layoutData);
            convProc.closeWriteChannel();
            if (!convProc.waitForFinished(kProcessTimeoutMs)) {
                convProc.kill();
                convProc.waitForFinished();
                QMessageBox::critical(parent, trPS("Error"), QString(trPS("Format conversion timed out for scale '%1'.")).arg(scale.name));
                return false;
            }
            if (convProc.exitStatus() != QProcess::NormalExit || convProc.exitCode() != 0) {
                const QString err = readStdErr(convProc);
                QMessageBox::critical(parent, trPS("Error"), err.isEmpty()
                                                          ? QString(trPS("Format conversion failed for scale '%1'.")).arg(scale.name)
                                                          : QString(trPS("Format conversion failed for scale '%1':\n%2")).arg(scale.name, err));
                return false;
            }
            QByteArray convData = convProc.readAllStandardOutput();
            QString ext = config.transform == "css" ? "css" : (config.transform == "xml" ? "xml" : (config.transform == "csv" ? "csv" : "json"));
            QFile convFile(scaleDir.filePath("layout_formatted." + ext));
            if (!convFile.open(QIODevice::WriteOnly) || convFile.write(convData) < 0) {
                QMessageBox::critical(parent, trPS("Error"), QString(trPS("Could not write converted layout for scale '%1'.")).arg(scale.name));
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
        if (!runProcess(zipProc, zipBin, QStringList() << "-r" << absDest << ".", trPS("Failed to create zip archive"))) {
            return false;
        }
    }

    loadingGuard.active = false;
    setLoading(false);
    savedDestination = config.destination;
    return true;
}
