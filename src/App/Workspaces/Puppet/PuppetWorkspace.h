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
class PuppetSequenceTrack;
class PuppetTreeWidget;
class ProjectSession;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QMenu;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTabBar;
class QTimer;
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
    /// Moves the selected part to @p to in the draw order. Variants are listed by name, not moved.
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
    /// Adds an empty pose at index @p poseIndex, and selects it.
    void addPose(int poseIndex);
    /// Adds a copy of pose @p poseIndex after it, and selects it.
    void duplicatePose(int poseIndex);
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
    /// Scales part @p partIndex by @p scale (x, y factors, within PUPPET_MAX_SCALE either way)
    /// in the selected pose.
    void setPartScale(int partIndex, const QPointF& scale, const QString& mergeKey = QString());
    /// Fades part @p partIndex to @p opacity (0 to 1) in the selected pose.
    void setPartOpacity(int partIndex, double opacity, const QString& mergeKey = QString());
    /// Tints part @p partIndex @p tint in the selected pose.
    void setPartTint(int partIndex, const QColor& tint);
    /// Has part @p partIndex ease the other way from step @p stepIndex of the selected
    /// animation (snap in a linear step, tween in a snapping one), or not.
    void setStepPartEase(int stepIndex, const QString& part, bool otherWay);
    /// Has part @p partIndex show its variant @p variantIndex in the selected pose.
    void setPartVariant(int partIndex, int variantIndex);
    /// Shows @p variant (a tree selection) in the selected pose, as the tree's double-click and
    /// Show in Pose do, and keeps it selected. On Rest it becomes the rest variant.
    void showVariantInPose(const PuppetSelection& variant);
    /// The tree's context menu for the selected item. The caller owns it.
    QMenu* createContextMenu();
    /// Adds part @p partIndex to the selected pose (as it is shown) or leaves it out.
    void setPartInPose(int partIndex, bool included);
    int selectedPose() const { return m_pose; }
    PuppetPoseStrip* poseStrip() const { return m_poseStrip; }
    QListWidget* poseParts() const { return m_poseParts; }

    // Animations (sprat-cli's sequences): poses played one after another, each reached in
    // `in` frames and held `hold` frames. Each edit is one undo step. While stopped on a
    // step whose pose has arrived, the canvas edits that pose: the animation's own (a frame
    // made for it), or, for a shared pose (from the Poses tab), a copy the step then plays,
    // so editing one animation never changes another. The Poses tab lists shared poses only.
    void selectSequence(int sequenceIndex);
    void selectStep(int stepIndex);
    void addSequence(int sequenceIndex);
    void duplicateSequence(int sequenceIndex);
    void removeSequence(int sequenceIndex);
    void renameSequence(int sequenceIndex, const QString& name);
    /// Adds a step showing @p pose at index @p stepIndex of the selected animation, and selects it.
    void addStep(int stepIndex, const QString& pose);
    /// Adds a step at index @p stepIndex playing a new pose of the selected animation: a copy
    /// of step @p copyOf's pose and timing (-1 for an empty pose), and selects it.
    void addFrame(int stepIndex, int copyOf);
    /// Copies step @p stepIndex's pose to the Poses tab as @p name. Returns why it couldn't,
    /// or an empty string.
    QString savePoseFromStep(int stepIndex, const QString& name);
    /// Whether the canvas edits the selected step's pose: stopped where it has arrived.
    bool editingStep() const;
    void moveStep(int from, int to);
    void duplicateStep(int stepIndex);
    void removeStep(int stepIndex);
    void setPlaying(bool playing);
    int selectedSequence() const { return m_sequence; }
    int selectedStep() const { return m_step; }
    int playFrame() const { return m_playFrame; }
    QTabBar* bottomTabs() const { return m_bottomTabs; }
    /// The page below the canvas for tab @p tab.
    QWidget* bottomPage(int tab) const { return m_bottomPages.value(tab); }
    PuppetSequenceTrack* sequenceTrack() const { return m_track; }

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
    /// @p states within the rig's limits: angles in their joint's, offsets in the parent marker
    /// and leeway, positions only for parts placed by position.
    QVector<PuppetPartState> limitedStates(const Puppet& puppet, QVector<PuppetPartState> states) const;
    /// The selected animation, and those playing along, at the playhead.
    QVector<PuppetPartState> animationStates(const Puppet& puppet) const;
    QWidget* buildAnimationsTab(QWidget* parent);
    /// Shows the current tab's page below the canvas, and only it.
    void fitBottomTabs();
    void updateSequences();
    /// Lists the parts @p step's pose sets in the part-ease menu, ticked when they ease the
    /// other way from the step.
    void updateStepPartEases(const Puppet& puppet, const PuppetSequenceStep& step);
    void updatePlayhead();
    void advancePlayback();
    /// Applies @p change to the selected animation (or its selected step) as one undo step.
    void editSequence(const QString& text, const QString& mergeKey, const std::function<void(PuppetSequence&)>& change);
    void editStep(const QString& text, const QString& mergeKey, const std::function<void(PuppetSequenceStep&)>& change);
    /// The pose the canvas edits: the selected frame of the Poses tab, the selected step's
    /// in the Animations tab while editingStep(), else -1.
    int editedPose() const;
    /// Merges the panel's edits of the selected part's @p field (angle, scale...) in the
    /// edited pose or step.
    QString partEditKey(const QString& field) const;
    /// The selected animation where step @p stepIndex's pose has arrived.
    QVector<PuppetPartState> arrivalStates(const Puppet& puppet, int stepIndex) const;
    /// The steps before and after the selected one, when onion skin is on and it is edited.
    QVector<PuppetCanvas::OnionSkin> onionSkins(const Puppet& puppet) const;
    /// Asks a name for step @p stepIndex's pose, and copies it to the Poses tab.
    void askSavePoseFromStep(int stepIndex);
    /// The parts the edited pose sets.
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
    QVector<QPixmap> m_poseThumbnails;  ///< As the strip shows them, for the animations' steps

    QTabBar* m_bottomTabs = nullptr;
    QVector<QWidget*> m_bottomPages;  ///< Only the current tab's is shown
    bool m_animating = false;  ///< The Animations tab is shown: the canvas plays
    int m_sequence = -1;       ///< The selected animation
    int m_step = -1;           ///< Its selected step
    int m_playFrame = 0;       ///< The playhead, in the selected animation's frames
    QSet<QString> m_playAlong;  ///< Animations ticked to play along (preview only, not saved)
    bool m_rebuildingSequences = false;
    QTimer* m_playTimer = nullptr;
    QListWidget* m_sequenceList = nullptr;
    QPushButton* m_addSequenceButton = nullptr;
    QPushButton* m_removeSequenceButton = nullptr;
    QToolButton* m_playButton = nullptr;
    QSpinBox* m_sequenceFps = nullptr;
    QComboBox* m_sequenceLoop = nullptr;
    QLabel* m_sequenceInfo = nullptr;
    PuppetSequenceTrack* m_track = nullptr;
    QLabel* m_stepLabel = nullptr;
    QComboBox* m_stepPose = nullptr;
    QSpinBox* m_stepIn = nullptr;
    QSpinBox* m_stepHold = nullptr;
    QComboBox* m_stepEase = nullptr;
    QToolButton* m_stepPartEases = nullptr;  ///< Its menu ticks the parts easing the other way
    QCheckBox* m_onionSkin = nullptr;
    QLabel* m_stepNote = nullptr;  ///< Whose pose the selected step plays
    QString m_copyNote;            ///< Set when an edit made a shared pose the step's own
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
