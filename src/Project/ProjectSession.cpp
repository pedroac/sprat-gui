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

    layoutModels.clear();
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
    return activeFramePaths.isEmpty() && layoutModels.isEmpty();
}

void ProjectSession::addTempDir(std::unique_ptr<QTemporaryDir> dir) {
    m_tempDirs.push_back(std::move(dir));
}

void ProjectSession::clearTempDirs() {
    m_tempDirs.clear();
}
