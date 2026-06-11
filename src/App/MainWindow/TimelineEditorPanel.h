#pragma once
#include <QWidget>

class QLineEdit;
class QLabel;
class QComboBox;
class QPushButton;
class QSpinBox;
class TimelineTreeWidget;
class TimelineListWidget;

/**
 * @class TimelineEditorPanel
 * @brief Widget that owns all timeline-related controls.
 *
 * Contains two visually separate sections joined by an internal vertical
 * splitter:
 *   - Top: timeline-list area (create-name field + TimelineTreeWidget)
 *   - Bottom: selected-timeline editor (name/FPS/flip/alias) + frame list
 *
 * All signal connections to MainWindow slots are made by MainWindow via the
 * accessor methods after construction.
 */
class TimelineEditorPanel : public QWidget {
    Q_OBJECT
public:
    explicit TimelineEditorPanel(QWidget* parent = nullptr);

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

private:
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
};
