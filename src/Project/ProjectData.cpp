#include "ProjectData.h"

ProjectData::ProjectData(QObject* parent)
    : QObject(parent)
    , m_sprites(new SpriteRepository(this))
    , m_sources(new SourceRepository(this))
    , m_markers(new MarkerRepository(this))
    , m_exports(new ExportRepository(this))
{
    connectCrossRepoSignals();
}

void ProjectData::connectCrossRepoSignals() {
    // Bubble all repository changes up to ProjectData::changed
    connect(m_sprites, &SpriteRepository::changed,              this, &ProjectData::changed);
    connect(m_sources, &SourceRepository::changed,              this, &ProjectData::changed);
    connect(m_markers, &MarkerRepository::markerTemplatesChanged, this, &ProjectData::changed);
    connect(m_exports, &ExportRepository::exportPresetsChanged,  this, &ProjectData::changed);

    // Frame paths added to sources → register them in the neutral atlas
    connect(m_sources, &SourceRepository::framePathsAdded,
            m_sprites, &SpriteRepository::addSpritePaths);

    // Frame paths removed from sources → remove from all atlases and clean
    // up timeline frame references (SpriteRepository handles that internally)
    connect(m_sources, &SourceRepository::framePathsRemoved,
            m_sprites, &SpriteRepository::removeSpritePaths);
}

void ProjectData::clear() {
    // Block individual changed() emissions during clear to emit only once
    blockSignals(true);
    m_sprites->clear();
    m_sources->clear();
    m_markers->clear();
    m_exports->clear();
    blockSignals(false);
    emit changed();
}

bool ProjectData::isEmpty() const {
    return m_sources->activeFramePaths().isEmpty()
        && m_sprites->activeAtlas().layoutModels.isEmpty();
}
