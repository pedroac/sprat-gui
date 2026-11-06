#pragma once
#include <QHash>
#include <QPixmap>
#include <QWidget>
#include "IWorkspace.h"
#include "PuppetCommands.h"
#include "PuppetCanvas.h"
#include "PuppetEvaluator.h"
#include "PuppetPropertiesPanel.h"
#include "SpriteModels.h"

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
    /// Opens the New Puppet window, then adds the puppet built there.
    void newPuppet();
    /// Adds @p puppet to the atlas as one undo step (a taken name gets a number) and selects it.
    void addPuppet(const Puppet& puppet);
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

    // Trying the joints: turns and drifts shown on the canvas, never saved (a rig is its rest pose).
    /// Turns part @p partIndex to @p angle: an attached part from its parent, within its
    /// limits; a positioned part around its pivot.
    void tryTurning(int partIndex, double angle);
    /// Moves attached part @p partIndex's joint @p sceneOffset from where the drag started,
    /// within its parent marker and leeway.
    void tryDrifting(int partIndex, const QPointF& sceneOffset);
    /// Pulls attached part @p partIndex by its point @p grip (in its sprite's pixels) towards
    /// @p target (puppet space), turning it and the parts it hangs from within their limits.
    void tryPulling(int partIndex, const QPointF& grip, const QPointF& target);
    /// Puts every part back at rest.
    void resetTriedPose();

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
    /// Accepts the active atlas's sprites.
    std::function<bool(const SpritePtr&)> atlasSpriteFilter() const;
    /// The markers a joint of @p part can use (as the child: point markers only), each with
    /// the variants that lack it.
    QVector<PuppetPropertiesPanel::MarkerChoice> jointMarkers(const PuppetPart& part, bool asChild) const;
    /// The marker of its parent that part @p partIndex hangs from, in the parent's rest sprite.
    std::optional<NamedPoint> parentMarkerOf(const Puppet& puppet, int partIndex) const;
    /// @p angle within part @p partIndex's joint limits, or brought into a free turn.
    int triedAngle(const Puppet& puppet, int partIndex, double angle) const;
    /// The tried turns and drifts as evaluator states, within the joints' limits.
    QVector<PuppetPartState> triedStates(const Puppet& puppet) const;
    std::optional<PuppetCanvas::JointRange> jointRange(const Puppet& puppet,
                                                       const QVector<PuppetPartPlacement>& placements) const;
    QTreeWidgetItem* itemFor(const PuppetSelection& selection) const;

    ProjectSession* m_session = nullptr;
    QUndoStack* m_undoStack = nullptr;
    PuppetSelection m_selection;
    /// The puppet the canvas last fitted, so it fits again only for another puppet.
    QString m_fittedPuppet;
    bool m_rebuilding = false;
    QPoint m_dragStartPosition;  ///< The dragged part's position when the drag started
    int m_dragCount = 0;         ///< Numbers drags, so each drag is its own undo step

    /// A tried turn of a part, and drift of an attached one.
    struct TriedJoint {
        double angle = 0;
        QPointF offset;
    };
    QHash<QString, TriedJoint> m_tried;  ///< By part name, for the shown puppet
    QString m_triedPuppet;               ///< Atlas id and name of the puppet m_tried is for
    TriedJoint m_turnStart;              ///< The turned part's, when the drag started
    QPointF m_pullGrip;                  ///< Where the pulled part is held, in its sprite's pixels
    QPushButton* m_resetPoseButton = nullptr;
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
