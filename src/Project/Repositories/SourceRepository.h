#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include "ProjectModels.h"

/**
 * @class SourceRepository
 * @brief Owns source folders, project sources, and the active frame path list.
 *
 * When frame paths are added or removed via addFramePaths() / removeFramePaths(),
 * ProjectData connects those signals to SpriteRepository::addSpritePaths() /
 * removeSpritePaths() so the atlas stays in sync automatically.
 */
class SourceRepository : public QObject {
    Q_OBJECT
public:
    explicit SourceRepository(QObject* parent = nullptr);

    const QString&              currentFolder()       const;
    const QString&              layoutSourcePath()    const;
    bool                        layoutSourceIsList()  const;
    const QString&              sourceFolder()        const;
    const QVector<SmartFolder>& smartFolders()        const;
    const QVector<ProjectSource>& sources()           const;
    QVector<ProjectSource>&       sources();
    const QStringList&          activeFramePaths()    const;
    const QString&              frameListPath()       const;
    const QStringList&          orphanedSpritePaths() const;

    void setCurrentFolder(const QString& folder);
    void setLayoutSource(const QString& path, bool isList);
    void setSourceFolder(const QString& folder);
    void setSmartFolders(const QVector<SmartFolder>& folders);
    void setSources(const QVector<ProjectSource>& sources);
    void setActiveFramePaths(const QStringList& paths);
    void setFrameListPath(const QString& path);
    void setOrphanedSpritePaths(const QStringList& paths);

    /** Appends new paths (deduplicates) and emits framePathsAdded. */
    void addFramePaths(const QStringList& paths);

    /** Removes paths and emits framePathsRemoved. */
    void removeFramePaths(const QStringList& paths);

    void addSource(const ProjectSource& source);
    void removeSource(int index);

    void clear();

signals:
    void currentFolderChanged(QString folder);
    void sourcesChanged();
    void sourceAdded(int index);
    void sourceRemoved(int index, ProjectSource removed);

    /** Emitted by addFramePaths(); ProjectData forwards this to SpriteRepository. */
    void framePathsAdded(QStringList paths);

    /** Emitted by removeFramePaths(); ProjectData forwards this to SpriteRepository. */
    void framePathsRemoved(QStringList paths);

    void activeFramePathsChanged();
    void changed();

private:
    QString              m_currentFolder;
    QString              m_layoutSourcePath;
    bool                 m_layoutSourceIsList = false;
    QString              m_sourceFolder;
    QVector<SmartFolder> m_smartFolders;
    QVector<ProjectSource> m_sources;
    QStringList          m_activeFramePaths;
    QString              m_frameListPath;
    QStringList          m_orphanedSpritePaths;
};
