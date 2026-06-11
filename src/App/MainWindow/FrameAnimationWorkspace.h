#pragma once
#include <QWidget>
#include "models.h"

class NavigatorPanel;
class ProjectSession;

/**
 * @class FrameAnimationWorkspace
 * @brief Widget that owns a NavigatorPanel for the Frame Animation workspace.
 *
 * The NavigatorPanel is configured with the atlas combo visible and the
 * show-hidden checkbox hidden.
 *
 * The right-side animation/timeline panel is still owned by MainWindow for now
 * and is left as a placeholder here (to be migrated in a later step).
 */
class FrameAnimationWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit FrameAnimationWorkspace(QWidget* parent = nullptr);

    NavigatorPanel* navigatorPanel() const { return m_navigator; }

    /**
     * @brief Update the atlas combo in the navigator panel.
     *
     * @param atlases            All atlas entries.
     * @param activeSessionIndex The currently active atlas index in the session.
     */
    void updateAtlasCombo(const QVector<AtlasEntry>& atlases, int activeSessionIndex);

    /**
     * @brief Refresh the navigator tree for the given session.
     *
     * Uses the atlas index currently selected in the combo as the filter.
     *
     * @param session Active project session.
     */
    void refreshNavigator(const ProjectSession* session);

signals:
    /** Emitted when the user selects a different atlas in the combo. */
    void atlasChanged(int sessionAtlasIndex);

private:
    NavigatorPanel* m_navigator = nullptr;
};
