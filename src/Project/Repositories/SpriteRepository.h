#pragma once
#include <QObject>
#include <QHash>
#include <QStringList>
#include <QVector>
#include "ProjectModels.h"

/**
 * @class SpriteRepository
 * @brief Owns atlas entries (including embedded timelines), the sprite index,
 *        and the layout cache produced by the CLI.
 *
 * Mutation methods keep the sprite index consistent and emit focused signals
 * so workspaces only react to what changed.
 *
 * Cross-repository note: removeSpritePaths() also cleans up timeline frame
 * references, since timelines are embedded inside AtlasEntry. This is
 * intentional — callers only need to call one method.
 */
class SpriteRepository : public QObject {
    Q_OBJECT
public:
    explicit SpriteRepository(QObject* parent = nullptr);

    // --- Atlas access ---
    const QVector<AtlasEntry>& atlases() const;
    QVector<AtlasEntry>&       atlases();
    int activeAtlasIndex() const;

    AtlasEntry&       activeAtlas();
    const AtlasEntry& activeAtlas() const;
    AtlasEntry*       atlasById(const QString& id);
    AtlasEntry*       atlasForSprite(const QString& path);
    int               neutralAtlasIndex() const;
    int               excludedAtlasIndex() const;

    // --- Sprite index ---
    const QHash<QString, SpritePtr>& spriteIndex() const;
    void rebuildSpriteIndex();

    // --- Layout cache ---
    const QString& cachedLayoutOutput() const;
    double         cachedLayoutScale() const;
    const QString& lastSuccessfulProfile() const;
    bool           lastRunUsedTrim() const;

    // --- Mutators ---
    void setAtlases(const QVector<AtlasEntry>& atlases);
    void setActiveAtlasIndex(int index);

    /** Adds paths to the neutral atlas and rebuilds the sprite index. */
    void addSpritePaths(const QStringList& paths);

    /**
     * Removes paths from all atlases, cleans up timeline frame references
     * (since timelines are embedded in AtlasEntry), and rebuilds the sprite index.
     */
    void removeSpritePaths(const QStringList& paths);

    void setLayoutCache(const QString& output, double scale,
                        const QString& profile, bool usedTrim);

    void clear();

signals:
    void spritesAdded(QStringList paths);
    void spritesRemoved(QStringList paths);
    void spritesModified(QStringList paths);
    void atlasesChanged();
    void activeAtlasChanged(int index);
    void timelinesChanged();
    void layoutCacheUpdated();
    void changed();

private:
    /** Removes paths from all timeline frame lists across all atlases. */
    void cleanupTimelineReferences(const QStringList& paths);

    QVector<AtlasEntry>      m_atlases;
    int                      m_activeAtlasIndex = 0;
    QHash<QString, SpritePtr> m_spriteIndex;
    QString                  m_cachedLayoutOutput;
    double                   m_cachedLayoutScale = 1.0;
    QString                  m_lastSuccessfulProfile;
    bool                     m_lastRunUsedTrim = false;
};
