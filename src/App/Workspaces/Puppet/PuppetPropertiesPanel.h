#pragma once
#include <functional>
#include <optional>
#include <QHash>
#include <QPixmap>
#include <QPoint>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;

/**
 * @brief The Puppet workspace's right panel: the selected puppet's, part's or variant's
 *        properties. It only shows them and asks for changes; PuppetWorkspace applies them.
 *
 * Names are checked while typing with the validator the workspace gives; a name with a
 * problem is shown under the field and put back when editing ends.
 */
class PuppetPropertiesPanel : public QWidget {
    Q_OBJECT
public:
    /// Returns why a name can't be used, or an empty string when it can.
    using NameValidator = std::function<QString(const QString&)>;

    /// A marker a joint can use, and the variants that lack it (empty when all have it).
    struct MarkerChoice {
        QString name;
        QStringList missingIn;
    };

    /// How a part eases in the selected step of an animation (sprat-cli's step:PARTS and
    /// linear:PARTS): as the whole step does, unless it has its own.
    struct StepEase {
        QString sequence;
        int step = 0;                  ///< From 1, as the track labels steps
        bool stepSnaps = false;        ///< The whole step's ease: Step, or Linear
        std::optional<bool> snaps;     ///< The part's own, when it differs from the step's
        bool setByPose = true;         ///< The step's pose moves the part; otherwise no ease applies
    };

    /// Where a part is and where it could hang.
    struct Placement {
        std::optional<QPoint> position;  ///< Set when the part is placed by position
        // When attached:
        QString parent;
        QString parentMarker;
        QString childMarker;
        int leeway = 0;
        std::optional<int> angleMin;
        std::optional<int> angleMax;
        /// The parts it may hang from (not itself, nor the parts hanging from it).
        QStringList parents;
        /// Each of those parts' markers.
        QHash<QString, QVector<MarkerChoice>> parentMarkers;
        /// Why a new marker can't be added to a part for this one to hang from by its pivot,
        /// by part; parts it can be added to aren't listed.
        QHash<QString, QString> newMarkerProblems;
        /// This part's point markers, which it can hang by.
        QVector<MarkerChoice> childMarkers;
        /// The angle it is tried at: from its parent, or around its pivot. Not saved.
        int angle = 0;
        /// A pose is shown: Angle and Shows edit it. Otherwise (an animation playing) they are off.
        bool posing = false;
        /// Its variants' names, the order to list them in (indexes, by name), and the one the
        /// shown pose has it show.
        QStringList variants;
        QVector<int> variantOrder;
        int variant = 0;
        /// Its sprites' markers (not "pivot"), and those it neither shows nor offers for joints.
        QStringList markers;
        QStringList hiddenMarkers;
        /// The rig fields are locked (PuppetPart::rigLocked).
        bool rigLocked = false;
        /// The pose its Transformation edits, or empty when none is (an animation playing).
        QString pose;
        /// The part is locked in that pose (PuppetPose::lockedParts).
        bool poseLocked = false;
        /// X and Y can't change: locked in the pose, or, on Rest, where they move the rig, a
        /// locked rig.
        bool positionLocked = false;
        /// Shown while the Animations tab is: the selected step, if any.
        bool animating = false;
        std::optional<StepEase> stepEase;
    };

    /// What the part's Ease field asks for in the selected step.
    enum class PartEase { AsStep, Linear, Step };

    explicit PuppetPropertiesPanel(QWidget* parent = nullptr);

