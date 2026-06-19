#include "SourceRepository.h"

SourceRepository::SourceRepository(QObject* parent) : QObject(parent) {}

const QString&              SourceRepository::currentFolder()       const { return m_currentFolder; }
const QString&              SourceRepository::layoutSourcePath()    const { return m_layoutSourcePath; }
bool                        SourceRepository::layoutSourceIsList()  const { return m_layoutSourceIsList; }
const QString&              SourceRepository::sourceFolder()        const { return m_sourceFolder; }
const QVector<SmartFolder>& SourceRepository::smartFolders()        const { return m_smartFolders; }
const QVector<ProjectSource>& SourceRepository::sources()           const { return m_sources; }
QVector<ProjectSource>&       SourceRepository::sources()                  { return m_sources; }
const QStringList&          SourceRepository::activeFramePaths()    const { return m_activeFramePaths; }
const QString&              SourceRepository::frameListPath()       const { return m_frameListPath; }
const QStringList&          SourceRepository::orphanedSpritePaths() const { return m_orphanedSpritePaths; }

void SourceRepository::setCurrentFolder(const QString& folder) {
    if (m_currentFolder == folder) return;
    m_currentFolder = folder;
    emit currentFolderChanged(folder);
    emit changed();
}

void SourceRepository::setLayoutSource(const QString& path, bool isList) {
    m_layoutSourcePath  = path;
    m_layoutSourceIsList = isList;
    emit changed();
}

void SourceRepository::setSourceFolder(const QString& folder) {
    m_sourceFolder = folder;
    emit changed();
}

void SourceRepository::setSmartFolders(const QVector<SmartFolder>& folders) {
    m_smartFolders = folders;
    emit sourcesChanged();
    emit changed();
}

void SourceRepository::setSources(const QVector<ProjectSource>& sources) {
    m_sources = sources;
    emit sourcesChanged();
    emit changed();
}

void SourceRepository::setActiveFramePaths(const QStringList& paths) {
    m_activeFramePaths = paths;
    emit activeFramePathsChanged();
    emit changed();
}

void SourceRepository::setFrameListPath(const QString& path) {
    m_frameListPath = path;
    emit changed();
}

void SourceRepository::setOrphanedSpritePaths(const QStringList& paths) {
    m_orphanedSpritePaths = paths;
    emit changed();
}

void SourceRepository::addFramePaths(const QStringList& paths) {
    if (paths.isEmpty()) return;
    QStringList added;
    for (const QString& p : paths) {
        if (!m_activeFramePaths.contains(p)) {
            m_activeFramePaths.append(p);
            added.append(p);
        }
    }
    if (added.isEmpty()) return;
    emit framePathsAdded(added);
    emit activeFramePathsChanged();
    emit changed();
}

void SourceRepository::removeFramePaths(const QStringList& paths) {
    if (paths.isEmpty()) return;
    QStringList removed;
    for (const QString& p : paths) {
        if (m_activeFramePaths.removeAll(p) > 0)
            removed.append(p);
    }
    if (removed.isEmpty()) return;
    emit framePathsRemoved(removed);
    emit activeFramePathsChanged();
    emit changed();
}

void SourceRepository::addSource(const ProjectSource& source) {
    m_sources.append(source);
    emit sourceAdded(m_sources.size() - 1);
    emit sourcesChanged();
    emit changed();
}

void SourceRepository::removeSource(int index) {
    if (index < 0 || index >= m_sources.size()) return;
    const ProjectSource removed = m_sources.takeAt(index);
    emit sourceRemoved(index, removed);
    emit sourcesChanged();
    emit changed();
}

void SourceRepository::clear() {
    m_currentFolder.clear();
    m_layoutSourcePath.clear();
    m_layoutSourceIsList = false;
    m_sourceFolder.clear();
    m_smartFolders.clear();
    m_sources.clear();
    m_activeFramePaths.clear();
    m_frameListPath.clear();
    m_orphanedSpritePaths.clear();
    emit sourcesChanged();
    emit activeFramePathsChanged();
    emit changed();
}
