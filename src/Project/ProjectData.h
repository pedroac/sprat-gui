#pragma once
#include <QObject>
#include "Repositories/SpriteRepository.h"
#include "Repositories/SourceRepository.h"
#include "Repositories/MarkerRepository.h"
#include "Repositories/ExportRepository.h"

/**
 * @class ProjectData
 * @brief Container for all project repositories.
 *
 * Owns SpriteRepository, SourceRepository, MarkerRepository, and
 * ExportRepository. The repositories are connected to each other in
 * connectCrossRepoSignals():
 *
 *   SourceRepository::framePathsAdded   → SpriteRepository::addSpritePaths
 *   SourceRepository::framePathsRemoved → SpriteRepository::removeSpritePaths
 *
 * SpriteRepository::removeSpritePaths additionally purges removed paths from
 * all embedded timeline frame lists, so callers only need one call.
 *
 * Intended to replace ProjectSession as the authoritative project state holder.
 * Workspaces should hold a pointer to the specific repository they need rather
 * than to ProjectData itself.
 */
class ProjectData : public QObject {
    Q_OBJECT
public:
    explicit ProjectData(QObject* parent = nullptr);

    SpriteRepository* sprites() const { return m_sprites; }
    SourceRepository* sources() const { return m_sources; }
    MarkerRepository* markers() const { return m_markers; }
    ExportRepository* exports() const { return m_exports; }

    void clear();
    bool isEmpty() const;

signals:
    /** Re-emitted whenever any repository emits its own changed() signal. */
    void changed();

private:
    /**
     * Wires cross-repository signals.
     * This is the single place to add new cross-repo connections.
     */
    void connectCrossRepoSignals();

    SpriteRepository* m_sprites;
    SourceRepository* m_sources;
    MarkerRepository* m_markers;
    ExportRepository* m_exports;
};
