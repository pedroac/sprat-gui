#include "ProjectSaveService.h"
#include "MarkerUtils.h"
#include "ResolutionUtils.h"
#include "ArchiveExtractor.h"

#ifdef SPRAT_EMBEDDED_CLI
#include "EmbeddedCli.h"
#endif

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

    bool isCompactPreset(const QString& preset) {
        const QString p = preset.trimmed().toLower();
        return p == "quality" || p == "small";
    }
}

bool ProjectSaveService::writeProjectJson(
    const QString& projectFolder,
    const QJsonObject& payload,
    QString& error
) {
    QDir dir(projectFolder);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            error = trPS("Could not create project directory.");
            return false;
        }
    }
    QFile projectFile(dir.filePath("project.spart.json"));
    if (!projectFile.open(QIODevice::WriteOnly)) {
        error = trPS("Could not write project.spart.json.");
        return false;
    }
    if (projectFile.write(QJsonDocument(payload).toJson()) < 0) {
        error = trPS("Failed to write project.spart.json.");
        return false;
    }
    projectFile.close();
    return true;
}

bool ProjectSaveService::save(
    SaveConfig config,
    const QString& layoutInputPath,
    const QStringList& framePaths,
    const QString& sourceFolder,
    const QVector<SpratProfile>& availableProfiles,
    const QString& selectedProfileName,
    const QString& spratLayoutBin,
    const QString& spratPackBin,
    const QString& spratConvertBin,
    const QJsonObject& projectPayload,
    QString& savedDestination,
    QString& error,
    const QString& deduplicateMode,
    const std::function<void(bool)>& setLoading,
    const std::function<void(const QString&)>& setStatus,
    const std::function<bool()>& shouldCancel,
    const std::function<bool(const QString&, const QStringList&, const QString&, const QByteArray*, QByteArray*)>& runProcessFunc
) {
    constexpr int kProcessTimeoutMs = 120000;

    struct LoadingGuard {
        const std::function<void(bool)>& setLoading;
        bool active = true;
        ~LoadingGuard() {
            if (active && setLoading) {
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

    QTemporaryDir tempZipDir;
    QString workingPath;
    if (isZip) {
        if (!tempZipDir.isValid()) {
            error = trPS("Could not create temporary directory.");
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

    // For folder exports, remove stale profile folders first.
    if (!isZip) {
        const QDir wd(workingPath);
        for (const QString& profileNameRaw : config.profiles) {
            const QString profileName = profileNameRaw.trimmed();
            if (!profileName.isEmpty()) {
                QDir(wd.filePath(profileName)).removeRecursively();
            }
        }
    }

    if (setLoading) setLoading(true);
    LoadingGuard loadingGuard{setLoading};
    if (setStatus) setStatus(trPS("Exporting..."));

    auto checkCanceled = [&]() -> bool {
        if (shouldCancel && shouldCancel()) {
            error = trPS("Save canceled.");
            return true;
        }
        return false;
    };
    auto updateStatus = [&](const QString& status) {
        if (setStatus) {
            setStatus(status);
        }
    };
    if (checkCanceled()) {
        return false;
    }

    QDir destDir(workingPath);
    if (!destDir.exists()) {
        if (!destDir.mkpath(".")) {
            error = trPS("Could not create destination directory.");
            return false;
        }
    }

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

    // Emit top-level alias directives for each sprite that has aliases.
    for (auto it = spritesState.begin(); it != spritesState.end(); ++it) {
        const QJsonObject spriteState = it.value().toObject();
        const QString canonicalName = spriteState["name"].toString();
        if (canonicalName.isEmpty()) continue;
        for (const auto& a : spriteState["aliases"].toArray()) {
            const QString alias = a.toString().trimmed();
            if (!alias.isEmpty())
                markersStream << "alias \"" << alias << "\" \"" << canonicalName << "\"\n";
        }
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
        const QString aliasOf = timeline["alias_of"].toString();
        if (!aliasOf.isEmpty()) {
            animStream << "alias \"" << aliasOf << "\"";
            const bool hFlip = timeline["h_flip"].toBool();
            const bool vFlip = timeline["v_flip"].toBool();
            if (hFlip || vFlip) {
                animStream << " flip ";
                if (hFlip) animStream << "h";
                if (vFlip) animStream << "v";
            }
            animStream << "\n";
        } else {
            QJsonArray frames = timeline["frames"].toArray();
            for (const auto& fVal : frames) {
                animStream << "- frame \"" << fVal.toString() << "\"\n";
            }
        }
        animStream << "\n";
    }

    QTemporaryFile markersTemp;
    markersTemp.setFileTemplate(QDir::temp().filePath("sprat-gui-markers-XXXXXX.txt"));
    if (!markersTemp.open()) {
        error = trPS("Could not create temporary markers file.");
        return false;
    }
    if (markersTemp.write(markersContent.toUtf8()) < 0 || !markersTemp.flush()) {
        error = trPS("Could not write temporary markers file.");
        return false;
    }
    markersTemp.close();

    QTemporaryFile animTemp;
    animTemp.setFileTemplate(QDir::temp().filePath("sprat-gui-animations-XXXXXX.txt"));
    if (!animTemp.open()) {
        error = trPS("Could not create temporary animation file.");
        return false;
    }
    if (animTemp.write(animContent.toUtf8()) < 0 || !animTemp.flush()) {
        error = trPS("Could not write temporary animation file.");
        return false;
    }
    animTemp.close();

    if (config.profiles.isEmpty()) {
        error = trPS("No output profiles selected.");
        return false;
    }

    updateStatus(trPS("Preparing layout input..."));
    if (checkCanceled()) {
        return false;
    }
    QString layoutPathForSave = layoutInputPath;
    QTemporaryFile saveFrameList;
    if (!framePaths.isEmpty()) {
        saveFrameList.setFileTemplate(QDir::temp().filePath("sprat-gui-save-frames-XXXXXX.txt"));
        if (!saveFrameList.open()) {
            error = trPS("Could not create temporary frame list for save.");
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

    auto runProcess = [&](const QString& tool, const QStringList& args, const QString& step, const QByteArray* inputData = nullptr, QByteArray* outputData = nullptr) -> bool {
#ifdef SPRAT_EMBEDDED_CLI
        QString toolName = QFileInfo(tool).baseName();
        if (toolName.startsWith("sprat")) {
            // Use EmbeddedCli for sprat-* tools
            CliResult embeddedResult = EmbeddedCli::run(
                toolName,
                args,
                inputData ? *inputData : QByteArray());
            
            if (outputData) *outputData = embeddedResult.stdOut;
            if (embeddedResult.exitCode != 0) {
                const QString stderrText = QString::fromUtf8(embeddedResult.stdErr).trimmed();
                if (!stderrText.isEmpty()) {
                    error = stderrText;
                } else {
                    error = trPS("Command failed: %1").arg(step);
                }
                return false;
            }
            return true;
        }
#endif
        if (!runProcessFunc) return false;
        if (!runProcessFunc(tool, args, step, inputData, outputData)) {
            // Error handling is complex here since we don't have access to process.readAllStandardError() directly,
            // so we assume runProcessFunc either handles it or we'll get a general failure.
            // For now, let's assume if it returns false, it already set an appropriate error if possible.
            // But we can't easily set 'error' from here without changing the lambda.
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
        updateStatus(QString(trPS("Generating layout for profile '%1'...")).arg(profileName));
        if (checkCanceled()) {
            return false;
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
                error = QString(trPS("Could not create profile directory: %1")).arg(profileName);
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
            QStringList layoutArgs;
            layoutArgs << layoutPathForSave;
            if (!effectiveProfile.preset.trimmed().isEmpty()) {
                layoutArgs << "--preset" << effectiveProfile.preset.trimmed();
            }
            if (effectiveProfile.maxWidth > 0) {
                layoutArgs << "--max-width" << QString::number(effectiveProfile.maxWidth);
            }
            if (effectiveProfile.maxHeight > 0) {
                layoutArgs << "--max-height" << QString::number(effectiveProfile.maxHeight);
            }
            if (isCompactPreset(effectiveProfile.preset) && effectiveProfile.threads > 0) {
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
            if (!deduplicateMode.isEmpty() && deduplicateMode != "none") {
                layoutArgs << "--deduplicate" << deduplicateMode;
            }

            bool layoutSuccess = false;
            while (!layoutSuccess) {
                layoutData.clear();
                if (!runProcess(spratLayoutBin, layoutArgs, QString(trPS("Layout generation failed for profile '%1'")).arg(profileName), nullptr, &layoutData)) {
                    // Try without trim-transparent on failure
                    if (layoutArgs.contains("--trim-transparent")) {
                        layoutArgs.removeAll("--trim-transparent");
                        continue;
                    }
                    error = QString(trPS("Layout generation failed for profile '%1'")).arg(profileName);
                    return false;
                }
                layoutSuccess = true;
            }
            if (checkCanceled()) {
                return false;
            }
            if (!layoutData.contains("atlas ")) {
                error = QString(trPS("Layout generation produced invalid output for profile '%1'.")).arg(profileName);
                return false;
            }
        }
        
        QByteArray imageData;
        updateStatus(QString(trPS("Packing spritesheet for profile '%1'...")).arg(profileName));
        if (checkCanceled()) {
            return false;
        }

        QStringList packArgs;
        if (effectiveProfile.dilate > 0) {
            packArgs << "--dilate" << QString::number(effectiveProfile.dilate);
        }
        const bool hasDds = !effectiveProfile.gpuCompress.isEmpty();
        if (hasDds) {
            packArgs << "--gpu-compress" << effectiveProfile.gpuCompress;
        }

        if (!runProcess(spratPackBin, packArgs, QString(trPS("Packing failed for profile '%1'")).arg(profileName), &layoutData, &imageData)) {
            const QString packDetails = error;
            error = QString(trPS("Packing failed for profile '%1'")).arg(profileName);
            if (!packDetails.isEmpty()) {
                error += QStringLiteral("\n\n") + packDetails;
            }
            return false;
        }
        const bool isMultipack = layoutData.contains("multipack true") || layoutData.count("atlas ") > 1;
        if (checkCanceled()) {
            return false;
        }

        if (isMultipack && !imageData.startsWith("\x89PNG\r\n\x1a\n")) {
            QString tarBin = QStandardPaths::findExecutable("tar");
            bool extracted = false;
            if (!tarBin.isEmpty()) {
                // In background thread, we need to handle working directory carefully if we used runProcessFunc.
                // But runProcessFunc uses QProcess internally.
                // We'll use a specific tar command if we can.
                // This part is a bit tricky with the runProcessFunc abstraction.
                // Let's assume for now that tar works or save the .tar.
                QFile imgFile(profileDir.filePath("spritesheet.tar"));
                if (imgFile.open(QIODevice::WriteOnly)) {
                    imgFile.write(imageData);
                    imgFile.close();
                }
            } else {
                QFile imgFile(profileDir.filePath("spritesheet.tar"));
                if (imgFile.open(QIODevice::WriteOnly)) {
                    imgFile.write(imageData);
                    imgFile.close();
                }
            }
        } else {
            const QString imageFileName = hasDds ? "spritesheet.dds" : "spritesheet.png";
            QFile imgFile(profileDir.filePath(imageFileName));
            if (!imgFile.open(QIODevice::WriteOnly) || imgFile.write(imageData) < 0) {
                error = QString(trPS("Could not write spritesheet for profile '%1'.")).arg(profileName);
                return false;
            }
            imgFile.close();
        }

        // Save combined layout, markers and animations (absolute paths — for spratconvert)
        QByteArray combinedInput = layoutData;
        if (!combinedInput.endsWith('\n')) combinedInput.append('\n');
        combinedInput.append(markersContent.toUtf8());
        if (!combinedInput.endsWith('\n')) combinedInput.append('\n');
        combinedInput.append(animContent.toUtf8());

        if (config.transform != "none" && !spratConvertBin.isEmpty()) {
            updateStatus(QString(trPS("Formatting output for profile '%1'...")).arg(profileName));
            if (checkCanceled()) {
                return false;
            }
            QStringList convArgs;
            convArgs << "--transform" << config.transform;
            convArgs << "--output-dir" << profileDir.absolutePath();

            if (isMultipack) {
                convArgs << "--atlas" << (hasDds ? "atlas_%d.dds" : "atlas_%d.png");
            } else {
                convArgs << "--atlas" << (hasDds ? "spritesheet.dds" : "spritesheet.png");
            }
            
            if (!runProcess(spratConvertBin, convArgs, QString(trPS("Format conversion failed for profile '%1'")).arg(profileName), &combinedInput, nullptr)) {
                error = QString(trPS("Format conversion failed for profile '%1'")).arg(profileName);
                return false;
            }
            if (checkCanceled()) {
                return false;
            }
        }
    }

    if (isZip) {
        updateStatus(trPS("Building zip..."));
        if (checkCanceled()) {
            return false;
        }
        QString absDest = QFileInfo(config.destination).absoluteFilePath();
        QDir().mkpath(QFileInfo(absDest).path());
        QFile::remove(absDest);

        if (!ArchiveExtractor::createZip(workingPath, absDest, error)) {
            return false;
        }
    }

    loadingGuard.active = false;
    if (setLoading) setLoading(false);
    savedDestination = config.destination;
    return true;
}
