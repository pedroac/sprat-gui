#pragma once

#include <QDialog>
#include <QTableWidgetItem>
#include <QVector>
#include "models.h"

class QTableWidget;
class QLabel;
class QPushButton;
class QListWidget;

/**
 * @class SourcesDialog
 * @brief Non-modal dialog that shows and manages project image sources.
 *
 * Displays a table of ProjectSource entries with per-row Sync and Remove
 * actions, an Add button for registering new sources, and an orphaned-sprites
 * section at the bottom.
 */
class SourcesDialog : public QDialog {
    Q_OBJECT

public:
    explicit SourcesDialog(QWidget* parent = nullptr);

    /** Refresh the displayed source list and orphaned-sprite section. */
    void refresh(const QVector<ProjectSource>& sources,
                 const QStringList& orphaned);

signals:
    /** Emitted when the user requests a sync for the source at the given index. */
    void syncSourceRequested(int sourceIndex);

    /** Emitted when the user requests removal of the source at the given index. */
    void removeSourceRequested(int sourceIndex);

    /** Emitted when the user edits the name of a source. */
    void sourceRenamed(int sourceIndex, const QString& newName);

    /** Emitted when the user wants to add a folder source. */
    void addFolderRequested();

    /** Emitted when the user wants to add a single-image source. */
    void addFileRequested();

    /** Emitted when the user wants to add an archive source. */
    void addArchiveRequested();

    /** Emitted when the user wants to add a URL source. */
    void addUrlRequested();

    /** Emitted when the user requests restoring an orphaned sprite. */
    void restoreOrphanRequested(const QString& path);

    /** Emitted when the user discards an orphaned sprite. */
    void discardOrphanRequested(const QString& path);

private slots:
    void onAddClicked();
    void onItemChanged(QTableWidgetItem* item);

private:
    void buildUi();

    QTableWidget* m_sourcesTable = nullptr;
    QListWidget*  m_orphanList   = nullptr;
    QLabel*       m_orphanLabel  = nullptr;
    bool          m_programmaticUpdate = false;
};
