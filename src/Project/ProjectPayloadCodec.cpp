#include "ProjectPayloadCodec.h"
#include "AnimationModels.h"
#include "MarkerUtils.h"
#include "NineSliceInsets.h"
#include "PathSafety.h"
#include "PuppetEditing.h"
#include "ResolutionUtils.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QStandardPaths>
#include <QColor>
#include <QUuid>

#include <functional>
#include <optional>

namespace {
QJsonArray toJsonArray(const QVector<int>& values) {
    QJsonArray arr;
    for (int value : values) {
        arr.append(value);
    }
    return arr;
}

QVector<int> fromJsonArray(const QJsonValue& value) {
    QVector<int> out;
    const QJsonArray arr = value.toArray();
    out.reserve(arr.size());
    for (const auto& entry : arr) {
        out.append(entry.toInt());
    }
    return out;
}

// Before schema 5, "raw" named the built-in layout-text export; since then it is
// sprat-cli's raw.jsonnet transform.
QString readTransform(const QJsonValue& value, int schemaVersion) {
    const QString transform = value.toString();
    if (schemaVersion < 5 && transform == QLatin1String("raw")) {
        return kLayoutTextTransform;
    }
    return transform;
}

QJsonObject timelineToJson(const AnimationTimeline& t) {
    QJsonObject tObj;
    tObj["name"] = t.name;
    tObj["fps"] = t.fps;
    QJsonArray framesArr;
    for (const auto& f : t.frames) {
        framesArr.append(f);
    }
    tObj["frames"] = framesArr;
    if (!t.aliasOf.isEmpty()) tObj["alias_of"] = t.aliasOf;
    if (t.hFlip) tObj["h_flip"] = true;
    if (t.vFlip) tObj["v_flip"] = true;
    if (t.loopMode != AnimationLoopMode::Loop) tObj["loop"] = animationLoopModeName(t.loopMode);
    return tObj;
}

AnimationTimeline timelineFromJson(const QJsonObject& tObj, int defaultFps) {
    AnimationTimeline t;
    t.name = tObj["name"].toString();
    t.fps = sanitizedAnimationFps(tObj["fps"].toInt(defaultFps));
    const QJsonArray framesArr = tObj["frames"].toArray();
    t.frames.reserve(framesArr.size());
    for (const auto& fVal : framesArr) {
        t.frames.append(fVal.toString());
    }
    t.aliasOf = tObj["alias_of"].toString();
    t.hFlip   = tObj["h_flip"].toBool(false);
    t.vFlip   = tObj["v_flip"].toBool(false);
    // Projects saved before loop modes have none, and those animations looped.
    t.loopMode = animationLoopModeFromName(tObj["loop"].toString()).value_or(AnimationLoopMode::Loop);
    return t;
}

QJsonArray pointToJson(const QPoint& point) {
    return QJsonArray{point.x(), point.y()};
}

std::optional<QPoint> pointFromJson(const QJsonValue& value) {
    const QJsonArray arr = value.toArray();
    if (arr.size() != 2) return std::nullopt;
    return QPoint(arr[0].toInt(), arr[1].toInt());
}

// Sprite paths turn relative for portable saves, like the atlas's sprite paths. A sprite a
// puppets file named by name or index isn't a path, so it is never rewritten.
QJsonObject puppetToJson(const Puppet& puppet, const std::function<QString(const QString&)>& portablePath) {
    QJsonObject pObj;
    pObj["name"] = puppet.name;
    QJsonArray partsArr;
    for (const PuppetPart& part : puppet.parts) {
        QJsonObject partObj;
        partObj["name"] = part.name;
        if (part.position) partObj["at"] = pointToJson(*part.position);
        QJsonArray variantsArr;
        for (const PuppetVariant& variant : part.variants) {
            QJsonObject vObj;
            const bool isSprite = variant.kind == PuppetVariant::Kind::Sprite;
            vObj["kind"] = isSprite ? QStringLiteral("sprite") : QStringLiteral("animation");
            const bool isPath = isSprite && !variant.byIndex && QDir::isAbsolutePath(variant.source);
            const QString stored = isPath ? portablePath(variant.source) : variant.source;
            vObj["source"] = stored;
            if (isPath && QDir::isRelativePath(stored)) vObj["relative"] = true;
            if (variant.byIndex) vObj["by_index"] = true;
            if (!variant.name.isEmpty()) vObj["name"] = variant.name;
            variantsArr.append(vObj);
        }
        partObj["variants"] = variantsArr;
        if (!part.hiddenMarkers.isEmpty()) partObj["hidden_markers"] = QJsonArray::fromStringList(part.hiddenMarkers);
        if (part.rigLocked) partObj["rig_locked"] = true;
        partsArr.append(partObj);
    }
    pObj["parts"] = partsArr;

    QJsonArray attachmentsArr;
    for (const PuppetAttachment& a : puppet.attachments) {
        QJsonObject aObj;
        aObj["child"] = a.child;
        aObj["child_marker"] = a.childMarker;
        aObj["parent"] = a.parent;
        aObj["parent_marker"] = a.parentMarker;
        if (a.leeway != 0) aObj["leeway"] = a.leeway;
        if (a.angleMin && a.angleMax) {
            aObj["angle_min"] = *a.angleMin;
            aObj["angle_max"] = *a.angleMax;
        }
        attachmentsArr.append(aObj);
    }
    pObj["attachments"] = attachmentsArr;

    QJsonArray posesArr;
    for (const PuppetPose& pose : puppet.poses) {
        QJsonObject poseObj;
        poseObj["name"] = pose.name;
        if (!pose.owner.isEmpty()) poseObj["animation"] = pose.owner;
        if (!pose.lockedParts.isEmpty()) poseObj["locked_parts"] = QJsonArray::fromStringList(pose.lockedParts);
        QJsonArray settingsArr;
        for (const PuppetPoseSetting& s : pose.settings) {
            QJsonObject sObj;
            sObj["part"] = s.part;
            if (!s.variant.isEmpty()) sObj["variant"] = s.variant;
            if (s.angle) sObj["angle"] = *s.angle;
            if (s.offset) sObj["offset"] = pointToJson(*s.offset);
            if (s.position) sObj["at"] = pointToJson(*s.position);
            settingsArr.append(sObj);
        }
        poseObj["parts"] = settingsArr;
        posesArr.append(poseObj);
    }
    pObj["poses"] = posesArr;

    QJsonArray sequencesArr;
    for (const PuppetSequence& sequence : puppet.sequences) {
        QJsonObject seqObj;
        seqObj["name"] = sequence.name;
        seqObj["fps"] = sequence.fps;
        if (sequence.loopMode != AnimationLoopMode::Loop) seqObj["loop"] = animationLoopModeName(sequence.loopMode);
        QJsonArray stepsArr;
        for (const PuppetSequenceStep& step : sequence.steps) {
            QJsonObject stepObj;
            stepObj["pose"] = step.pose;
            if (step.in != 0) stepObj["in"] = step.in;
            if (step.hold != 0) stepObj["hold"] = step.hold;
            if (step.snap) stepObj["ease"] = QStringLiteral("step");
            if (!step.partSnaps.isEmpty()) {
                QJsonObject partEases;
                for (auto it = step.partSnaps.cbegin(); it != step.partSnaps.cend(); ++it)
                    partEases[it.key()] = it.value() ? QStringLiteral("step") : QStringLiteral("linear");
                stepObj["part_eases"] = partEases;
            }
            stepsArr.append(stepObj);
        }
        seqObj["steps"] = stepsArr;
        sequencesArr.append(seqObj);
    }
    pObj["sequences"] = sequencesArr;
    return pObj;
}

Puppet puppetFromJson(const QJsonObject& pObj, const QDir& spriteDir) {
    Puppet puppet;
    puppet.name = pObj["name"].toString();
    for (const auto& partVal : pObj["parts"].toArray()) {
        const QJsonObject partObj = partVal.toObject();
        PuppetPart part;
        part.name = partObj["name"].toString();
        part.position = pointFromJson(partObj["at"]);
        for (const auto& marker : partObj["hidden_markers"].toArray()) part.hiddenMarkers.append(marker.toString());
        part.rigLocked = partObj["rig_locked"].toBool();
        for (const auto& vVal : partObj["variants"].toArray()) {
            const QJsonObject vObj = vVal.toObject();
            PuppetVariant variant;
            variant.kind = vObj["kind"].toString() == QLatin1String("animation")
                ? PuppetVariant::Kind::Animation : PuppetVariant::Kind::Sprite;
            variant.source = vObj["source"].toString();
            if (vObj["relative"].toBool(false)) variant.source = spriteDir.filePath(variant.source);
            variant.byIndex = vObj["by_index"].toBool(false);
            variant.name = vObj["name"].toString();
            part.variants.append(variant);
        }
        puppet.parts.append(part);
    }
    for (const auto& aVal : pObj["attachments"].toArray()) {
        const QJsonObject aObj = aVal.toObject();
        PuppetAttachment a;
        a.child = aObj["child"].toString();
        a.childMarker = aObj["child_marker"].toString();
        a.parent = aObj["parent"].toString();
        a.parentMarker = aObj["parent_marker"].toString();
        a.leeway = qMax(0, aObj["leeway"].toInt(0));
        if (aObj.contains("angle_min") && aObj.contains("angle_max")) {
            a.angleMin = aObj["angle_min"].toInt();
            a.angleMax = aObj["angle_max"].toInt();
        }
        puppet.attachments.append(a);
    }
    for (const auto& poseVal : pObj["poses"].toArray()) {
        const QJsonObject poseObj = poseVal.toObject();
        PuppetPose pose;
        pose.name = poseObj["name"].toString();
        pose.owner = poseObj["animation"].toString();
        for (const auto& part : poseObj["locked_parts"].toArray()) pose.lockedParts.append(part.toString());
        for (const auto& sVal : poseObj["parts"].toArray()) {
            const QJsonObject sObj = sVal.toObject();
            PuppetPoseSetting s;
            s.part = sObj["part"].toString();
            s.variant = sObj["variant"].toString();
            if (sObj.contains("angle")) s.angle = sObj["angle"].toInt();
            s.offset = pointFromJson(sObj["offset"]);
            s.position = pointFromJson(sObj["at"]);
            pose.settings.append(s);
        }
        puppet.poses.append(pose);
    }
    for (const auto& seqVal : pObj["sequences"].toArray()) {
        const QJsonObject seqObj = seqVal.toObject();
        PuppetSequence sequence;
        sequence.name = seqObj["name"].toString();
        sequence.fps = sanitizedAnimationFps(seqObj["fps"].toInt(DEFAULT_ANIMATION_FPS));
        sequence.loopMode = animationLoopModeFromName(seqObj["loop"].toString()).value_or(AnimationLoopMode::Loop);
        for (const auto& stepVal : seqObj["steps"].toArray()) {
            const QJsonObject stepObj = stepVal.toObject();
            PuppetSequenceStep step;
            step.pose = stepObj["pose"].toString();
            step.in = qBound(0, stepObj["in"].toInt(0), PUPPET_MAX_STEP_FRAMES);
            step.hold = qBound(0, stepObj["hold"].toInt(0), PUPPET_MAX_STEP_FRAMES);
            step.snap = stepObj["ease"].toString() == QLatin1String("step");
            const QJsonObject partEases = stepObj["part_eases"].toObject();
            for (auto it = partEases.begin(); it != partEases.end(); ++it) {
                const bool snaps = it.value().toString() == QLatin1String("step");
                if (snaps != step.snap) step.partSnaps.insert(it.key(), snaps);
            }
            sequence.steps.append(step);
        }
        puppet.sequences.append(sequence);
    }
    return puppet;
}

bool isValidCliPath(const QString& path) {
    if (path.isEmpty()) {
        return true; // Empty path is allowed (use default)
    }
    // Resolve symlinks and .. components to get the real path
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty()) {
        return false; // Path doesn't exist or can't be resolved
    }
    QFileInfo fi(canonical);
    return fi.isAbsolute() && fi.isExecutable() && !fi.isDir();
}

}  // namespace

