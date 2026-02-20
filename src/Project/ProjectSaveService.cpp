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
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextStream>
#include <QStringList>
#include <QtGlobal>
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

bool isCompactMode(const QString& mode) {
    return mode.trimmed().compare("compact", Qt::CaseInsensitive) == 0;
}

QString resolutionArg(int width, int height) {
    return QString("%1x%2").arg(width).arg(height);
}

bool parseResolution(const QString& value, int& width, int& height) {
    const QStringList parts = value.trimmed().toLower().split('x', Qt::SkipEmptyParts);
    if (parts.size() != 2) {
        return false;
    }
    bool okW = false;
    bool okH = false;
    const int parsedWidth = parts[0].trimmed().toInt(&okW);
    const int parsedHeight = parts[1].trimmed().toInt(&okH);
    if (!okW || !okH || parsedWidth <= 0 || parsedHeight <= 0) {
        return false;
    }
    width = parsedWidth;
    height = parsedHeight;
    return true;
}
}

bool ProjectSaveService::save(
    QWidget* parent,
    SaveConfig config,
    const QString& layoutInputPath,
    const QStringList& framePaths,
    const QVector<SpratProfile>& availableProfiles,
    const QString& selectedProfileName,
    const QString& spratLayoutBin,
    const QString& spratPackBin,
    const QString& spratConvertBin,
    const QJsonObject& projectPayload,
    QString& savedDestination,
    const std::function<void(bool)>& setLoading,
    const std::function<void(const QString&)>& setStatus,
    const std::function<void(const QString&)>& debugLog) {
    Q_UNUSED(debugLog);
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
    const QJsonObject layoutOptions = projectPayload["layout_options"].toObject();
    const QString cachedLayoutData = layoutInfo["output"].toString();
    const double cachedLayoutScale = layoutInfo["scale"].toDouble(1.0);
    const double layoutOptionScale = [] (double value) {
        return (value > 0.0 && value <= 1.0) ? value : 1.0;
    }(layoutOptions["scale"].toDouble(1.0));
    int sourceResolutionWidth = 0;
    int sourceResolutionHeight = 0;
    const bool hasSourceResolution = parseResolution(
        layoutOptions["source_resolution"].toString(),
        sourceResolutionWidth,
        sourceResolutionHeight);

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
    const int animationFps = animInfo["animation_fps"].toInt(8);
    QJsonArray timelines = animInfo["timelines"].toArray();
    for (int i = 0; i < timelines.size(); ++i) {
        QJsonObject timeline = timelines[i].toObject();
        int timelineFps = timeline["fps"].toInt(animationFps);
        if (timelineFps <= 0) {
            timelineFps = 8;
        }
        timeline["fps"] = timelineFps;
        timelines[i] = timeline;
    }
    animInfo["timelines"] = timelines;
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

    if (config.profiles.isEmpty()) {
        QMessageBox::critical(parent, trPS("Error"), trPS("No output profiles selected."));
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

    auto findProfile = [&](const QString& profileName) -> const SpratProfile* {
        for (const SpratProfile& profile : availableProfiles) {
            if (profile.name.trimmed() == profileName) {
                return &profile;
            }
        }
        return nullptr;
    };

    constexpr double kScaleMatchTolerance = 1e-6;
    for (const QString& profileNameRaw : config.profiles) {
        const QString profileName = profileNameRaw.trimmed();
        if (profileName.isEmpty()) {
            continue;
        }

        SpratProfile effectiveProfile;
        if (const SpratProfile* found = findProfile(profileName)) {
            effectiveProfile = *found;
        } else {
            effectiveProfile.name = profileName;
            effectiveProfile.padding = 0;
            effectiveProfile.trimTransparent = false;
        }
        const int profilePadding = qMax(0, effectiveProfile.padding);
        const bool profileTrimTransparent = effectiveProfile.trimTransparent;

        QDir profileDir(destDir.filePath(profileName));
        if (!profileDir.exists()) {
            if (!profileDir.mkpath(".")) {
                QMessageBox::critical(parent, trPS("Error"), QString(trPS("Could not create profile directory: %1")).arg(profileName));
                return false;
            }
        }

        QByteArray layoutData;
        bool usingCachedLayout = false;
        if (!cachedLayoutData.isEmpty() &&
            profileName == selectedProfileName &&
            std::abs(cachedLayoutScale - layoutOptionScale) < kScaleMatchTolerance) {
            layoutData = cachedLayoutData.toUtf8();
            if (layoutData.startsWith("atlas ")) {
                usingCachedLayout = true;
            } else {
                layoutData.clear();
            }
        }
        if (!usingCachedLayout) {
            QProcess layoutProc;
            QStringList layoutArgs;
            layoutArgs << layoutPathForSave;
            if (!effectiveProfile.mode.trimmed().isEmpty()) {
                layoutArgs << "--mode" << effectiveProfile.mode.trimmed();
            }
            if (!effectiveProfile.optimize.trimmed().isEmpty()) {
                layoutArgs << "--optimize" << effectiveProfile.optimize.trimmed();
            }
            if (effectiveProfile.maxWidth > 0) {
                layoutArgs << "--max-width" << QString::number(effectiveProfile.maxWidth);
            }
            if (effectiveProfile.maxHeight > 0) {
                layoutArgs << "--max-height" << QString::number(effectiveProfile.maxHeight);
            }
            if (isCompactMode(effectiveProfile.mode) && effectiveProfile.maxCombinations > 0) {
                layoutArgs << "--max-combinations" << QString::number(effectiveProfile.maxCombinations);
            }
            if (isCompactMode(effectiveProfile.mode) && effectiveProfile.threads > 0) {
                layoutArgs << "--threads" << QString::number(effectiveProfile.threads);
            }
            const bool hasTargetResolution = effectiveProfile.targetResolutionUseSource ||
                (effectiveProfile.targetResolutionWidth > 0 && effectiveProfile.targetResolutionHeight > 0);
            if (hasSourceResolution && hasTargetResolution) {
                const int targetResolutionWidth = effectiveProfile.targetResolutionUseSource
                    ? sourceResolutionWidth
                    : effectiveProfile.targetResolutionWidth;
                const int targetResolutionHeight = effectiveProfile.targetResolutionUseSource
                    ? sourceResolutionHeight
                    : effectiveProfile.targetResolutionHeight;
                layoutArgs << "--source-resolution" << resolutionArg(sourceResolutionWidth, sourceResolutionHeight);
                layoutArgs << "--target-resolution" << resolutionArg(targetResolutionWidth, targetResolutionHeight);
                if (!effectiveProfile.resolutionReference.trimmed().isEmpty()) {
                    layoutArgs << "--resolution-reference" << effectiveProfile.resolutionReference.trimmed();
                }
            }
            layoutArgs << "--padding" << QString::number(profilePadding);
            layoutArgs << "--scale" << QString::number(layoutOptionScale);
            if (profileTrimTransparent) {
                layoutArgs << "--trim-transparent";
            }
            if (!runProcess(layoutProc, spratLayoutBin, layoutArgs, QString("Layout generation failed for profile '%1'").arg(profileName))) {
                return false;
            }
            layoutData = layoutProc.readAllStandardOutput();
            if (!layoutData.startsWith("atlas ")) {
                const QString preview = QString::fromUtf8(layoutData.left(200)).trimmed();
                QMessageBox::critical(parent,
                                      trPS("Error"),
                                      QString(trPS("Layout generation produced invalid output for profile '%1'.\nInput: %2\nOutput preview:\n%3"))
                                          .arg(profileName, layoutPathForSave, preview.isEmpty() ? QString("<empty>") : preview));
                return false;
            }
        }
        QProcess packProc;
        packProc.start(spratPackBin, QStringList());
        if (!packProc.waitForStarted()) {
            QMessageBox::critical(parent, trPS("Error"), QString(trPS("Packing failed for profile '%1': could not start spratpack.")).arg(profileName));
            return false;
        }
        packProc.write(layoutData);
        packProc.closeWriteChannel();
        if (!packProc.waitForFinished(kProcessTimeoutMs)) {
            packProc.kill();
            packProc.waitForFinished();
            QMessageBox::critical(parent, trPS("Error"), QString(trPS("Packing timed out for profile '%1'.")).arg(profileName));
            return false;
        }
        if (packProc.exitStatus() != QProcess::NormalExit || packProc.exitCode() != 0) {
            const QString err = readStdErr(packProc);
            QString details = err.isEmpty() ? QString(trPS("Packing failed for profile '%1'.")).arg(profileName)
                                            : QString(trPS("Packing failed for profile '%1':\n%2")).arg(profileName, err);
            QMessageBox::critical(parent, trPS("Error"), details);
            return false;
        }
        QByteArray imageData = packProc.readAllStandardOutput();

        QFile imgFile(profileDir.filePath("spritesheet.png"));
        if (!imgFile.open(QIODevice::WriteOnly) || imgFile.write(imageData) < 0) {
            QMessageBox::critical(parent, trPS("Error"), QString(trPS("Could not write spritesheet for profile '%1'.")).arg(profileName));
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
                QMessageBox::critical(parent, trPS("Error"), QString(trPS("Format conversion failed for profile '%1': could not start spratconvert.")).arg(profileName));
                return false;
            }
            convProc.write(layoutData);
            convProc.closeWriteChannel();
            if (!convProc.waitForFinished(kProcessTimeoutMs)) {
                convProc.kill();
                convProc.waitForFinished();
                QMessageBox::critical(parent, trPS("Error"), QString(trPS("Format conversion timed out for profile '%1'.")).arg(profileName));
                return false;
            }
            if (convProc.exitStatus() != QProcess::NormalExit || convProc.exitCode() != 0) {
                const QString err = readStdErr(convProc);
                QMessageBox::critical(parent, trPS("Error"), err.isEmpty()
                                                          ? QString(trPS("Format conversion failed for profile '%1'.")).arg(profileName)
                                                          : QString(trPS("Format conversion failed for profile '%1':\n%2")).arg(profileName, err));
                return false;
            }
            QByteArray convData = convProc.readAllStandardOutput();
            QString ext = config.transform == "css" ? "css" : (config.transform == "xml" ? "xml" : (config.transform == "csv" ? "csv" : "json"));
            QFile convFile(profileDir.filePath("layout_formatted." + ext));
            if (!convFile.open(QIODevice::WriteOnly) || convFile.write(convData) < 0) {
                QMessageBox::critical(parent, trPS("Error"), QString(trPS("Could not write converted layout for profile '%1'.")).arg(profileName));
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
