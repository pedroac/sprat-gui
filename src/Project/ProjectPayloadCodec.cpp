#include "ProjectPayloadCodec.h"
#include "MarkerUtils.h"
#include "ResolutionUtils.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QColor>

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
    root["schema_version"] = 3;
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
                sObj["cachedFolder"] = src.cachedFolderPath;
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
        timelinesArr.append(tObj);
    }
    if (!input.timelines.isEmpty()) {
        // Keep legacy field for older consumers that still expect one global fps.
        animInfo["animation_fps"] = input.timelines.first().fps;
    }
    animInfo["timelines"] = timelinesArr;
    root["animations"] = animInfo;

    QJsonObject markersInfo;
    QJsonObject spritesState;
    for (const auto& model : input.layoutModels) {
        for (const auto& s : model.sprites) {
            QJsonObject sObj;
            sObj["name"] = s->name;
            sObj["has_pivot"] = true;
            sObj["pivot_x"] = s->pivotX;
            sObj["pivot_y"] = s->pivotY;
            QJsonArray markersArr;
            for (const auto& p : s->points) {
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

            if (!s->aliases.isEmpty()) {
                QJsonArray aliasArr;
                for (const auto& a : s->aliases) aliasArr.append(a);
                sObj["aliases"] = aliasArr;
            }

            QString key = QDir(input.currentFolder).relativeFilePath(s->path);
            if (key.isEmpty()) {
                key = QFileInfo(s->path).fileName();
            }
            spritesState[key] = sObj;
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
    saveOpts["sync_sprites"] = input.saveConfig.syncSprites;
    root["save_options"] = saveOpts;

    root["complete"] = true;
    return root;
}

ProjectPayloadApplyResult ProjectPayloadCodec::applyToLayout(const QJsonObject& root, const QString& currentFolder, QVector<LayoutModel>& layoutModels) {
    ProjectPayloadApplyResult out;
    if (!root.value(QStringLiteral("complete")).toBool()) {
        qWarning() << "ProjectPayloadCodec: project file may be incomplete or corrupted (missing 'complete' sentinel)";
    }

    QJsonObject markersInfo = root["spritemarkers"].toObject();
    QJsonObject spritesState = markersInfo["sprites"].toObject();
    for (auto& model : layoutModels) {
        for (auto& sprite : model.sprites) {
            QString key = QDir(currentFolder).relativeFilePath(sprite->path);
            if (!spritesState.contains(key)) {
                key = QFileInfo(sprite->path).fileName();
            }
            if (!spritesState.contains(key)) {
                continue;
            }
            QJsonObject state = spritesState[key].toObject();
            if (state.contains("name")) {
                sprite->name = state["name"].toString();
            }
            sprite->aliases.clear();
            for (const auto& a : state["aliases"].toArray())
                sprite->aliases.append(a.toString());
            // Only apply stored pivot if the project was saved with the has_pivot flag.
            // Older projects (no flag, or pivot_y=0 from partial centering) are ignored
            // so LayoutParser's correctly-centered values are used instead.
            if (state["has_pivot"].toBool() && state.contains("pivot_x") && state.contains("pivot_y")) {
                sprite->pivotX = state["pivot_x"].toInt();
                sprite->pivotY = state["pivot_y"].toInt();
            }
            if (!state.contains("markers")) {
                continue;
            }
            sprite->points.clear();
            QJsonArray markersArr = state["markers"].toArray();
            sprite->points.reserve(markersArr.size());
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
                sprite->points.append(p);
            }
        }
    }

    QJsonObject animInfo = root["animations"].toObject();
    out.selectedTimelineIndex = animInfo["selected_timeline_index"].toInt(-1);
    out.selectedTimelineFrameRows = fromJsonArray(animInfo["selected_frame_rows"]);
    out.animationFrameIndex = animInfo["animation_frame_index"].toInt(0);
    out.animationPlaying = animInfo["animation_playing"].toBool(false);
    const int legacyAnimationFps = animInfo["animation_fps"].toInt(8);
    QJsonArray timelinesArr = animInfo["timelines"].toArray();
    out.timelines.reserve(timelinesArr.size());
    for (const auto& tVal : timelinesArr) {
        QJsonObject tObj = tVal.toObject();
        AnimationTimeline t;
        t.name = tObj["name"].toString();
        t.fps = tObj["fps"].toInt(legacyAnimationFps);
        if (t.fps <= 0) {
            t.fps = 8;
        }
        QJsonArray framesArr = tObj["frames"].toArray();
        t.frames.reserve(framesArr.size());
        for (const auto& fVal : framesArr) {
            t.frames.append(fVal.toString());
        }
        t.aliasOf = tObj["alias_of"].toString();
        t.hFlip   = tObj["h_flip"].toBool(false);
        t.vFlip   = tObj["v_flip"].toBool(false);
        out.timelines.append(t);
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

    QJsonObject saveOpts = root["save_options"].toObject();
    out.saveConfig.destination = saveOpts["destination"].toString();
    out.saveConfig.outputPath = saveOpts["output_path"].toString();
    out.saveConfig.transform = saveOpts["transform"].toString();
    QJsonArray profilesArr = saveOpts["profiles"].toArray();
    out.saveConfig.profiles.reserve(profilesArr.size());
    for (const auto& pVal : profilesArr) {
        out.saveConfig.profiles.append(pVal.toString());
    }
    out.saveConfig.syncSprites = saveOpts["sync_sprites"].toBool(false);

    auto sourceTypeFromString = [](const QString& s) -> SourceType {
        if (s == QStringLiteral("single_image")) return SourceType::SingleImage;
        if (s == QStringLiteral("archive"))      return SourceType::Archive;
        if (s == QStringLiteral("url"))          return SourceType::Url;
        return SourceType::Folder;
    };

    {
        const QJsonObject layoutInfo = root["layout"].toObject();

        // Read sources array (new model) — highest priority
        const QJsonArray sourcesArr = layoutInfo["sources"].toArray();
        if (!sourcesArr.isEmpty()) {
            for (const auto& sVal : sourcesArr) {
                const QJsonObject sObj = sVal.toObject();
                ProjectSource src;
                src.name = sObj["name"].toString();
                src.type = sourceTypeFromString(sObj["type"].toString());
                QString origPath = sObj["originalPath"].toString();
                if (!origPath.isEmpty() && QDir::isRelativePath(origPath) && !currentFolder.isEmpty()) {
                    origPath = QDir(currentFolder).absoluteFilePath(origPath);
                    // Reject traversal attempts that escape the project folder
                    if (!QDir::cleanPath(origPath).startsWith(QDir::cleanPath(currentFolder))) {
                        origPath.clear();
                    }
                }
                src.originalPath = origPath;
                src.cachedFolderPath = sObj["cachedFolder"].toString();
                const QJsonArray excArr = sObj["excluded"].toArray();
                src.excludedFiles.reserve(excArr.size());
                for (const auto& e : excArr) src.excludedFiles.append(e.toString());
                const QJsonArray hiddenArr = sObj["hidden_folders"].toArray();
                src.hiddenFolders.reserve(hiddenArr.size());
                for (const auto& h : hiddenArr) src.hiddenFolders.append(h.toString());
                out.sources.append(src);
            }
            // Derive smart_folders from sources for backward-compat consumers
            for (const auto& src : out.sources) {
                if (src.type == SourceType::Folder) {
                    SmartFolder sf;
                    sf.path = src.originalPath;
                    sf.excludedFiles = src.excludedFiles;
                    out.smartFolders.append(sf);
                }
            }
        } else {
            // Read smart_folders (legacy format)
            const QJsonArray sfArr = layoutInfo["smart_folders"].toArray();
            if (!sfArr.isEmpty()) {
                for (const auto& sfVal : sfArr) {
                    const QJsonObject sfObj = sfVal.toObject();
                    SmartFolder sf;
                    QString path = sfObj["path"].toString();
                    if (!path.isEmpty() && QDir::isRelativePath(path) && !currentFolder.isEmpty()) {
                        path = QDir(currentFolder).absoluteFilePath(path);
                        if (!QDir::cleanPath(path).startsWith(QDir::cleanPath(currentFolder))) {
                            path.clear();
                        }
                    }
                    sf.path = path;
                    const QJsonArray excArr = sfObj["excluded"].toArray();
                    sf.excludedFiles.reserve(excArr.size());
                    for (const auto& e : excArr) sf.excludedFiles.append(e.toString());
                    out.smartFolders.append(sf);
                    // Convert to ProjectSource
                    ProjectSource src;
                    src.name = QFileInfo(path).fileName();
                    src.type = SourceType::Folder;
                    src.originalPath = path;
                    src.excludedFiles = sf.excludedFiles;
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
                    SmartFolder sf;
                    sf.path = legacyFolder;
                    out.smartFolders.append(sf);
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

    return out;
}
