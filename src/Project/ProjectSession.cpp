#include "ProjectSession.h"
#include <QDir>
#include <QUuid>
#include <QFileInfo>

ProjectSession::ProjectSession(QObject* parent) : QObject(parent) {
    // Initialize with one neutral atlas followed by the excluded atlas
    AtlasEntry neutral;
    neutral.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    neutral.name = tr("Default");
    neutral.isNeutral = true;
    atlases.append(neutral);

    AtlasEntry excl;
    excl.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    excl.name = tr("Excluded");
    excl.isExcluded = true;
    atlases.append(excl);
}

void ProjectSession::clear() {
    currentFolder.clear();
    layoutSourcePath.clear();
    layoutSourceIsList = false;
    activeFramePaths.clear();
    frameListPath.clear();

    // Reset to neutral atlas + excluded atlas
    atlases.clear();
    AtlasEntry neutral;
    neutral.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    neutral.name = QStringLiteral("Default");
    neutral.isNeutral = true;
    atlases.append(neutral);

    AtlasEntry excl;
    excl.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    excl.name = QStringLiteral("Excluded");
    excl.isExcluded = true;
    atlases.append(excl);
    activeAtlasIndex = 0;

    cachedLayoutOutput.clear();
    cachedLayoutScale = 1.0;
    lastSuccessfulProfile.clear();
    lastRunUsedTrim = false;

    selectedTimelineIndex = -1;

    selectedSprite.reset();
    selectedSprites.clear();
    selectedPointName.clear();

    pendingProjectPayload = QJsonObject();
    spriteIndex.clear();

    emit atlasesChanged();
    emit changed();
}

bool ProjectSession::isEmpty() const {
    return activeFramePaths.isEmpty() && activeAtlas().layoutModels.isEmpty();
}

AtlasEntry& ProjectSession::activeAtlas() {
    if (activeAtlasIndex < 0 || activeAtlasIndex >= atlases.size()) {
        activeAtlasIndex = 0;
    }
    if (atlases.isEmpty()) {
        AtlasEntry neutral;
        neutral.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        neutral.name = QStringLiteral("Default");
        neutral.isNeutral = true;
        atlases.append(neutral);
    }
    return atlases[activeAtlasIndex];
}

const AtlasEntry& ProjectSession::activeAtlas() const {
    static AtlasEntry fallback;
    if (atlases.isEmpty()) return fallback;
    const int idx = qBound(0, activeAtlasIndex, atlases.size() - 1);
    return atlases[idx];
}

AtlasEntry* ProjectSession::atlasById(const QString& id) {
    for (auto& a : atlases) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

AtlasEntry* ProjectSession::atlasForSprite(const QString& path) {
    const QString abs = QFileInfo(path).absoluteFilePath();
    for (auto& a : atlases) {
        for (const QString& sp : a.spritePaths) {
            if (QFileInfo(sp).absoluteFilePath() == abs)
                return &a;
        }
    }
    return nullptr;
}

int ProjectSession::neutralAtlasIndex() const {
    for (int i = 0; i < atlases.size(); ++i) {
        if (atlases[i].isNeutral) return i;
    }
    return 0;
}

void ProjectSession::rebuildSpriteIndex() {
    spriteIndex.clear();
    // Collect all SpritePtr from layoutModels (have full rect data)
    for (const auto& atlas : atlases) {
        for (const auto& model : atlas.layoutModels) {
            for (const auto& sp : model.sprites) {
                if (sp && !sp->path.isEmpty())
                    spriteIndex.insert(QDir::cleanPath(sp->path), sp);
            }
        }
    }
    // Ensure all spritePaths across atlases have at least a minimal entry
    for (const auto& atlas : atlases) {
        for (const QString& p : atlas.spritePaths) {
            const QString key = QDir::cleanPath(p);
            if (!spriteIndex.contains(key)) {
                auto sp = std::make_shared<Sprite>();
                sp->path = p;
                sp->name = QFileInfo(p).baseName();
                spriteIndex.insert(key, sp);
            }
        }
    }
}

int ProjectSession::excludedAtlasIndex() const {
    for (int i = 0; i < atlases.size(); ++i) {
        if (atlases[i].isExcluded) return i;
    }
    return -1;
}
