#pragma once
#include <QHash>
#include <QPixmap>
#include <QWidget>
#include "IWorkspace.h"
#include "PuppetCommands.h"
#include "PuppetEvaluator.h"
#include "PuppetPropertiesPanel.h"
#include "SpriteModels.h"

class PuppetCanvas;
class PuppetTreeWidget;
class ProjectSession;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QTreeWidgetItem;
class QUndoStack;
struct AtlasEntry;

/**
 * @brief Workspace for building sprat-cli puppets (cutout rigs) of the active atlas.
 *
 *  - Left:   the puppets tree (puppet › part › variant, parts in draw order), with Add and
 *            Remove under it, and an Atlas selector above it when there are several atlases.
 *  - Middle: PuppetCanvas, the selected puppet in its rest pose.
 *  - Right:  PuppetPropertiesPanel, the selected item's properties.
 *
 * Every change is one PuppetsEditCommand on the undo stack.
 */
class PuppetWorkspace : public QWidget, public IWorkspace {
    Q_OBJECT
public:
    PuppetWorkspace(ProjectSession* session, QUndoStack* undoStack, QWidget* parent = nullptr);

    // IWorkspace
    void enter() override;
    void leave() override;
    QWidget* widget() override { return this; }

    /// Rebuilds everything from the active atlas's puppets.
    void refresh();
    /// Reloads the active atlas's sprites and images (after a layout, or an atlas switch),
    /// then refreshes.
    void refreshSprites();
    /// Lists the atlases that can hold puppets, selecting the active one.
    void updateAtlasCombo(const QVector<AtlasEntry>& atlases, int activeIndex);

    PuppetTreeWidget* tree() const { return m_tree; }
    PuppetCanvas* canvas() const { return m_canvas; }
    PuppetPropertiesPanel* propertiesPanel() const { return m_properties; }
    QDoubleSpinBox* zoomSpin() const { return m_zoomSpin; }
    PuppetSelection selection() const { return m_selection; }

    // Edits, each one undo step. They act on the current selection.
    void addPuppet();
    void addPart();
    /// Adds the sprites as variants of the selected part.
    void addSpriteVariants(const QStringList& spritePaths);
    void removeSelected();
    void renameSelected(const QString& name);
    /// Moves the selected part or variant to @p to among its siblings.
    void moveSelected(int to);
    /// Sets the selected part's position. Edits with the same @p mergeKey in a row are one
    /// undo step; an empty key merges the panel's edits of this part.
    void setSelectedPartPosition(const QPoint& position, const QString& mergeKey = QString());
    void select(const PuppetSelection& selection);
    /// Hangs the selected part from @p parentMarker of part @p parentIndex by its @p childMarker.
    void attachSelectedPart(int parentIndex, const QString& parentMarker, const QString& childMarker);
    /// Places the selected part by position, where it is now.
    void detachSelectedPart();
    /// Places the selected part by position so that its joint (the pivot handle) is at @p scenePos.
    void placeSelectedPartJointAt(const QPointF& scenePos);
    void setSelectedPartLimits(int leeway, std::optional<int> angleMin, std::optional<int> angleMax);

signals:
    void atlasSelected(int atlasIndex);

private:
    void setupUi();
    void rebuildTree();
    void updateCanvas();
    void updateProperties();
    void updateButtons();
    void showContextMenu(const QPoint& pos);
    void pickSpriteVariants();
    /// Pushes one undo step that turns the active atlas's puppets into @p after.
    void pushEdit(const QString& text, const QVector<Puppet>& after, const PuppetSelection& selectionAfter,
                  const QString& mergeKey = QString());
    void applyPuppets(const QString& atlasId, const QVector<Puppet>& puppets, const PuppetSelection& selection);
    /// The selection with indexes that exist in @p puppets, keeping as much as it can.
    static PuppetSelection clampedSelection(const QVector<Puppet>& puppets, PuppetSelection selection);

    const QVector<Puppet>& puppets() const;
    const Puppet* selectedPuppet() const;
    /// The sprite a variant shows (an animation's first frame), or nullptr.
    SpritePtr spriteFor(const PuppetVariant& variant) const;
    QPixmap imageOf(const SpritePtr& sprite) const;
    PuppetSpriteLookup spriteLookup() const;
    QString describeSource(const PuppetVariant& variant) const;
    /// The markers a joint of @p part can use (as the child: point markers only), each with
    /// the variants that lack it.
    QVector<PuppetPropertiesPanel::MarkerChoice> jointMarkers(const PuppetPart& part, bool asChild) const;
    QTreeWidgetItem* itemFor(const PuppetSelection& selection) const;

    ProjectSession* m_session = nullptr;
    QUndoStack* m_undoStack = nullptr;
    PuppetSelection m_selection;
    /// The puppet the canvas last fitted, so it fits again only for another puppet.
    QString m_fittedPuppet;
    bool m_rebuilding = false;
    QPoint m_dragStartPosition;  ///< The dragged part's position when the drag started
    int m_dragCount = 0;         ///< Numbers drags, so each drag is its own undo step
    mutable QHash<QString, QPixmap> m_images;  ///< By sprite path
    QHash<QString, SpritePtr> m_spritesByPath;  ///< The active atlas's laid-out sprites
    QHash<QString, SpritePtr> m_spritesByName;

    QWidget* m_atlasRow = nullptr;
    QComboBox* m_atlasCombo = nullptr;
    QStackedWidget* m_treeStack = nullptr;
    PuppetTreeWidget* m_tree = nullptr;
    QToolButton* m_addButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QAction* m_addPuppetAction = nullptr;
    QAction* m_addPartAction = nullptr;
    QAction* m_addSpritesAction = nullptr;
    QAction* m_removeAction = nullptr;
    QAction* m_renameAction = nullptr;

    QDoubleSpinBox* m_zoomSpin = nullptr;
    PuppetCanvas* m_canvas = nullptr;
    PuppetPropertiesPanel* m_properties = nullptr;
};
