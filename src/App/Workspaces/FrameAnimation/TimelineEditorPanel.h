#pragma once
#include <QWidget>
#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <QSize>
#include <QStringList>

class QLineEdit;
class QLabel;
class QComboBox;
class QPushButton;
class QSpinBox;
class QUndoStack;
class QTreeWidgetItem;
class TimelineTreeWidget;
class TimelineListWidget;
class ProjectSession;

/**
 * @class TimelineEditorPanel
 * @brief Widget that owns all timeline-related controls and business logic.
 *
 * Contains two visually separate sections joined by an internal vertical
 * splitter:
 *   - Top: timeline-list area (create-name field + TimelineTreeWidget)
 *   - Bottom: selected-timeline editor (name/FPS/flip/alias) + frame list
 *
 * After Phase 7, all timeline business logic lives here. MainWindow connects
 * to the signals emitted by this panel to update the animation preview.
 */
class TimelineEditorPanel : public QWidget {
    Q_OBJECT
public:
    explicit TimelineEditorPanel(ProjectSession* session, QUndoStack* undoStack, QWidget* parent = nullptr);

    // ── Accessors for the timeline-list section ───────────────────────────────
    QLineEdit*          timelineCreateEdit() const { return m_timelineCreateEdit; }
    TimelineTreeWidget* timelineList()       const { return m_timelineList; }

    // ── Accessors for the selected-timeline editor section ───────────────────
    /** The container widget for the selected-timeline editor (name/fps/flip + frames). */
    QWidget*            timelineEditorContainer() const { return m_timelineEditorContainer; }
    /** The sub-widget grouping name/fps/flip/alias controls. */
    QWidget*            selectedTimelineGroup()   const { return m_selectedTimelineGroup; }
    QLineEdit*          timelineNameEdit()         const { return m_timelineNameEdit; }
    QSpinBox*           timelineFpsSpin()          const { return m_timelineFpsSpin; }
    QLabel*             timelineAliasLabel()       const { return m_timelineAliasLabel; }
    QLabel*             timelineFlipLabel()        const { return m_timelineFlipLabel; }
    QComboBox*          timelineFlipCombo()        const { return m_timelineFlipCombo; }
    QWidget*            timelineDropArea()         const { return m_timelineDropArea; }
    QLabel*             timelineDragHintLabel()    const { return m_timelineDragHintLabel; }
    TimelineListWidget* timelineFramesList()       const { return m_timelineFramesList; }

    /** The widget that carries the top timeline-list area (for splitter assembly). */
    QWidget*            listAreaWidget()           const { return m_listArea; }

    QPushButton*        addTimelineButton()        const { return m_addBtn; }
    QPushButton*        removeTimelineButton()     const { return m_removeBtn; }

    // ── Public methods ────────────────────────────────────────────────────────
    void refreshTimelineList();
    void refreshTimelineFrames();
    /** Refreshes the list, sets the current item to the given index (or clears
     *  selection if index < 0), then lets onTimelineSelectionChanged() fire. */
    void selectTimeline(int index);

    void onTimelineAddClicked();
    void onTimelineRemoveClicked();
    void onTimelineSelectionChanged();
    void onGenerateTimelinesFromFrames();

    bool    hasDuplicateTimelineName(const QString& name) const;
    QString getUniqueTimelineName(const QString& baseName, const QString& folderPath = QString()) const;

    /** Returns the tree item whose UserRole data equals @p index, or nullptr. */
    QTreeWidgetItem* timelineItemForIndex(int index) const;

signals:
    /** MainWindow: update timer interval if currently playing. */
    void animPlaybackIntervalChanged(int fps);
    /** MainWindow: set m_animFrameIndex = 0, fitAnimationToViewport, refreshAnimationTest. */
    void animFrameReset();
    /** MainWindow: set m_animFrameIndex = index, refreshAnimationTest (only when not playing). */
    void animFrameIndexSelected(int index);
    /** MainWindow: setZoomManual(false), fitAnimationToViewport, refreshAnimationTest. */
    void animZoomResetAndFitRequested();
    /** MainWindow: refreshAnimationTest only. */
    void animationDataChanged();
    /** MainWindow: m_statusLabel->setText(text). */
    void statusMessage(const QString& text);
    /** MainWindow: onSpritesDroppedToTimeline(paths, targetFolder). */
    void spritesToTimelineRequested(const QStringList& paths, const QString& targetFolder);

private slots:
    void onTimelineNameChanged();
    void onTimelineFpsChanged(int fps);
    void onTimelineCreateAlias();
    void onTimelineFlipChanged(int index);
    void onTimelineContextMenu(const QPoint& pos);
    void onTimelineDeleteKey();
    void onTimelineTreeDropCompleted(int draggedIndex, const QString& draggedFolder, const QString& targetFolder);
    void onTimelineItemChanged(QTreeWidgetItem* item, int column);
    void onFrameDropped(const QString& path, int index);
    void onFrameMoved(int from, int to);
    void onFrameDuplicateRequested(int index);
    void onFrameRemoveRequested();
    void onTimelineFrameSelectionChanged();
    void onCreateTimelineFromDroppedPaths();

private:
    /** Refreshes the list, restores current item, refreshes frames, emits animationDataChanged. */
    void refreshAndSelect();

    // Timeline list section
    QWidget*            m_listArea            = nullptr;
    QLineEdit*          m_timelineCreateEdit  = nullptr;
    TimelineTreeWidget* m_timelineList        = nullptr;
    QPushButton*        m_addBtn              = nullptr;
    QPushButton*        m_removeBtn           = nullptr;

    // Selected-timeline editor section
    QWidget*            m_timelineEditorContainer = nullptr;
    QWidget*            m_selectedTimelineGroup   = nullptr;
    QLineEdit*          m_timelineNameEdit        = nullptr;
    QSpinBox*           m_timelineFpsSpin         = nullptr;
    QLabel*             m_timelineAliasLabel      = nullptr;
    QLabel*             m_timelineFlipLabel       = nullptr;
    QComboBox*          m_timelineFlipCombo       = nullptr;
    QWidget*            m_timelineDropArea        = nullptr;
    QLabel*             m_timelineDragHintLabel   = nullptr;
    TimelineListWidget* m_timelineFramesList      = nullptr;

    // Session + undo
    ProjectSession* m_session   = nullptr;
    QUndoStack*     m_undoStack = nullptr;

    // Pending paths for creating a new timeline when no timeline is selected
    QStringList m_pendingCreateTimelinePaths;

    // Icon / pixmap caches
    QHash<QString, QSize>   m_imageSizeCache;      // natural image size (header-only read)
    QHash<QString, QPixmap> m_framePixmapCache;    // source pixmaps for frame thumbnails
    QHash<QString, QIcon>   m_timelineListIconCache;
};
