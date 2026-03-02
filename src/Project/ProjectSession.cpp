#include "ProjectSession.h"

ProjectSession::ProjectSession(QObject* parent) : QObject(parent) {
}

void ProjectSession::clear() {
    clearTempDirs();
    currentFolder.clear();
    layoutSourcePath.clear();
    layoutSourceIsList = false;
    activeFramePaths.clear();
    frameListPath.clear();

    layoutModel.sprites.clear();
    layoutModel.atlasWidth = 0;
    layoutModel.atlasHeight = 0;
    layoutModel.scale = 1.0;
    cachedLayoutOutput.clear();
    cachedLayoutScale = 1.0;
    lastSuccessfulProfile.clear();
    lastRunUsedTrim = false;

    timelines.clear();
    selectedTimelineIndex = -1;

    selectedSprite.reset();
    selectedSprites.clear();
    selectedPointName.clear();

    pendingProjectPayload = QJsonObject();

    emit changed();
}

bool ProjectSession::isEmpty() const {
    return activeFramePaths.isEmpty() && layoutModel.sprites.isEmpty();
}

void ProjectSession::addTempDir(std::unique_ptr<QTemporaryDir> dir) {
    m_tempDirs.push_back(std::move(dir));
}

void ProjectSession::clearTempDirs() {
    m_tempDirs.clear();
}
