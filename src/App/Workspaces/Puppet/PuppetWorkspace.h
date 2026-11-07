#pragma once
#include <functional>
#include <QHash>
#include <QSet>
#include <QPixmap>
#include <QWidget>
#include "IWorkspace.h"
#include "PuppetCommands.h"
#include "PuppetCanvas.h"
#include "PuppetEvaluator.h"
#include "PuppetPropertiesPanel.h"
#include "SpriteModels.h"

class PuppetPoseStrip;
class PuppetTreeWidget;
class ProjectSession;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
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
    /// Sets which of the selected part's markers are neither shown nor offered for joints.
    void setSelectedPartHiddenMarkers(const QStringList& hiddenMarkers);

    // Poses: the film strip's frames are Rest (the rig, -1) and the puppet's poses. On a
    // pose frame, turning, pulling, moving and picking variants edit that pose: each drag is
    // one undo step, and a part it changes joins the pose.
    /// Shows frame @p poseIndex: -1 for Rest, else a pose.
    void selectPose(int poseIndex);
    /// Adds an empty pose after the selected frame, and selects it.
    void newPose();
    /// Adds a copy of the selected pose after it, and selects it.
    void duplicatePose();
    void removePose(int poseIndex);
    void renamePose(int poseIndex, const QString& name);
    void movePose(int from, int to);
    /// Turns part @p partIndex to @p angle in the selected pose: an attached part from its
    /// parent, within its limits; a positioned part around its pivot.
    void setPartAngle(int partIndex, double angle, const QString& mergeKey = QString());
    /// Moves attached part @p partIndex's joint @p sceneOffset from where the drag started,
    /// within its parent marker and leeway, in the selected pose.
    void driftPart(int partIndex, const QPointF& sceneOffset, const QString& mergeKey = QString());
    /// Pulls attached part @p partIndex by its point @p grip (in its sprite's pixels) towards
    /// @p target (puppet space), turning it and the parts it hangs from, in the selected pose.
    void pullPart(int partIndex, const QPointF& grip, const QPointF& target, const QString& mergeKey = QString());
    /// Has part @p partIndex show its variant @p variantIndex in the selected pose.
    void setPartVariant(int partIndex, int variantIndex);
    /// Adds part @p partIndex to the selected pose (as it is shown) or leaves it out.
    void setPartInPose(int partIndex, bool included);
    int selectedPose() const { return m_pose; }
    PuppetPoseStrip* poseStrip() const { return m_poseStrip; }
    QListWidget* poseParts() const { return m_poseParts; }

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
    int limitedAngle(const Puppet& puppet, int partIndex, double angle) const;
    /// Moves positioned part @p partIndex to @p position: in the selected pose, or at Rest in
    /// the rig (one undo step per @p mergeKey).
    void movePositionedPart(int partIndex, const QPoint& position, const QString& mergeKey = QString());
    /// Where positioned part @p partIndex is shown: its place in the selected pose, or its `at:`.
    QPoint shownPosition(const Puppet& puppet, int partIndex) const;
    /// Applies @p change to the selected pose's settings as one undo step (merged by @p mergeKey).
    void editPose(const QString& text, const QString& mergeKey,
                  const std::function<void(const Puppet&, QVector<PuppetPoseSetting>&)>& change);
    /// Rebuilds the film strip's thumbnails, all framed alike.
    void updatePoseStrip();
    void updatePoseButtons();
    /// Lists the parts under the tree, ticked when the selected pose sets them.
    void updatePoseParts();
    QPixmap renderThumbnail(const Puppet& puppet, const QVector<PuppetPartState>& states, const QRectF& frame) const;
    /// What the canvas shows: the selected pose on its own (rest elsewhere), within the joints'
    /// limits, or the rest pose on Rest.
    QVector<PuppetPartState> shownStates(const Puppet& puppet) const;
    /// The parts the selected pose sets.
    QSet<int> partsInPose(const Puppet& puppet) const;
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

    /// The selected frame of the film strip: -1 for Rest, else a pose.
    int m_pose = -1;
    QString m_posedPuppet;       ///< Atlas id and name of the puppet m_pose is for
    PuppetPartState m_turnStart;  ///< The turned part's, when the drag started
    QPointF m_pullGrip;          ///< Where the pulled part is held, in its sprite's pixels
    PuppetPoseStrip* m_poseStrip = nullptr;
    QPushButton* m_newPoseButton = nullptr;
    QPushButton* m_duplicatePoseButton = nullptr;
    QPushButton* m_removePoseButton = nullptr;
    QLabel* m_poseStatus = nullptr;
    QLabel* m_posePartsTitle = nullptr;
    QListWidget* m_poseParts = nullptr;
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
