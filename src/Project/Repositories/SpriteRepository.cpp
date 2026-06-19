#include "SpriteRepository.h"
#include <QDir>
#include <QFileInfo>
#include <QUuid>

SpriteRepository::SpriteRepository(QObject* parent) : QObject(parent) {
    AtlasEntry neutral;
    neutral.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    neutral.name = tr("Default");
    neutral.isNeutral = true;
    m_atlases.append(neutral);

    AtlasEntry excl;
    excl.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    excl.name = tr("Excluded");
    excl.isExcluded = true;
    m_atlases.append(excl);
}

const QVector<AtlasEntry>& SpriteRepository::atlases() const { return m_atlases; }
QVector<AtlasEntry>&       SpriteRepository::atlases()       { return m_atlases; }
int                        SpriteRepository::activeAtlasIndex() const { return m_activeAtlasIndex; }
const QHash<QString, SpritePtr>& SpriteRepository::spriteIndex() const { return m_spriteIndex; }
const QString& SpriteRepository::cachedLayoutOutput() const { return m_cachedLayoutOutput; }
double         SpriteRepository::cachedLayoutScale()  const { return m_cachedLayoutScale; }
const QString& SpriteRepository::lastSuccessfulProfile() const { return m_lastSuccessfulProfile; }
bool           SpriteRepository::lastRunUsedTrim()    const { return m_lastRunUsedTrim; }

AtlasEntry& SpriteRepository::activeAtlas() {
    if (m_activeAtlasIndex < 0 || m_activeAtlasIndex >= m_atlases.size())
        m_activeAtlasIndex = 0;
    if (m_atlases.isEmpty()) {
        AtlasEntry neutral;
        neutral.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        neutral.name = QStringLiteral("Default");
        neutral.isNeutral = true;
        m_atlases.append(neutral);
    }
    return m_atlases[m_activeAtlasIndex];
}

const AtlasEntry& SpriteRepository::activeAtlas() const {
    static AtlasEntry fallback;
    if (m_atlases.isEmpty()) return fallback;
    return m_atlases[qBound(0, m_activeAtlasIndex, m_atlases.size() - 1)];
}

AtlasEntry* SpriteRepository::atlasById(const QString& id) {
    for (auto& a : m_atlases)
        if (a.id == id) return &a;
    return nullptr;
}

AtlasEntry* SpriteRepository::atlasForSprite(const QString& path) {
    const QString abs = QFileInfo(path).absoluteFilePath();
    for (auto& a : m_atlases)
        for (const QString& sp : a.spritePaths)
            if (QFileInfo(sp).absoluteFilePath() == abs)
                return &a;
    return nullptr;
}

int SpriteRepository::neutralAtlasIndex() const {
    for (int i = 0; i < m_atlases.size(); ++i)
        if (m_atlases[i].isNeutral) return i;
    return 0;
}

int SpriteRepository::excludedAtlasIndex() const {
    for (int i = 0; i < m_atlases.size(); ++i)
        if (m_atlases[i].isExcluded) return i;
    return -1;
}

void SpriteRepository::rebuildSpriteIndex() {
    m_spriteIndex.clear();
    for (const auto& atlas : m_atlases)
        for (const auto& model : atlas.layoutModels)
            for (const auto& sp : model.sprites)
                if (sp && !sp->path.isEmpty())
                    m_spriteIndex.insert(QDir::cleanPath(sp->path), sp);

    for (const auto& atlas : m_atlases)
        for (const QString& p : atlas.spritePaths) {
            const QString key = QDir::cleanPath(p);
            if (!m_spriteIndex.contains(key)) {
                auto sp = std::make_shared<Sprite>();
                sp->path = p;
                sp->name = QFileInfo(p).baseName();
                m_spriteIndex.insert(key, sp);
            }
        }
}

void SpriteRepository::setAtlases(const QVector<AtlasEntry>& atlases) {
    m_atlases = atlases;
    rebuildSpriteIndex();
    emit atlasesChanged();
    emit changed();
}

void SpriteRepository::setActiveAtlasIndex(int index) {
    if (m_activeAtlasIndex == index) return;
    m_activeAtlasIndex = index;
    emit activeAtlasChanged(index);
    emit changed();
}

void SpriteRepository::addSpritePaths(const QStringList& paths) {
    if (paths.isEmpty()) return;
    const int neutralIdx = neutralAtlasIndex();
    if (neutralIdx >= 0 && neutralIdx < m_atlases.size()) {
        auto& neutral = m_atlases[neutralIdx];
        for (const QString& p : paths)
            if (!neutral.spritePaths.contains(p))
                neutral.spritePaths.append(p);
    }
    rebuildSpriteIndex();
    emit spritesAdded(paths);
    emit atlasesChanged();
    emit changed();
}

void SpriteRepository::removeSpritePaths(const QStringList& paths) {
    if (paths.isEmpty()) return;
    for (auto& atlas : m_atlases)
        for (const QString& p : paths)
            atlas.spritePaths.removeAll(p);
    cleanupTimelineReferences(paths);
    rebuildSpriteIndex();
    emit spritesRemoved(paths);
    emit atlasesChanged();
    emit changed();
}

void SpriteRepository::setLayoutCache(const QString& output, double scale,
                                       const QString& profile, bool usedTrim) {
    m_cachedLayoutOutput  = output;
    m_cachedLayoutScale   = scale;
    m_lastSuccessfulProfile = profile;
    m_lastRunUsedTrim     = usedTrim;
    emit layoutCacheUpdated();
    emit changed();
}

void SpriteRepository::cleanupTimelineReferences(const QStringList& paths) {
    for (auto& atlas : m_atlases)
        for (auto& timeline : atlas.timelines)
            for (const QString& p : paths)
                timeline.frames.removeAll(p);
}

void SpriteRepository::clear() {
    m_atlases.clear();

    AtlasEntry neutral;
    neutral.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    neutral.name = QStringLiteral("Default");
    neutral.isNeutral = true;
    m_atlases.append(neutral);

    AtlasEntry excl;
    excl.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    excl.name = QStringLiteral("Excluded");
    excl.isExcluded = true;
    m_atlases.append(excl);

    m_activeAtlasIndex = 0;
    m_spriteIndex.clear();
    m_cachedLayoutOutput.clear();
    m_cachedLayoutScale = 1.0;
    m_lastSuccessfulProfile.clear();
    m_lastRunUsedTrim = false;

    emit atlasesChanged();
    emit changed();
}