    void showNothing();
    void showPuppet(const QString& name, int partCount, NameValidator validator);
    /// @param problem Why the part can't be drawn, if it can't
    void showPart(const QString& name, const Placement& placement, const QString& problem,
                  NameValidator validator);
    /// @param explicitName The variant's own name, empty when it goes by @p defaultName
    /// @param source What it shows, e.g. "Sprite sprites/face.png"
    void showVariant(const QString& explicitName, const QString& defaultName, const QString& source,
                     const QPixmap& thumbnail, bool isRest, NameValidator validator);
    /// Shows the shown part's tried angle, as it turns on the canvas.
    void setAngle(int angle);

signals:
    void renameRequested(const QString& name);
    void positionChanged(const QPoint& position);
    /// Hang the part from @p parentMarker of part @p parent by its own @p childMarker.
    void attachRequested(const QString& parent, const QString& parentMarker, const QString& childMarker);
    /// Place the part by position, where it is now.
    void detachRequested();
    /// Hang the part by its pivot from a new marker of part @p parent, where its pivot is now.
    void newParentMarkerRequested(const QString& parent);
    /// The attachment's leeway and, when @p limited, its angle limits.
    void limitsChanged(int leeway, bool limited, int angleMin, int angleMax);
    /// Try the part at @p angle (not saved).
    void angleChanged(int angle);
    /// Show variant @p variantIndex in the selected pose.
    void shownVariantChanged(int variantIndex);
    /// The markers the part should neither show nor offer for joints.
    void hiddenMarkersChanged(const QStringList& hiddenMarkers);
    /// Lock or unlock how the part is placed, in every pose.
    void rigLockChanged(bool locked);
    /// Lock or unlock the part in the pose its Transformation edits.
    void poseLockChanged(bool locked);
    /// Ease the part in the selected step its own way, or as the step does.
    void stepEaseChanged(PuppetPropertiesPanel::PartEase ease);

private:
    /// Adds a "Name:" row, and under it a row for the name's problem (hidden when there is none).
    void addNameRows(QFormLayout* form, QWidget* page, QLineEdit*& edit, QLabel*& problem);
    void commitName(QLineEdit* edit, QLabel* problem);
    void checkName(QLineEdit* edit, QLabel* problem);
    void buildPlacement(QWidget* page, QFormLayout* form);
    /// The Transformation group: what the shown pose does to the part.
    void buildTransform(QWidget* page);
    /// Fills a marker combo, selecting @p current (added with ⚠ when it isn't offered).
    void fillMarkers(QComboBox* combo, const QVector<MarkerChoice>& markers, const QString& current);
    /// Adds the parent marker combo's last item, which asks for a new marker on @p parent.
    void addNewMarkerItem(const QString& parent);
    void emitAttach();
    void emitLimits();
    void updatePlacementRows();
    /// The Angle field's range: the joint's limits, or a free turn.
    void updateAngleRange();
    void showStepEase(const Placement& placement);
    /// Turns the fields the locks guard off (and the others back on as they allow).
    void applyLocks();

    QStackedWidget* m_pages = nullptr;
    NameValidator m_validator;
    QString m_shownName;  ///< The name in the field when it was shown

    QLineEdit* m_puppetName = nullptr;
    QLabel* m_puppetNameProblem = nullptr;
    QLabel* m_puppetInfo = nullptr;

    QLineEdit* m_partName = nullptr;
    QLabel* m_partNameProblem = nullptr;
    QFormLayout* m_partForm = nullptr;
    QGroupBox* m_rigGroup = nullptr;  ///< How the part is placed, in every pose
    QCheckBox* m_rigLock = nullptr;
    QGroupBox* m_transformGroup = nullptr;  ///< Shows, position and angle: the part in the shown pose
    QLabel* m_poseLabel = nullptr;
    QCheckBox* m_poseLock = nullptr;
    QFormLayout* m_transformForm = nullptr;
    QRadioButton* m_byPosition = nullptr;
    QRadioButton* m_attached = nullptr;
    QSpinBox* m_positionX = nullptr;
    QSpinBox* m_positionY = nullptr;
    QComboBox* m_parentCombo = nullptr;
    QComboBox* m_parentMarkerCombo = nullptr;
    QComboBox* m_childMarkerCombo = nullptr;
    QSpinBox* m_leeway = nullptr;
    QHBoxLayout* m_angleRow = nullptr;
    QCheckBox* m_limitAngle = nullptr;
    QSpinBox* m_angleMin = nullptr;
    QSpinBox* m_angleMax = nullptr;
    QSpinBox* m_angle = nullptr;
    QListWidget* m_markers = nullptr;
    QComboBox* m_shownVariant = nullptr;
    Placement m_placement;  ///< As shown
    QLabel* m_partProblem = nullptr;
    QGroupBox* m_animationGroup = nullptr;
    QComboBox* m_stepEase = nullptr;
    QLabel* m_stepEaseNote = nullptr;

    QLineEdit* m_variantName = nullptr;
    QLabel* m_variantNameProblem = nullptr;
    QLabel* m_variantSource = nullptr;
    QLabel* m_variantThumbnail = nullptr;
    QLabel* m_restLabel = nullptr;
};
