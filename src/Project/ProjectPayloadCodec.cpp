#include "ProjectPayloadCodec.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QColor>
#include <QStringList>

namespace {
QString normalizedMarkerName(QString name) {
    name = name.trimmed();
    if (name.compare("pivot", Qt::CaseInsensitive) == 0) {
        return "pivot";
    }
    return name;
}

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

QString formatResolution(int width, int height) {
    return QString("%1x%2").arg(width).arg(height);
}

bool parseResolution(const QString& value, int& width, int& height) {
    const QString normalized = value.trimmed().toLower();
    const QStringList parts = normalized.split('x', Qt::SkipEmptyParts);
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

QJsonObject ProjectPayloadCodec::build(const ProjectPayloadBuildInput& input) {
    QJsonObject root;
    root["version"] = 1;

    QJsonObject layoutOpts;
    layoutOpts["profile"] = input.profile;
    layoutOpts["padding"] = input.padding;
    layoutOpts["trim_transparent"] = input.trimTransparent;
    layoutOpts["scale"] = input.layoutOptionScale;
    if (input.sourceResolutionWidth > 0 && input.sourceResolutionHeight > 0) {
        layoutOpts["source_resolution"] = formatResolution(input.sourceResolutionWidth, input.sourceResolutionHeight);
    }
    root["layout_options"] = layoutOpts;

    QJsonObject layoutInfo;
    layoutInfo["folder"] = input.currentFolder;
    layoutInfo["scale"] = input.layoutScale;
    layoutInfo["output"] = input.layoutOutput;
    layoutInfo["source_mode"] = input.layoutSourceIsList ? "list" : "folder";
    if (!input.activeFramePaths.isEmpty()) {
        QJsonArray framePaths;
        for (const QString& path : input.activeFramePaths) {
            framePaths.append(path);
        }
        layoutInfo["frame_paths"] = framePaths;
    }
    root["layout"] = layoutInfo;

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
    for (const auto& s : input.layoutModel.sprites) {
        QJsonObject sObj;
        sObj["name"] = s->name;
        sObj["pivot_x"] = s->pivotX;
        sObj["pivot_y"] = s->pivotY;
        QJsonArray markersArr;
        for (const auto& p : s->points) {
            QJsonObject mObj;
            const QString kind = markerKindToString(p.kind);
            mObj["name"] = normalizedMarkerName(p.name);
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

        QString key = QDir(input.currentFolder).relativeFilePath(s->path);
        if (key.isEmpty()) {
            key = QFileInfo(s->path).fileName();
        }
        spritesState[key] = sObj;
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
    if (!input.leftSplitterSizes.isEmpty()) {
        uiOpts["left_splitter_sizes"] = toJsonArray(input.leftSplitterSizes);
    }
    if (!input.rightSplitterSizes.isEmpty()) {
        uiOpts["right_splitter_sizes"] = toJsonArray(input.rightSplitterSizes);
    }
    root["ui_options"] = uiOpts;

    QJsonObject settings;
    settings["canvas_color"] = input.appSettings.canvasColor.name(QColor::HexArgb);
    settings["frame_color"] = input.appSettings.frameColor.name(QColor::HexArgb);
    settings["show_borders"] = input.appSettings.showBorders;
    settings["border_color"] = input.appSettings.borderColor.name(QColor::HexArgb);
    settings["border_style"] = static_cast<int>(input.appSettings.borderStyle);
    settings["cli_spratlayout"] = input.cliPaths.layoutBinary;
    settings["cli_spratpack"] = input.cliPaths.packBinary;
    settings["cli_spratconvert"] = input.cliPaths.convertBinary;
    root["settings"] = settings;

    QJsonObject saveOpts;
    saveOpts["destination"] = input.saveConfig.destination;
    saveOpts["transform"] = input.saveConfig.transform;
    QJsonArray profilesArr;
    for (const QString& profileName : input.saveConfig.profiles) {
        profilesArr.append(profileName);
    }
    saveOpts["profiles"] = profilesArr;
    root["save_options"] = saveOpts;

    return root;
}

ProjectPayloadApplyResult ProjectPayloadCodec::applyToLayout(const QJsonObject& root, const QString& currentFolder, LayoutModel& layoutModel) {
    ProjectPayloadApplyResult out;

    QJsonObject markersInfo = root["spritemarkers"].toObject();
    QJsonObject spritesState = markersInfo["sprites"].toObject();
    for (auto& sprite : layoutModel.sprites) {
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
        if (state.contains("pivot_x")) {
            sprite->pivotX = state["pivot_x"].toInt();
        }
        if (state.contains("pivot_y")) {
            sprite->pivotY = state["pivot_y"].toInt();
        }
        if (!state.contains("markers")) {
            continue;
        }
        sprite->points.clear();
        QJsonArray markersArr = state["markers"].toArray();
        for (const auto& mVal : markersArr) {
            QJsonObject mObj = mVal.toObject();
            NamedPoint p;
            p.name = normalizedMarkerName(mObj["name"].toString());
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
                for (const auto& ptVal : polyArr) {
                    QJsonArray ptPair = ptVal.toArray();
                    if (ptPair.size() == 2) {
                        p.polygonPoints.append(QPoint(ptPair[0].toInt(), ptPair[1].toInt()));
                    }
                }
            } else if (mObj.contains("vertices")) {
                QJsonArray verticesArr = mObj["vertices"].toArray();
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

    QJsonObject animInfo = root["animations"].toObject();
    out.selectedTimelineIndex = animInfo["selected_timeline_index"].toInt(-1);
    out.selectedTimelineFrameRows = fromJsonArray(animInfo["selected_frame_rows"]);
    out.animationFrameIndex = animInfo["animation_frame_index"].toInt(0);
    out.animationPlaying = animInfo["animation_playing"].toBool(false);
    const int legacyAnimationFps = animInfo["animation_fps"].toInt(8);
    QJsonArray timelinesArr = animInfo["timelines"].toArray();
    for (const auto& tVal : timelinesArr) {
        QJsonObject tObj = tVal.toObject();
        AnimationTimeline t;
        t.name = tObj["name"].toString();
        t.fps = tObj["fps"].toInt(legacyAnimationFps);
        if (t.fps <= 0) {
            t.fps = 8;
        }
        QJsonArray framesArr = tObj["frames"].toArray();
        for (const auto& fVal : framesArr) {
            t.frames.append(fVal.toString());
        }
        out.timelines.append(t);
    }

    out.selectedSpritePath = markersInfo["selected_sprite_path"].toString();
    {
        const QJsonArray selectedSprites = markersInfo["selected_sprite_paths"].toArray();
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
        if (parseResolution(layoutOpts["source_resolution"].toString(), sourceWidth, sourceHeight)) {
            out.sourceResolutionWidth = sourceWidth;
            out.sourceResolutionHeight = sourceHeight;
        }
        const double scale = layoutOpts["scale"].toDouble(1.0);
        out.layoutOptionScale = (scale > 0.0 && scale <= 1.0) ? scale : 1.0;
    }

    QJsonObject uiOpts = root["ui_options"].toObject();
    out.layoutZoom = uiOpts["layout_zoom"].toDouble(1.0);
    out.previewZoom = uiOpts["preview_zoom"].toDouble(1.0);
    out.animationZoom = uiOpts["animation_zoom"].toDouble(1.0);
    out.leftSplitterSizes = fromJsonArray(uiOpts["left_splitter_sizes"]);
    out.rightSplitterSizes = fromJsonArray(uiOpts["right_splitter_sizes"]);

    QJsonObject settings = root["settings"].toObject();
    {
        const QString canvasColor = settings["canvas_color"].toString();
        if (!canvasColor.isEmpty()) {
            out.appSettings.canvasColor = QColor(canvasColor);
        }
        const QString frameColor = settings["frame_color"].toString();
        if (!frameColor.isEmpty()) {
            out.appSettings.frameColor = QColor(frameColor);
        }
        out.appSettings.showBorders = settings["show_borders"].toBool(out.appSettings.showBorders);
        const QString borderColor = settings["border_color"].toString();
        if (!borderColor.isEmpty()) {
            out.appSettings.borderColor = QColor(borderColor);
        }
        out.appSettings.borderStyle = static_cast<Qt::PenStyle>(settings["border_style"].toInt(static_cast<int>(out.appSettings.borderStyle)));
        out.cliPaths.layoutBinary = settings["cli_spratlayout"].toString();
        out.cliPaths.packBinary = settings["cli_spratpack"].toString();
        out.cliPaths.convertBinary = settings["cli_spratconvert"].toString();
    }
    return out;
}
