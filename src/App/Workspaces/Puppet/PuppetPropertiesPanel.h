#pragma once
#include <functional>
#include <optional>
#include <QHash>
#include <QColor>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;
class QToolButton;

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
        /// This part's point markers, which it can hang by.
        QVector<MarkerChoice> childMarkers;
        /// The angle it is tried at: from its parent, or around its pivot. Not saved.
        int angle = 0;
        /// How the shown pose scales, fades and tints it.
        QPointF scale{1.0, 1.0};
        double opacity = 1.0;
        QColor tint = Qt::white;
        /// A pose is shown: Angle, Scale, Opacity, Tint and Shows edit it. Otherwise (an
        /// animation playing) they are off.
        bool posing = false;
        /// Its variants' names, the order to list them in (indexes, by name), and the one the
        /// shown pose has it show.
        QStringList variants;
        QVector<int> variantOrder;
        int variant = 0;
        /// Its sprites' markers (not "pivot"), and those it neither shows nor offers for joints.
        QStringList markers;
        QStringList hiddenMarkers;
    };

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
    /// The attachment's leeway and, when @p limited, its angle limits.
    void limitsChanged(int leeway, bool limited, int angleMin, int angleMax);
    /// Try the part at @p angle (not saved).
    void angleChanged(int angle);
    /// Scale the part by @p scale (x, y factors) in the selected pose.
    void scaleChanged(const QPointF& scale);
    /// Fade the part to @p opacity (0 to 1) in the selected pose.
    void opacityChanged(double opacity);
    /// Tint the part @p tint in the selected pose.
    void tintChanged(const QColor& tint);
    /// Show variant @p variantIndex in the selected pose.
    void shownVariantChanged(int variantIndex);
    /// The markers the part should neither show nor offer for joints.
    void hiddenMarkersChanged(const QStringList& hiddenMarkers);

private:
    /// Adds a "Name:" row, and under it a row for the name's problem (hidden when there is none).
    void addNameRows(QFormLayout* form, QWidget* page, QLineEdit*& edit, QLabel*& problem);
    void commitName(QLineEdit* edit, QLabel* problem);
    void checkName(QLineEdit* edit, QLabel* problem);
    void buildPlacement(QWidget* page, QFormLayout* form);
    /// Fills a marker combo, selecting @p current (added with ⚠ when it isn't offered).
    void fillMarkers(QComboBox* combo, const QVector<MarkerChoice>& markers, const QString& current);
    void emitAttach();
    void emitLimits();
    void updatePlacementRows();
    /// The Angle field's range: the joint's limits, or a free turn.
    void updateAngleRange();
    void buildLooks(QWidget* page, QFormLayout* form);
    void showLooks(const Placement& placement);
    void emitScale();
    void showTint(const QColor& tint);
    void pickTint();

    QStackedWidget* m_pages = nullptr;
    NameValidator m_validator;
    QString m_shownName;  ///< The name in the field when it was shown

    QLineEdit* m_puppetName = nullptr;
    QLabel* m_puppetNameProblem = nullptr;
    QLabel* m_puppetInfo = nullptr;

    QLineEdit* m_partName = nullptr;
    QLabel* m_partNameProblem = nullptr;
    QFormLayout* m_partForm = nullptr;
    QRadioButton* m_byPosition = nullptr;
    QRadioButton* m_attached = nullptr;
    QHBoxLayout* m_positionRow = nullptr;
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
    QHBoxLayout* m_scaleRow = nullptr;
    QDoubleSpinBox* m_scaleX = nullptr;
    QDoubleSpinBox* m_scaleY = nullptr;
    QToolButton* m_scaleLinked = nullptr;
    QHBoxLayout* m_flipRow = nullptr;
    QToolButton* m_flipX = nullptr;
    QToolButton* m_flipY = nullptr;
    QSpinBox* m_opacity = nullptr;
    QHBoxLayout* m_tintRow = nullptr;
    QPushButton* m_tint = nullptr;
    QToolButton* m_clearTint = nullptr;
    QColor m_shownTint = Qt::white;
    QListWidget* m_markers = nullptr;
    QComboBox* m_shownVariant = nullptr;
    Placement m_placement;  ///< As shown
    QLabel* m_partProblem = nullptr;

    QLineEdit* m_variantName = nullptr;
    QLabel* m_variantNameProblem = nullptr;
    QLabel* m_variantSource = nullptr;
    QLabel* m_variantThumbnail = nullptr;
    QLabel* m_restLabel = nullptr;
};
