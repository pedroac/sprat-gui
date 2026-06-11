#include "FrameAnimationWorkspace.h"
#include "NavigatorPanel.h"
#include "ProjectSession.h"

#include <QHBoxLayout>
#include <QWidget>

FrameAnimationWorkspace::FrameAnimationWorkspace(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Left: navigator panel with atlas combo visible, show-hidden hidden
    m_navigator = new NavigatorPanel(this);
    m_navigator->setAtlasComboVisible(true);
    m_navigator->setShowHiddenVisible(false);
    m_navigator->setCheckboxesEnabled(true);
    layout->addWidget(m_navigator, 0);

    // Forward atlas selection changes
    connect(m_navigator, &NavigatorPanel::atlasIndexChanged,
            this, &FrameAnimationWorkspace::atlasChanged);

    // Right: placeholder for the timeline/animation panel (still owned by MainWindow)
    // This will be populated/migrated in a later refactoring step.
    auto* rightPlaceholder = new QWidget(this);
    layout->addWidget(rightPlaceholder, 1);
}

void FrameAnimationWorkspace::updateAtlasCombo(const QVector<AtlasEntry>& atlases,
                                                int activeSessionIndex)
{
    if (m_navigator)
        m_navigator->updateAtlasCombo(atlases, activeSessionIndex);
}

void FrameAnimationWorkspace::refreshNavigator(const ProjectSession* session)
{
    if (!m_navigator || !session) return;

    // Determine the atlas filter from the combo's current selection
    int atlasFilter = -1;
    if (m_navigator->tree()) {
        // The atlas combo holds the currently selected atlas index in item data.
        // We delegate the logic to NavigatorPanel::refresh which accepts the filter.
    }

    // Use -1 as default; MainWindow calls refresh with the correct filter
    // (via refreshSpriteTree which routes through the panel)
    m_navigator->refresh(session, /*showHidden=*/false, atlasFilter);
}
