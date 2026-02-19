#include "ProjectPayloadCodec.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

namespace {
QString normalizedMarkerName(QString name) {
    name = name.trimmed();
    if (name.compare("pivot", Qt::CaseInsensitive) == 0) {
        return "pivot";
    }
    return name;
}
}

QJsonObject ProjectPayloadCodec::build(const ProjectPayloadBuildInput& input) {
    QJsonObject root;
    root["version"] = 1;

    QJsonObject layoutOpts;
    layoutOpts["profile"] = input.profile;
    layoutOpts["padding"] = input.padding;
    layoutOpts["trim_transparent"] = input.trimTransparent;
    root["layout_options"] = layoutOpts;

    QJsonObject layoutInfo;
    layoutInfo["folder"] = input.currentFolder;
    layoutInfo["scale"] = input.layoutScale;
    layoutInfo["output"] = input.layoutOutput;
    root["layout"] = layoutInfo;

    QJsonObject animInfo;
    animInfo["animation_fps"] = input.animationFps;
    animInfo["selected_timeline_index"] = input.selectedTimelineIndex;
    QJsonArray timelinesArr;
    for (const auto& t : input.timelines) {
        QJsonObject tObj;
        tObj["name"] = t.name;
        QJsonArray framesArr;
        for (const auto& f : t.frames) {
            framesArr.append(f);
        }
        tObj["frames"] = framesArr;
        timelinesArr.append(tObj);
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
    markersInfo["selected_marker_name"] = input.selectedPointName;
    root["spritemarkers"] = markersInfo;

    QJsonObject uiOpts;
    uiOpts["layout_zoom"] = input.layoutZoom;
    uiOpts["preview_zoom"] = input.previewZoom;
    uiOpts["animation_zoom"] = input.animationZoom;
    root["ui_options"] = uiOpts;

    QJsonObject saveOpts;
    saveOpts["destination"] = input.saveConfig.destination;
    saveOpts["transform"] = input.saveConfig.transform;
    QJsonArray scalesArr;
    for (const auto& sc : input.saveConfig.scales) {
        QJsonObject scObj;
        scObj["name"] = sc.name;
        scObj["value"] = sc.value;
        scalesArr.append(scObj);
    }
    saveOpts["scales"] = scalesArr;
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
    out.animationFps = animInfo["animation_fps"].toInt(8);
    QJsonArray timelinesArr = animInfo["timelines"].toArray();
    for (const auto& tVal : timelinesArr) {
        QJsonObject tObj = tVal.toObject();
        AnimationTimeline t;
        t.name = tObj["name"].toString();
        QJsonArray framesArr = tObj["frames"].toArray();
        for (const auto& fVal : framesArr) {
            t.frames.append(fVal.toString());
        }
        out.timelines.append(t);
    }
    return out;
}
