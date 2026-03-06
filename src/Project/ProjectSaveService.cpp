#include "ProjectSaveService.h"
#include "MarkerUtils.h"
#include "ResolutionUtils.h"

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
#include <QtGlobal>

namespace {
    QString trPS(const char* text) {
        return QCoreApplication::translate("ProjectSaveService", text);
    }

    bool isCompactMode(const QString& mode) {
        return mode.trimmed().compare("compact", Qt::CaseInsensitive) == 0;
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
    const std::function<void(const QString&)>& setStatus
) {
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
    bool usePowerShell = false;
    if (isZip) {
        zipBin = QStandardPaths::findExecutable("zip");
        if (zipBin.isEmpty()) {
#ifdef Q_OS_WIN
            zipBin = QStandardPaths::findExecutable("powershell");
            if (!zipBin.isEmpty()) {
                usePowerShell = true;
            } else {
                QMessageBox::critical(parent, trPS("Error"), trPS("Neither 'zip' nor 'powershell' was found. Cannot create zip archive."));
                return false;
            }
#else
            QMessageBox::critical(parent, trPS("Error"), trPS("The 'zip' command line tool is required to save .zip projects but was not found."));
            return false;
#endif
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
    const double layoutOptionScale = 1.0;
    int sourceResolutionWidth = 0;
    int sourceResolutionHeight = 0;
    const bool hasSourceResolution = parseResolutionText(
        layoutOptions["source_resolution"].toString(),
        sourceResolutionWidth,
        sourceResolutionHeight);

    QJsonObject markersInfo = projectPayload["spritemarkers"].toObject();
    QJsonObject spritesState = markersInfo["sprites"].toObject();
    
    QString markersContent;
    QTextStream markersStream(&markersContent);

    for (auto it = spritesState.begin(); it != spritesState.end(); ++it) {
        QJsonObject spriteState = it.value().toObject();
        QJsonArray markersArr = spriteState["markers"].toArray();
        bool hasPivotMarker = false;
        
        QString spritePath = it.key();
        markersStream << "path \"" << spritePath << "\"\n";

        for (auto markerIt = markersArr.begin(); markerIt != markersArr.end(); ++markerIt) {
            QJsonObject markerObj = markerIt->toObject();
            const QString markerName = normalizeMarkerName(markerObj["name"].toString());
            QString markerKindStr = markerObj["kind"].toString();
            if (markerKindStr.isEmpty()) {
                markerKindStr = markerObj["type"].toString();
            }
            if (markerKindStr.isEmpty()) {
                markerKindStr = "point";
            }
            
            markersStream << "- marker \"" << markerName << "\" " << markerKindStr;
            
            if (markerKindStr == "point") {
                markersStream << " " << markerObj["x"].toInt() << "," << markerObj["y"].toInt();
            } else if (markerKindStr == "circle") {
                markersStream << " " << markerObj["x"].toInt() << "," << markerObj["y"].toInt() 
                              << " " << markerObj["radius"].toInt();
            } else if (markerKindStr == "rectangle") {
                markersStream << " " << markerObj["x"].toInt() << "," << markerObj["y"].toInt() 
                              << " " << markerObj["w"].toInt() << "," << markerObj["h"].toInt();
            } else if (markerKindStr == "polygon") {
                QJsonArray vertices = markerObj["vertices"].toArray();
                if (vertices.isEmpty()) {
                    vertices = markerObj["polygon_points"].toArray(); // Legacy fallback
                }
                for (const auto& vVal : vertices) {
                    if (vVal.isArray()) {
                        QJsonArray vArr = vVal.toArray();
                        if (vArr.size() >= 2) {
                            markersStream << " " << vArr[0].toInt() << "," << vArr[1].toInt();
                        }
                    } else {
                        QJsonObject vObj = vVal.toObject();
                        markersStream << " " << vObj["x"].toInt() << "," << vObj["y"].toInt();
                    }
                }
            }
            markersStream << "\n";

            if (markerName == "pivot") {
                hasPivotMarker = true;
            }
        }
        
        if (!hasPivotMarker) {
            markersStream << "- marker \"pivot\" point " 
                          << spriteState["pivot_x"].toInt() << "," << spriteState["pivot_y"].toInt() << "\n";
        }
        markersStream << "\n";
    }

    QJsonObject animInfo = projectPayload["animations"].toObject();
    const int animationFps = animInfo["animation_fps"].toInt(8);
    QJsonArray timelines = animInfo["timelines"].toArray();
    
    QString animContent;
    QTextStream animStream(&animContent);
    animStream << "fps " << animationFps << "\n\n";

    for (int i = 0; i < timelines.size(); ++i) {
        QJsonObject timeline = timelines[i].toObject();
        int timelineFps = timeline["fps"].toInt(animationFps);
        if (timelineFps <= 0) {
            timelineFps = 8;
        }
        
        animStream << "animation \"" << timeline["name"].toString() << "\" " << timelineFps << "\n";
        QJsonArray frames = timeline["frames"].toArray();
        for (const auto& fVal : frames) {
            animStream << "- frame \"" << fVal.toString() << "\"\n";
        }
        animStream << "\n";
    }

    QTemporaryFile markersTemp;
    markersTemp.setFileTemplate(QDir::temp().filePath("sprat-gui-markers-XXXXXX.txt"));
    if (!markersTemp.open()) {
        QMessageBox::critical(parent, trPS("Error"), trPS("Could not create temporary markers file."));
        return false;
    }
    if (markersTemp.write(markersContent.toUtf8()) < 0 || !markersTemp.flush()) {
        QMessageBox::critical(parent, trPS("Error"), trPS("Could not write temporary markers file."));
        return false;
    }
    markersTemp.close();

    QTemporaryFile animTemp;
    animTemp.setFileTemplate(QDir::temp().filePath("sprat-gui-animations-XXXXXX.txt"));
    if (!animTemp.open()) {
        QMessageBox::critical(parent, trPS("Error"), trPS("Could not create temporary animation file."));
        return false;
    }
    if (animTemp.write(animContent.toUtf8()) < 0 || !animTemp.flush()) {
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
        const int profileExtrude = qMax(0, effectiveProfile.extrude);
        const double profileScale = qBound(0.01, effectiveProfile.scale, 1.0);
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
            std::abs(cachedLayoutScale - profileScale) < kScaleMatchTolerance) {
            layoutData = cachedLayoutData.toUtf8();
            if (layoutData.contains("atlas ")) {
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
                layoutArgs << "--source-resolution" << formatResolutionText(sourceResolutionWidth, sourceResolutionHeight);
                layoutArgs << "--target-resolution" << formatResolutionText(targetResolutionWidth, targetResolutionHeight);
                if (!effectiveProfile.resolutionReference.trimmed().isEmpty()) {
                    layoutArgs << "--resolution-reference" << effectiveProfile.resolutionReference.trimmed();
                }
            }
            layoutArgs << "--padding" << QString::number(profilePadding);
            if (profileExtrude > 0) {
                layoutArgs << "--extrude" << QString::number(profileExtrude);
            }
            layoutArgs << "--scale" << QString::number(profileScale);
            if (profileTrimTransparent) {
                layoutArgs << "--trim-transparent";
            }
            if (effectiveProfile.allowRotation) {
                layoutArgs << "--rotate";
            }
            if (effectiveProfile.multipack) {
                layoutArgs << "--multipack";
            }
            if (!effectiveProfile.sort.trimmed().isEmpty()) {
                layoutArgs << "--sort" << effectiveProfile.sort.trimmed();
            }

            bool layoutSuccess = false;
            while (!layoutSuccess) {
                if (layoutProc.state() != QProcess::NotRunning) {
                    layoutProc.kill();
                    layoutProc.waitForFinished();
                }
                layoutProc.start(spratLayoutBin, layoutArgs);
                if (!layoutProc.waitForStarted()) {
                    QMessageBox::critical(parent, trPS("Error"), QString(trPS("Layout generation failed for profile '%1': could not start spratlayout.")).arg(profileName));
                    return false;
                }
                if (!layoutProc.waitForFinished(kProcessTimeoutMs)) {
                    layoutProc.kill();
                    layoutProc.waitForFinished();
                    QMessageBox::critical(parent, trPS("Error"), QString(trPS("Layout generation timed out for profile '%1'.")).arg(profileName));
                    return false;
                }
                if (layoutProc.exitStatus() != QProcess::NormalExit || layoutProc.exitCode() != 0) {
                    const QString err = readStdErr(layoutProc);
                    const QString combined = (err + "\n" + QString::fromUtf8(layoutProc.readAllStandardOutput())).toLower();
                    if (layoutArgs.contains("--trim-transparent") && combined.contains("failed to compute compact layout")) {
                        layoutArgs.removeAll("--trim-transparent");
                        continue;
                    }
                    QMessageBox::critical(parent, trPS("Error"), err.isEmpty()
                                                              ? QString(trPS("Layout generation failed for profile '%1'.")).arg(profileName)
                                                              : QString(trPS("Layout generation failed for profile '%1':\n%2")).arg(profileName, err));
                    return false;
                }
                layoutSuccess = true;
            }
            layoutData = layoutProc.readAllStandardOutput();
            if (!layoutData.contains("atlas ")) {
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
        const bool isMultipack = layoutData.contains("multipack true") || layoutData.count("atlas ") > 1;

        if (isMultipack && !imageData.startsWith("\x89PNG\r\n\x1a\n")) {
            QString tarBin = QStandardPaths::findExecutable("tar");
            bool extracted = false;
            if (!tarBin.isEmpty()) {
                QProcess tarProc;
                tarProc.setWorkingDirectory(profileDir.absolutePath());
                tarProc.start(tarBin, QStringList() << "-xf" << "-");
                if (tarProc.waitForStarted()) {
                    tarProc.write(imageData);
                    tarProc.closeWriteChannel();
                    if (tarProc.waitForFinished(30000) && tarProc.exitStatus() == QProcess::NormalExit && tarProc.exitCode() == 0) {
                        extracted = true;
                    }
                }
            }
            if (extracted) {
                QFile::remove(profileDir.filePath("spritesheet.png"));
            } else {
                // If tar failed or was missing, save the tar as .tar instead of .png to at least not lose it
                QFile imgFile(profileDir.filePath("spritesheet.tar"));
                if (imgFile.open(QIODevice::WriteOnly)) {
                    imgFile.write(imageData);
                    imgFile.close();
                }
                QFile::remove(profileDir.filePath("spritesheet.png"));
                if (tarBin.isEmpty()) {
                    QMessageBox::warning(parent, trPS("Warning"), trPS("The 'tar' command line tool is required to extract multipack atlases but was not found. The archive was saved as 'spritesheet.tar'."));
                } else {
                    QMessageBox::warning(parent, trPS("Warning"), trPS("Failed to extract multipack atlases. The archive was saved as 'spritesheet.tar'."));
                }
            }
        } else {
            QFile imgFile(profileDir.filePath("spritesheet.png"));
            if (!imgFile.open(QIODevice::WriteOnly) || imgFile.write(imageData) < 0) {
                QMessageBox::critical(parent, trPS("Error"), QString(trPS("Could not write spritesheet for profile '%1'.")).arg(profileName));
                return false;
            }
            imgFile.close();
        }

        // Save combined layout, markers and animations to a file that can be used for future transformations
        QByteArray combinedInput = layoutData;
        if (!combinedInput.endsWith('\n')) combinedInput.append('\n');
        combinedInput.append(markersContent.toUtf8());
        if (!combinedInput.endsWith('\n')) combinedInput.append('\n');
        combinedInput.append(animContent.toUtf8());

        QFile layoutRawFile(profileDir.filePath("layout_raw.txt"));
        if (layoutRawFile.open(QIODevice::WriteOnly)) {
            layoutRawFile.write(combinedInput);
            layoutRawFile.close();
        }

        if (config.transform != "none" && !spratConvertBin.isEmpty()) {
            QProcess convProc;
            QStringList convArgs;
            convArgs << "--transform" << config.transform;
            if (isMultipack) {
                convArgs << "--output" << "atlas_%d.png";
            } else {
                convArgs << "--output" << "spritesheet.png";
            }
            // No need for --markers and --animations as they are now in stdin
            convProc.start(spratConvertBin, convArgs);
            if (!convProc.waitForStarted()) {
                QMessageBox::critical(parent, trPS("Error"), QString(trPS("Format conversion failed for profile '%1': could not start spratconvert.")).arg(profileName));
                return false;
            }
            convProc.write(combinedInput);
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

        if (usePowerShell) {
            QString script = QString("Compress-Archive -Path * -DestinationPath '%1' -Force").arg(absDest);
            if (!runProcess(zipProc, zipBin, QStringList() << "-Command" << script, trPS("Failed to create zip archive via PowerShell"))) {
                return false;
            }
        } else {
            if (!runProcess(zipProc, zipBin, QStringList() << "-r" << absDest << ".", trPS("Failed to create zip archive"))) {
                return false;
            }
        }
    }

    loadingGuard.active = false;
    setLoading(false);
    savedDestination = config.destination;
    return true;
}