QJsonObject ProjectPayloadCodec::build(const ProjectPayloadBuildInput& input) {
    QJsonObject root;
    root["schema_version"] = 6;
    root["written_by"] = QStringLiteral("sprat-gui");
    root["written_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    QJsonObject layoutOpts;
    layoutOpts["profile"] = input.profile;
    layoutOpts["padding"] = input.padding;
    layoutOpts["trim_transparent"] = input.trimTransparent;
    if (input.sourceResolutionWidth > 0 && input.sourceResolutionHeight > 0) {
        layoutOpts["source_resolution"] = formatResolutionText(input.sourceResolutionWidth, input.sourceResolutionHeight);
    }
    root["layout_options"] = layoutOpts;

    QJsonObject layoutInfo;
    if (input.portablePaths) {
        // Store the source images folder as a path relative to the project directory
        // so the project can be relocated without breaking image references.
        // Fall back to an absolute path when the source folder is outside the project tree.
        const QString srcBase = input.sourceFolder.isEmpty() ? input.currentFolder : input.sourceFolder;
        if (!input.projectDir.isEmpty() && !srcBase.isEmpty()) {
            const QString rel = QDir(input.projectDir).relativeFilePath(srcBase);
            layoutInfo["folder"] = rel.startsWith("..") ? srcBase : rel;
        } else {
            layoutInfo["folder"] = srcBase;
        }
    } else {
        layoutInfo["folder"] = input.currentFolder;
    }
    layoutInfo["scale"] = input.layoutScale;
    layoutInfo["output"] = input.layoutOutput;
    layoutInfo["source_mode"] = input.layoutSourceIsList ? "list" : "folder";
    if (!input.activeFramePaths.isEmpty()) {
        QJsonArray framePaths;
        if (input.portablePaths) {
            // Use sourceFolder as base — matches the save service's sprite copy logic
            const QString base = input.sourceFolder.isEmpty() ? input.currentFolder : input.sourceFolder;
            QDir baseDir(base);
            for (const QString& path : input.activeFramePaths) {
                QString rel = baseDir.relativeFilePath(path);
                if (rel.startsWith("..")) {
                    rel = QFileInfo(path).fileName();
                }
                framePaths.append(rel);
            }
        } else {
            for (const QString& path : input.activeFramePaths) {
                framePaths.append(path);
            }
        }
        layoutInfo["frame_paths"] = framePaths;
    }

    // Write sources array (new model)
    auto sourceTypeToString = [](SourceType t) -> QString {
        switch (t) {
        case SourceType::Folder:      return QStringLiteral("folder");
        case SourceType::SingleImage: return QStringLiteral("single_image");
        case SourceType::Archive:     return QStringLiteral("archive");
        case SourceType::Url:         return QStringLiteral("url");
        }
        return QStringLiteral("folder");
    };
    if (!input.sources.isEmpty()) {
        QJsonArray sourcesArr;
        for (const auto& src : input.sources) {
            if (src.hasError) continue;  // don't persist failed sources
            QJsonObject sObj;
            sObj["name"] = src.name;
            sObj["type"] = sourceTypeToString(src.type);
            QString origPath = src.originalPath;
            if (input.portablePaths && !input.projectDir.isEmpty() && !origPath.isEmpty()
                && src.type == SourceType::Folder) {
                const QString rel = QDir(input.projectDir).relativeFilePath(origPath);
                origPath = rel.startsWith("..") ? origPath : rel;
            }
            sObj["originalPath"] = origPath;
            if (!src.cachedFolderPath.isEmpty()) {
                // Relative when inside the project, so excluded files (stored relative to
                // it) still resolve after the project is moved or opened from a ZIP.
                QString cached = src.cachedFolderPath;
                if (input.portablePaths && !input.projectDir.isEmpty()) {
                    const QString rel = QDir(input.projectDir).relativeFilePath(cached);
                    if (!rel.startsWith(QLatin1String(".."))) cached = rel;
                }
                sObj["cachedFolder"] = cached;
            }
            if (!src.excludedFiles.isEmpty()) {
                QJsonArray excArr;
                for (const auto& e : src.excludedFiles) excArr.append(e);
                sObj["excluded"] = excArr;
            }
            if (!src.hiddenFolders.isEmpty()) {
                QJsonArray hiddenArr;
                for (const auto& h : src.hiddenFolders) hiddenArr.append(h);
                sObj["hidden_folders"] = hiddenArr;
            }
            sourcesArr.append(sObj);
        }
        layoutInfo["sources"] = sourcesArr;
    }

    root["layout"] = layoutInfo;

    // Write orphaned sprites (top-level key)
    if (!input.orphanedSpritePaths.isEmpty()) {
        QJsonArray orphanedArr;
        for (const QString& p : input.orphanedSpritePaths) {
            QJsonObject entry;
            entry["path"] = p;
            orphanedArr.append(entry);
        }
        root["orphaned_sprites"] = orphanedArr;
    }

    QJsonObject animInfo;
    animInfo["selected_timeline_index"] = input.selectedTimelineIndex;
    if (!input.selectedTimelineFrameRows.isEmpty()) {
        animInfo["selected_frame_rows"] = toJsonArray(input.selectedTimelineFrameRows);
    }
    animInfo["animation_frame_index"] = input.animationFrameIndex;
    animInfo["animation_playing"] = input.animationPlaying;
    QJsonArray timelinesArr;
    for (const auto& t : input.timelines) {
        timelinesArr.append(timelineToJson(t));
    }
    if (!input.timelines.isEmpty()) {
        // Keep legacy field for older consumers that still expect one global fps.
        animInfo["animation_fps"] = input.timelines.first().fps;
    }
    animInfo["timelines"] = timelinesArr;
    root["animations"] = animInfo;

    // --- v4: Multi-atlas block ---
    if (!input.atlases.isEmpty()) {
        root["active_atlas_index"] = input.activeAtlasIndex;
        const QString spBase = input.sourceFolder.isEmpty() ? input.currentFolder : input.sourceFolder;
        const QDir spBaseDir(spBase);
        QJsonArray atlasesArr;
        for (const auto& atlas : input.atlases) {
            QJsonObject aObj;
            aObj["id"]         = atlas.id;
            aObj["name"]       = atlas.name;
            aObj["is_neutral"] = atlas.isNeutral;
            aObj["is_excluded"] = atlas.isExcluded;
            aObj["output_subdir"] = atlas.outputSubdir;

            QJsonObject eoObj;
            {
                QJsonArray profArr;
                for (const QString& p : atlas.exportConfig.profiles)
                    profArr.append(p);
                eoObj["profiles"]     = profArr;
            }
            eoObj["transform"]    = atlas.exportConfig.transform;
            eoObj["scale_filter"] = atlas.exportConfig.scaleFilter;
            if (atlas.exportConfig.colors > 0) eoObj["colors"] = atlas.exportConfig.colors;
            if (atlas.exportConfig.dither)     eoObj["dither"]  = true;
            aObj["export_override"] = eoObj;

            // Sprite paths — excluded atlas paths are derived at runtime via syncExcludedAtlas()
            QJsonArray spArr;
            if (!atlas.isExcluded) {
                for (const QString& sp : atlas.spritePaths) {
                    if (input.portablePaths && !spBase.isEmpty()) {
                        QString rel = spBaseDir.relativeFilePath(sp);
                        spArr.append(rel.startsWith("..") ? sp : rel);
                    } else {
                        spArr.append(sp);
                    }
                }
            }
            aObj["sprite_paths"] = spArr;

            // Timelines
            QJsonArray atArr;
            for (const auto& t : atlas.timelines) {
                atArr.append(timelineToJson(t));
            }
            aObj["timelines"] = atArr;

            if (!atlas.puppets.isEmpty()) {
                const auto portablePath = [&](const QString& path) {
                    if (!input.portablePaths || spBase.isEmpty()) return path;
                    const QString rel = spBaseDir.relativeFilePath(path);
                    return rel.startsWith("..") ? path : rel;
                };
                QJsonArray puppetsArr;
                for (const Puppet& puppet : atlas.puppets) {
                    puppetsArr.append(puppetToJson(puppet, portablePath));
                }
                aObj["puppets"] = puppetsArr;
            }

            atlasesArr.append(aObj);
        }
        root["atlases"] = atlasesArr;
    }

    QJsonObject markersInfo;
    // Sprites not laid out since the project was loaded keep their stored state; the
    // active atlas goes last, so its live sprites win over stale copies.
    QJsonObject spritesState = input.storedSpriteStates;
    for (const auto& model : input.layoutModels) {
        for (const auto& s : model.sprites) {
            spritesState[spriteStateKey(input.currentFolder, s->path)] = spriteStateToJson(*s);
        }
    }
    markersInfo["sprites"] = spritesState;
    if (input.selectedSprite) {
        markersInfo["selected_sprite_path"] = input.selectedSprite->path;
    }
    if (!input.selectedSpritePaths.isEmpty()) {
        QJsonArray selectedSprites;
        for (const QString& path : input.selectedSpritePaths) {
            selectedSprites.append(path);
        }
        markersInfo["selected_sprite_paths"] = selectedSprites;
    }
    if (!input.primarySelectedSpritePath.isEmpty()) {
        markersInfo["selected_primary_sprite_path"] = input.primarySelectedSpritePath;
    }
    markersInfo["selected_marker_name"] = input.selectedPointName;
    root["spritemarkers"] = markersInfo;

    QJsonObject uiOpts;
    uiOpts["layout_zoom"] = input.layoutZoom;
    uiOpts["preview_zoom"] = input.previewZoom;
    uiOpts["animation_zoom"] = input.animationZoom;
    if (input.exportZoom > 0.0)
        uiOpts["export_zoom"] = input.exportZoom;
    if (!input.dockState.isEmpty()) {
        uiOpts["dock_state"] = QString::fromLatin1(input.dockState.toBase64());
    }
    root["ui_options"] = uiOpts;

    QJsonObject settings;
    settings["canvas_color"] = input.appSettings.workspaceColor.name(QColor::HexArgb);
    settings["frame_color"] = input.appSettings.spriteFrameColor.name(QColor::HexArgb);
    settings["show_borders"] = input.appSettings.showBorders;
    settings["border_color"] = input.appSettings.borderColor.name(QColor::HexArgb);
    settings["border_style"] = static_cast<int>(input.appSettings.borderStyle);
    settings["show_trim_rect"] = input.appSettings.showTrimRect;
    settings["trim_rect_color"] = input.appSettings.trimRectColor.name(QColor::HexArgb);
    settings["trim_rect_style"] = static_cast<int>(input.appSettings.trimRectStyle);
    if (!input.portablePaths) {
        settings["cli_spratlayout"] = input.cliPaths.layoutBinary;
        settings["cli_spratpack"] = input.cliPaths.packBinary;
        settings["cli_spratconvert"] = input.cliPaths.convertBinary;
    }
    root["settings"] = settings;

    QJsonObject saveOpts;
    saveOpts["destination"] = input.saveConfig.destination;
    saveOpts["output_path"] = input.saveConfig.outputPath;
    saveOpts["transform"] = input.saveConfig.transform;
    QJsonArray profilesArr;
    for (const QString& profileName : input.saveConfig.profiles) {
        profilesArr.append(profileName);
    }
    saveOpts["profiles"] = profilesArr;
    saveOpts["profiles_global"] = input.saveConfig.profilesGlobal;
    saveOpts["sync_sprites"] = input.saveConfig.syncSprites;
    if (!input.saveConfig.postExportCommand.isEmpty())
        saveOpts["post_export_command"] = input.saveConfig.postExportCommand;
    root["save_options"] = saveOpts;

    QJsonArray presetsArr;
    for (const ExportPreset& p : input.exportPresets) {
        QJsonObject po;
        po["name"]         = p.name;
        po["output_path"]  = p.outputPath;
        po["transform"]    = p.transform;
        po["scale_filter"] = p.scaleFilter;
        if (!p.postExportCommand.isEmpty())
            po["post_export_command"] = p.postExportCommand;
        QJsonArray prArr;
        for (const QString& pr : p.profiles) prArr.append(pr);
        po["profiles"] = prArr;
        presetsArr.append(po);
    }
    root["export_presets"] = presetsArr;

    QJsonArray templArr;
    for (const MarkerTemplate& tmpl : input.markerTemplates) {
        QJsonObject to;
        to["name"] = tmpl.name;
        QJsonArray pts;
        for (const NamedPoint& p : tmpl.points) {
            QJsonObject po;
            po["name"]   = p.name;
            po["kind"]   = static_cast<int>(p.kind);
            po["x"]      = p.x;
            po["y"]      = p.y;
            po["radius"] = p.radius;
            po["w"]      = p.w;
            po["h"]      = p.h;
            QJsonArray poly;
            for (const QPoint& pt : p.polygonPoints)
                poly.append(QJsonObject{{"x", pt.x()}, {"y", pt.y()}});
            po["polygon"] = poly;
            pts.append(po);
        }
        to["points"] = pts;
        templArr.append(to);
    }
    root["marker_templates"] = templArr;

    root["complete"] = true;
    return root;
}

QString ProjectPayloadCodec::spriteStateKey(const QString& currentFolder, const QString& spritePath) {
    const QString key = QDir(currentFolder).relativeFilePath(spritePath);
    return key.isEmpty() ? QFileInfo(spritePath).fileName() : key;
}

std::optional<QJsonObject> ProjectPayloadCodec::findSpriteState(const QJsonObject& spriteStates,
                                                                const QString& currentFolder,
                                                                const QString& spritePath) {
    for (const QString& key : {QDir(currentFolder).relativeFilePath(spritePath), QFileInfo(spritePath).fileName()}) {
        const auto it = spriteStates.constFind(key);
        if (it != spriteStates.constEnd()) return it->toObject();
    }
    return std::nullopt;
}

QJsonObject ProjectPayloadCodec::atlasExportPayload(const QJsonObject& payload, const QStringList& spritePaths,
                                                    const QVector<AnimationTimeline>& timelines,
                                                    const QString& currentFolder) {
    QJsonObject result = payload;

    QJsonObject markersInfo = payload["spritemarkers"].toObject();
    const QJsonObject allStates = markersInfo["sprites"].toObject();
    QJsonObject atlasStates;
    const QDir folder(currentFolder);
    for (const QString& path : spritePaths) {
        for (const QString& key : {folder.relativeFilePath(path), QFileInfo(path).fileName()}) {
            const auto it = allStates.constFind(key);
            if (it != allStates.constEnd()) {
                atlasStates.insert(key, *it);
                break;
            }
        }
    }
    markersInfo["sprites"] = atlasStates;
    result["spritemarkers"] = markersInfo;

    QJsonObject animInfo = payload["animations"].toObject();
    QJsonArray timelinesArr;
    for (const auto& t : timelines) {
        timelinesArr.append(timelineToJson(t));
    }
    animInfo["timelines"] = timelinesArr;
    if (timelines.isEmpty()) {
        animInfo.remove("animation_fps");
    } else {
        animInfo["animation_fps"] = timelines.first().fps;
    }
    result["animations"] = animInfo;
    return result;
}

QJsonObject ProjectPayloadCodec::spriteStateToJson(const Sprite& sprite) {
    QJsonObject sObj;
    sObj["name"] = sprite.name;
    sObj["has_pivot"] = true;
    sObj["pivot_x"] = sprite.pivotX;
    sObj["pivot_y"] = sprite.pivotY;
    QJsonArray markersArr;
    for (const auto& p : sprite.points) {
        QJsonObject mObj;
        const QString kind = markerKindToString(p.kind);
        mObj["name"] = normalizeMarkerName(p.name);
        mObj["x"] = p.x;
        mObj["y"] = p.y;
        // Keep GUI field and include CLI-compatible field for spratconvert.
        mObj["kind"] = kind;
        mObj["type"] = kind;
        mObj["radius"] = p.radius;
        mObj["w"] = p.w;
        mObj["h"] = p.h;
        if (!p.polygonPoints.isEmpty()) {
            QJsonArray polyArr;
            QJsonArray verticesArr;
            for (const auto& pt : p.polygonPoints) {
                QJsonArray ptPair;
                ptPair.append(pt.x());
                ptPair.append(pt.y());
                polyArr.append(ptPair);

                QJsonObject vertex;
                vertex["x"] = pt.x();
                vertex["y"] = pt.y();
                verticesArr.append(vertex);
            }
            // GUI legacy format.
            mObj["polygon_points"] = polyArr;
            // CLI format expected by spratconvert.
            mObj["vertices"] = verticesArr;
        }
        markersArr.append(mObj);
    }
    sObj["markers"] = markersArr;

    if (!sprite.aliases.isEmpty()) {
        QJsonArray aliasArr;
        for (const auto& a : sprite.aliases) aliasArr.append(a);
        sObj["aliases"] = aliasArr;
    }
    if (sprite.isNineSliced) {
        sObj["is_nine_sliced"] = true;
        sObj["ns_left"]   = sprite.nsLeft;
        sObj["ns_top"]    = sprite.nsTop;
        sObj["ns_right"]  = sprite.nsRight;
        sObj["ns_bottom"] = sprite.nsBottom;
        sObj["ns_h_mode"] = sprite.nsHMode;
        sObj["ns_v_mode"] = sprite.nsVMode;
        if (sprite.nsTargetWidth  > 0) sObj["ns_target_width"]  = sprite.nsTargetWidth;
        if (sprite.nsTargetHeight > 0) sObj["ns_target_height"] = sprite.nsTargetHeight;
    }
    return sObj;
}

void ProjectPayloadCodec::applySpriteState(Sprite& sprite, const QJsonObject& state) {
    if (state.contains("name")) {
        sprite.name = state["name"].toString();
    }
    sprite.aliases.clear();
    for (const auto& a : state["aliases"].toArray())
        sprite.aliases.append(a.toString());
    // Only apply stored pivot if the project was saved with the has_pivot flag.
    // Older projects (no flag, or pivot_y=0 from partial centering) are ignored
    // so LayoutParser's correctly-centered values are used instead.
    if (state["has_pivot"].toBool() && state.contains("pivot_x") && state.contains("pivot_y")) {
        sprite.pivotX = state["pivot_x"].toInt();
        sprite.pivotY = state["pivot_y"].toInt();
    }
    sprite.isNineSliced = state["is_nine_sliced"].toBool(false);
    if (sprite.isNineSliced) {
        sprite.nsLeft   = state["ns_left"].toInt(sprite.nsLeft);
        sprite.nsTop    = state["ns_top"].toInt(sprite.nsTop);
        sprite.nsRight  = state["ns_right"].toInt(sprite.nsRight);
        sprite.nsBottom = state["ns_bottom"].toInt(sprite.nsBottom);
        sprite.nsHMode        = nineSliceFillModeOrDefault(state["ns_h_mode"].toString(sprite.nsHMode));
        sprite.nsVMode        = nineSliceFillModeOrDefault(state["ns_v_mode"].toString(sprite.nsVMode));
        sprite.nsTargetWidth  = state["ns_target_width"].toInt(0);
        sprite.nsTargetHeight = state["ns_target_height"].toInt(0);
    }
    if (!state.contains("markers")) {
        return;
    }
    sprite.points.clear();
    QJsonArray markersArr = state["markers"].toArray();
    sprite.points.reserve(markersArr.size());
    for (const auto& mVal : markersArr) {
        QJsonObject mObj = mVal.toObject();
        NamedPoint p;
        p.name = normalizeMarkerName(mObj["name"].toString());
        p.x = mObj["x"].toInt();
        p.y = mObj["y"].toInt();
        QString kind = mObj["kind"].toString();
        if (kind.isEmpty()) {
            kind = mObj["type"].toString();
        }
        p.kind = markerKindFromString(kind);
        p.radius = mObj["radius"].toInt(8);
        p.w = mObj["w"].toInt(16);
        p.h = mObj["h"].toInt(16);
        if (mObj.contains("polygon_points")) {
            QJsonArray polyArr = mObj["polygon_points"].toArray();
            p.polygonPoints.reserve(polyArr.size());
            for (const auto& ptVal : polyArr) {
                QJsonArray ptPair = ptVal.toArray();
                if (ptPair.size() == 2) {
                    p.polygonPoints.append(QPoint(ptPair[0].toInt(), ptPair[1].toInt()));
                }
            }
        } else if (mObj.contains("vertices")) {
            QJsonArray verticesArr = mObj["vertices"].toArray();
            p.polygonPoints.reserve(verticesArr.size());
            for (const auto& vertexVal : verticesArr) {
                QJsonObject vertex = vertexVal.toObject();
                if (vertex.contains("x") && vertex.contains("y")) {
                    p.polygonPoints.append(QPoint(vertex["x"].toInt(), vertex["y"].toInt()));
                }
            }
        }
        sprite.points.append(p);
    }
}

ProjectPayloadApplyResult ProjectPayloadCodec::applyToLayout(const QJsonObject& root, const QString& currentFolder, QVector<LayoutModel>& layoutModels, const QString& projectDir) {
    ProjectPayloadApplyResult out;
    if (!root.value(QStringLiteral("complete")).toBool()) {
        qWarning() << "ProjectPayloadCodec: project file may be incomplete or corrupted (missing 'complete' sentinel)";
    }

    QJsonObject markersInfo = root["spritemarkers"].toObject();
    QJsonObject spritesState = markersInfo["sprites"].toObject();

    // Build migration map from old atlas-level nine_slice format (v4 files written before per-sprite format)
    struct NsMigration {
        int left = 0, top = 0, right = 0, bottom = 0;
        QString hMode = QStringLiteral("stretch"), vMode = QStringLiteral("stretch");
    };
    QHash<QString, NsMigration> nsMigrationMap;
    {
        const QDir spDir(currentFolder);
        for (const auto& aVal : root["atlases"].toArray()) {
            const QJsonObject aObj = aVal.toObject();
            for (const auto& nsVal : aObj["nine_slice"].toArray()) {
                const QJsonObject nsObj = nsVal.toObject();
                NsMigration ns;
                ns.left   = nsObj["left"].toInt(0);
                ns.top    = nsObj["top"].toInt(0);
                ns.right  = nsObj["right"].toInt(0);
                ns.bottom = nsObj["bottom"].toInt(0);
                ns.hMode  = nineSliceFillModeOrDefault(nsObj["h_mode"].toString());
                ns.vMode  = nineSliceFillModeOrDefault(nsObj["v_mode"].toString());
                for (const auto& spVal : nsObj["sprites"].toArray()) {
                    QString sp = spVal.toString();
                    if (!sp.isEmpty() && QDir::isRelativePath(sp) && !currentFolder.isEmpty())
                        sp = spDir.filePath(sp);
                    if (!sp.isEmpty()) nsMigrationMap.insert(sp, ns);
                }
            }
        }
    }

    for (auto& model : layoutModels) {
        for (auto& sprite : model.sprites) {
            const std::optional<QJsonObject> state = findSpriteState(spritesState, currentFolder, sprite->path);
            if (!state) {
                continue;
            }
            applySpriteState(*sprite, *state);
            if (!(*state)["is_nine_sliced"].toBool(false) && nsMigrationMap.contains(sprite->path)) {
                const auto& ns = nsMigrationMap[sprite->path];
                sprite->isNineSliced = true;
                sprite->nsLeft   = ns.left;
                sprite->nsTop    = ns.top;
                sprite->nsRight  = ns.right;
                sprite->nsBottom = ns.bottom;
                sprite->nsHMode  = ns.hMode;
                sprite->nsVMode  = ns.vMode;
            }
        }
    }

    QJsonObject animInfo = root["animations"].toObject();
    out.selectedTimelineIndex = animInfo["selected_timeline_index"].toInt(-1);
    out.selectedTimelineFrameRows = fromJsonArray(animInfo["selected_frame_rows"]);
    out.animationFrameIndex = animInfo["animation_frame_index"].toInt(0);
    out.animationPlaying = animInfo["animation_playing"].toBool(false);
    const int legacyAnimationFps =
        sanitizedAnimationFps(animInfo["animation_fps"].toInt(DEFAULT_ANIMATION_FPS));
    QJsonArray timelinesArr = animInfo["timelines"].toArray();
    out.timelines.reserve(timelinesArr.size());
    for (const auto& tVal : timelinesArr) {
        out.timelines.append(timelineFromJson(tVal.toObject(), legacyAnimationFps));
    }

    out.selectedSpritePath = markersInfo["selected_sprite_path"].toString();
    {
        const QJsonArray selectedSprites = markersInfo["selected_sprite_paths"].toArray();
        out.selectedSpritePaths.reserve(selectedSprites.size());
        for (const auto& pathVal : selectedSprites) {
            out.selectedSpritePaths.append(pathVal.toString());
        }
    }
    out.primarySelectedSpritePath = markersInfo["selected_primary_sprite_path"].toString();
    out.selectedMarkerName = markersInfo["selected_marker_name"].toString();

    {
        const QJsonObject layoutOpts = root["layout_options"].toObject();
        int sourceWidth = 0;
        int sourceHeight = 0;
        if (parseResolutionText(layoutOpts["source_resolution"].toString(), sourceWidth, sourceHeight)) {
            out.sourceResolutionWidth = sourceWidth;
            out.sourceResolutionHeight = sourceHeight;
        }
    }

    QJsonObject uiOpts = root["ui_options"].toObject();
    out.layoutZoom = uiOpts["layout_zoom"].toDouble(1.0);
    out.previewZoom = uiOpts["preview_zoom"].toDouble(1.0);
    out.animationZoom = uiOpts["animation_zoom"].toDouble(1.0);
    out.exportZoom = uiOpts["export_zoom"].toDouble(0.0);
    out.dockState = QByteArray::fromBase64(uiOpts["dock_state"].toString().toLatin1());

    QJsonObject settings = root["settings"].toObject();
    {
        const QString workspaceColor = settings["canvas_color"].toString();
        if (!workspaceColor.isEmpty()) {
            out.appSettings.workspaceColor = QColor(workspaceColor);
        }
        const QString spriteFrameColor = settings["frame_color"].toString();
        if (!spriteFrameColor.isEmpty()) {
            out.appSettings.spriteFrameColor = QColor(spriteFrameColor);
        }
        out.appSettings.showBorders = settings["show_borders"].toBool(out.appSettings.showBorders);
        const QString borderColor = settings["border_color"].toString();
        if (!borderColor.isEmpty()) {
            out.appSettings.borderColor = QColor(borderColor);
        }
        out.appSettings.borderStyle = static_cast<Qt::PenStyle>(settings["border_style"].toInt(static_cast<int>(out.appSettings.borderStyle)));
        out.appSettings.showTrimRect = settings["show_trim_rect"].toBool(out.appSettings.showTrimRect);
        const QString trimRectColor = settings["trim_rect_color"].toString();
        if (!trimRectColor.isEmpty()) {
            out.appSettings.trimRectColor = QColor(trimRectColor);
        }
        out.appSettings.trimRectStyle = static_cast<Qt::PenStyle>(settings["trim_rect_style"].toInt(static_cast<int>(out.appSettings.trimRectStyle)));
        // Validate CLI paths to prevent arbitrary binary execution
        QString layoutPath = settings["cli_spratlayout"].toString();
        if (isValidCliPath(layoutPath)) {
            out.cliPaths.layoutBinary = layoutPath;
        }

        QString packPath = settings["cli_spratpack"].toString();
        if (isValidCliPath(packPath)) {
            out.cliPaths.packBinary = packPath;
        }

        QString convertPath = settings["cli_spratconvert"].toString();
        if (isValidCliPath(convertPath)) {
            out.cliPaths.convertBinary = convertPath;
        }
    }

    const int schemaVersion = root["schema_version"].toInt(1);
    QJsonObject saveOpts = root["save_options"].toObject();
    out.saveConfig.destination = saveOpts["destination"].toString();
    out.saveConfig.outputPath = saveOpts["output_path"].toString();
    out.saveConfig.transform = readTransform(saveOpts["transform"], schemaVersion);
    QJsonArray profilesArr = saveOpts["profiles"].toArray();
    out.saveConfig.profiles.reserve(profilesArr.size());
    for (const auto& pVal : profilesArr) {
        if (PathSafety::isPlainName(pVal.toString()))
            out.saveConfig.profiles.append(pVal.toString());
    }
    out.saveConfig.profilesGlobal     = saveOpts["profiles_global"].toBool(true);
    out.saveConfig.syncSprites        = saveOpts["sync_sprites"].toBool(false);
    out.saveConfig.postExportCommand  = saveOpts["post_export_command"].toString();

    for (const auto& pv : root["export_presets"].toArray()) {
        QJsonObject po = pv.toObject();
        ExportPreset p;
        p.name               = po["name"].toString();
        p.outputPath         = po["output_path"].toString();
        p.transform          = readTransform(po["transform"], schemaVersion);
        p.scaleFilter        = po["scale_filter"].toString();
        p.postExportCommand  = po["post_export_command"].toString();
        for (const auto& prv : po["profiles"].toArray()) {
            if (PathSafety::isPlainName(prv.toString()))
                p.profiles.append(prv.toString());
        }
        if (!p.name.isEmpty()) out.exportPresets.append(p);
    }

    for (const auto& tv : root["marker_templates"].toArray()) {
        const QJsonObject to = tv.toObject();
        MarkerTemplate tmpl;
        tmpl.name = to["name"].toString();
        for (const auto& pv : to["points"].toArray()) {
            const QJsonObject po = pv.toObject();
            NamedPoint p;
            p.name   = po["name"].toString();
            p.kind   = static_cast<MarkerKind>(po["kind"].toInt());
            p.x      = po["x"].toInt();
            p.y      = po["y"].toInt();
            p.radius = po["radius"].toInt(8);
            p.w      = po["w"].toInt(16);
            p.h      = po["h"].toInt(16);
            for (const auto& ptv : po["polygon"].toArray()) {
                const QJsonObject pt = ptv.toObject();
                p.polygonPoints.append({pt["x"].toInt(), pt["y"].toInt()});
            }
            tmpl.points.append(p);
        }
        if (!tmpl.name.isEmpty()) out.markerTemplates.append(tmpl);
    }

    auto sourceTypeFromString = [](const QString& s) -> SourceType {
        if (s == QStringLiteral("single_image")) return SourceType::SingleImage;
        if (s == QStringLiteral("archive"))      return SourceType::Archive;
        if (s == QStringLiteral("url"))          return SourceType::Url;
        return SourceType::Folder;
    };

    {
        const QJsonObject layoutInfo = root["layout"].toObject();

        // Read sources array (new model) — highest priority
        // Source paths are saved relative to the project folder.
        const QString sourceBase = projectDir.isEmpty() ? currentFolder : projectDir;
        const QJsonArray sourcesArr = layoutInfo["sources"].toArray();
        if (!sourcesArr.isEmpty()) {
            for (const auto& sVal : sourcesArr) {
                const QJsonObject sObj = sVal.toObject();
                ProjectSource src;
                src.name = sObj["name"].toString();
                src.type = sourceTypeFromString(sObj["type"].toString());
                QString origPath = sObj["originalPath"].toString();
                if (!origPath.isEmpty() && QDir::isRelativePath(origPath) && !sourceBase.isEmpty()) {
                    origPath = QDir(sourceBase).absoluteFilePath(origPath);
                    // Reject traversal attempts that escape the project folder
                    if (!PathSafety::isInsideFolder(origPath, sourceBase)) {
                        origPath.clear();
                    }
                }
                src.originalPath = origPath;
                // The cached folder is deleted when its source is removed, so only accept
                // the places the app creates it: inside the project or the temp folder.
                QString cachedFolder = sObj["cachedFolder"].toString();
                if (!cachedFolder.isEmpty() && QDir::isRelativePath(cachedFolder) && !sourceBase.isEmpty())
                    cachedFolder = QDir::cleanPath(QDir(sourceBase).absoluteFilePath(cachedFolder));
                if (PathSafety::isInsideFolder(cachedFolder, sourceBase)
                        || PathSafety::isInsideFolder(
                               cachedFolder, QStandardPaths::writableLocation(QStandardPaths::TempLocation))) {
                    src.cachedFolderPath = cachedFolder;
                }
                const QJsonArray excArr = sObj["excluded"].toArray();
                src.excludedFiles.reserve(excArr.size());
                for (const auto& e : excArr) src.excludedFiles.append(e.toString());
                const QJsonArray hiddenArr = sObj["hidden_folders"].toArray();
                src.hiddenFolders.reserve(hiddenArr.size());
                for (const auto& h : hiddenArr) src.hiddenFolders.append(h.toString());
                out.sources.append(src);
            }
        } else {
            // Read smart_folders (legacy format)
            const QJsonArray sfArr = layoutInfo["smart_folders"].toArray();
            if (!sfArr.isEmpty()) {
                for (const auto& sfVal : sfArr) {
                    const QJsonObject sfObj = sfVal.toObject();
                    QString path = sfObj["path"].toString();
                    if (!path.isEmpty() && QDir::isRelativePath(path) && !currentFolder.isEmpty()) {
                        path = QDir(currentFolder).absoluteFilePath(path);
                        if (!QDir::cleanPath(path).startsWith(QDir::cleanPath(currentFolder))) {
                            path.clear();
                        }
                    }
                    QStringList excludedFiles;
                    const QJsonArray excArr = sfObj["excluded"].toArray();
                    excludedFiles.reserve(excArr.size());
                    for (const auto& e : excArr) excludedFiles.append(e.toString());
                    // Convert to ProjectSource
                    ProjectSource src;
                    src.name = QFileInfo(path).fileName();
                    src.type = SourceType::Folder;
                    src.originalPath = path;
                    src.excludedFiles = excludedFiles;
                    out.sources.append(src);
                }
            } else {
                // Legacy: convert the "folder" key to a single SmartFolder with no exclusions
                QString legacyFolder = layoutInfo["folder"].toString();
                if (!legacyFolder.isEmpty()) {
                    if (QDir::isRelativePath(legacyFolder) && !currentFolder.isEmpty()) {
                        legacyFolder = QDir(currentFolder).absoluteFilePath(legacyFolder);
                        if (!QDir::cleanPath(legacyFolder).startsWith(QDir::cleanPath(currentFolder))) {
                            legacyFolder.clear();
                        }
                    }
                    // Convert to ProjectSource
                    ProjectSource src;
                    src.name = QFileInfo(legacyFolder).fileName();
                    src.type = SourceType::Folder;
                    src.originalPath = legacyFolder;
                    out.sources.append(src);
                }
            }
        }

        // Read orphaned sprites
        const QJsonArray orphanedArr = root["orphaned_sprites"].toArray();
        out.orphanedSpritePaths.reserve(orphanedArr.size());
        for (const auto& v : orphanedArr) {
            if (v.isString()) {
                out.orphanedSpritePaths.append(v.toString());
            } else if (v.isObject()) {
                const QString p = v.toObject()[QStringLiteral("path")].toString();
                if (!p.isEmpty()) out.orphanedSpritePaths.append(p);
            }
        }
    }

    // --- Multi-atlas deserialization (v4+) ---
    // Helper: parse a JSON timelines array into AnimationTimeline objects
    auto parseTimelines = [](const QJsonArray& arr, int legacyFps) {
        QVector<AnimationTimeline> result;
        result.reserve(arr.size());
        for (const auto& tVal : arr) {
            result.append(timelineFromJson(tVal.toObject(), legacyFps));
        }
        return result;
    };

    const QJsonArray atlasesArr = root["atlases"].toArray();

    if (schemaVersion >= 4 && !atlasesArr.isEmpty()) {
        // v4: read full atlas structure
        out.activeAtlasIndex = root["active_atlas_index"].toInt(0);
        out.atlases.reserve(atlasesArr.size());
        const QDir spDir(currentFolder);
        for (const auto& aVal : atlasesArr) {
            const QJsonObject aObj = aVal.toObject();
            AtlasEntry atlas;
            atlas.id         = aObj["id"].toString();
            atlas.name       = aObj["name"].toString();
            atlas.isNeutral   = aObj["is_neutral"].toBool(false);
            atlas.isExcluded  = aObj["is_excluded"].toBool(false);
            atlas.outputSubdir = aObj["output_subdir"].toString();
            if (!PathSafety::isContainedRelativePath(atlas.outputSubdir))
                atlas.outputSubdir.clear();

            {
                QJsonObject eo = aObj["export_override"].toObject();
                // Deserialize profiles array; fall back to legacy single "profile" field
                const QJsonArray profArr = eo["profiles"].toArray();
                if (!profArr.isEmpty()) {
                    for (const auto& pv : profArr) {
                        if (PathSafety::isPlainName(pv.toString()))
                            atlas.exportConfig.profiles.append(pv.toString());
                    }
                } else {
                    const QString legacyProfile = eo["profile"].toString();
                    if (PathSafety::isPlainName(legacyProfile))
                        atlas.exportConfig.profiles.append(legacyProfile);
                }
                atlas.exportConfig.transform   = readTransform(eo["transform"], schemaVersion);
                atlas.exportConfig.scaleFilter = eo["scale_filter"].toString();
                atlas.exportConfig.colors      = eo["colors"].toInt(0);
                atlas.exportConfig.dither      = eo["dither"].toBool(false);
            }

            // Sprite paths — excluded atlas paths are derived at runtime; skip loading them
            if (!atlas.isExcluded) {
                const QJsonArray spArr = aObj["sprite_paths"].toArray();
                atlas.spritePaths.reserve(spArr.size());
                for (const auto& spVal : spArr) {
                    QString sp = spVal.toString();
                    if (!sp.isEmpty() && QDir::isRelativePath(sp) && !currentFolder.isEmpty()) {
                        sp = spDir.filePath(sp);
                    }
                    if (!sp.isEmpty()) atlas.spritePaths.append(sp);
                }
            }

            atlas.timelines = parseTimelines(aObj["timelines"].toArray(), DEFAULT_ANIMATION_FPS);
            for (const auto& pVal : aObj["puppets"].toArray()) {
                atlas.puppets.append(puppetFromJson(pVal.toObject(), spDir));
                // Projects from before the Rest pose get one.
                PuppetEditing::keepRestPose(atlas.puppets.last());
                PuppetEditing::keepAnimationPosesLast(atlas.puppets.last());
            }

            out.atlases.append(atlas);
        }
        // Compat: if no excluded atlas was stored, append one now
        bool hasExcluded = false;
        for (const auto& a : out.atlases) {
            if (a.isExcluded) { hasExcluded = true; break; }
        }
        if (!hasExcluded) {
            AtlasEntry excl;
            excl.id   = QUuid::createUuid().toString(QUuid::WithoutBraces);
            excl.name = QStringLiteral("Excluded");
            excl.isExcluded = true;
            out.atlases.append(excl);
        }

        // Populate backward-compat timelines field from the active atlas
        if (out.activeAtlasIndex >= 0 && out.activeAtlasIndex < out.atlases.size()) {
            out.timelines = out.atlases[out.activeAtlasIndex].timelines;
        }
    } else {
        // v1-3 migration: wrap existing timelines in a single neutral atlas
        // (The caller — applyProjectPayload — will assign this atlas to the session)
        AtlasEntry neutral;
        neutral.id        = QUuid::createUuid().toString(QUuid::WithoutBraces);
        neutral.name      = QStringLiteral("Default");
        neutral.isNeutral = true;
        neutral.timelines = out.timelines; // already parsed above
        out.atlases.append(neutral);

        AtlasEntry excl;
        excl.id   = QUuid::createUuid().toString(QUuid::WithoutBraces);
        excl.name = QStringLiteral("Excluded");
        excl.isExcluded = true;
        out.atlases.append(excl);
        out.activeAtlasIndex = 0;
    }

    return out;
}
