#pragma once
#include <optional>
#include <QPainterPath>
#include <QPixmap>
#include <QPointF>
#include <QTransform>
#include <QVector>
#include "ZoomableGraphicsView.h"

class QGraphicsScene;

/**
 * @brief Draws a puppet in its rest pose, with a large cross at the puppet's origin, a
 *        small one at the selected part's pivot, and a line from each parent to the parts
 *        hanging from it.
 *
 * Clicking a part selects it; dragging a movable (positioned) part, or the arrow keys on the
 * selected one, asks to move it. Dragging an attached part asks to pull it (turning the parts
 * it hangs from); Shift+dragging it asks to move its joint. Dragging the selected part's turn
 * handle asks to turn it, whether attached or placed by position. Dragging the selected part's pivot handle shows the markers it can
 * hang from: dropping on one asks to attach the part there, dropping elsewhere asks to place
 * it there by position.
 *
 * Scene coordinates are the puppet's space: origin = the puppet's pivot, Y down, in the
 * sprites' original-image pixels.
 */
class PuppetCanvas : public ZoomableGraphicsView {
    Q_OBJECT
public:
    /// A marker of a part, in scene coordinates.
    struct Marker {
        QString name;
        QPointF point;      ///< Where a joint on it goes
        QPainterPath shape; ///< A circle, rectangle or polygon; empty for a point
    };

    /// A part as drawn: its shown sprite's image and where PuppetEvaluator placed it.
    struct Part {
        QPixmap image;
        QTransform transform;
        bool placed = false;
        bool movable = false;       ///< Placed by its position, so dragging it moves it
        bool attached = false;      ///< Hangs from another part: dragging it pulls it
        QPointF joint;              ///< Where it turns: its pivot or its joint
        bool attachTarget = false;  ///< The selected part may hang from it
        bool included = true;       ///< Set by the pose shown; drawn faint when not
        QVector<Marker> markers;    ///< Drawn faintly (the selected part's labelled); targets when attaching
    };

    /// A line from a parent's pivot to the joint of a part hanging from it.
    struct Bone {
        QPointF from;
        QPointF to;
        bool selected = false;  ///< Its part is selected
    };

    /// How far the selected part may turn and drift, drawn around its joint (its pivot, for a
    /// positioned part), with a handle at the end of its angle line that turns it.
    struct JointRange {
        bool attached = false;     ///< Turns from its parent; otherwise around its own pivot
        QPointF point;
        double restDirection = 0;  ///< Degrees, from the joint to the part's center at angle 0
        double angle = 0;          ///< Its angle now, from the parent
        std::optional<int> angleMin;
        std::optional<int> angleMax;
        QPainterPath area;         ///< Where the joint may go; empty when it can't drift
    };

    explicit PuppetCanvas(QWidget* parent = nullptr);

    /// Shows @p parts, in draw order (the first at the back). @p selectedPart is outlined,
    /// with its pivot handle at @p selectedPivot.
    void showPuppet(const QVector<Part>& parts, const QVector<Bone>& bones, int selectedPart,
                    std::optional<QPointF> selectedPivot, std::optional<JointRange> selectedJoint = std::nullopt);
    /// Whether a pose is shown: only then do the turn handle, pulling and moving joints work.
    /// Without one, dragging a part placed by position still moves it.
    void setPosing(bool posing);
    /// Off while an animation plays: clicks still select parts, but nothing moves or attaches.
    void setEditable(bool editable);
    /// Shows only the origin cross.
    void clearPuppet();
    /// Zooms to fit the puppet, and keeps fitting it as the view resizes until the user zooms
    /// or the puppet changes. Nothing else moves the view: edits leave it where it is.
    void fitPuppet();
    /// Fits the whole puppet (the base class fits one side and scrolls the other).
    void initialFit() override;

signals:
    /// A part was clicked (its index in draw order), or -1 for empty space.
    void partClicked(int partIndex);
    /// A movable part started being dragged.
    void partDragStarted(int partIndex);
    /// The dragged part moved @p offset scene pixels from where the drag started.
    void partDragged(int partIndex, const QPoint& offset);
    /// A part started being turned (by its handle) or, attached, drifted (Shift+dragging).
    void partTurnStarted(int partIndex, bool drift);
    /// The turned part turned @p degrees (clockwise, unwrapped) since the drag started.
    void partTurned(int partIndex, double degrees);
    /// The drifted part's joint moved @p offset scene pixels since the drag started.
    void partDrifted(int partIndex, const QPointF& offset);
    /// An attached part started being pulled, by its point at @p scenePos.
    void partPullStarted(int partIndex, const QPointF& scenePos);
    /// The pulled part's point is asked to be at @p scenePos.
    void partPulled(int partIndex, const QPointF& scenePos);
    /// The arrow keys asked to move the selected part by @p offset.
    void partNudged(int partIndex, const QPoint& offset);
    /// The selected part's pivot handle was dropped on marker @p marker of part @p parentIndex.
    void attachRequested(int partIndex, int parentIndex, const QString& marker);
    /// The selected part's pivot handle was dropped away from any marker, at @p scenePos.
    void detachRequested(int partIndex, const QPointF& scenePos);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    /// A marker the pivot handle is over: part index and marker index.
    struct Target {
        int part = -1;
        int marker = -1;
    };

    /// The part drawn at @p viewPos (topmost), or -1.
    int partAt(const QPoint& viewPos) const;
    bool isMovable(int part) const;
    bool isAttached(int part) const;
    /// Degrees clockwise of @p scenePos around the turned part's joint.
    double angleAround(const QPointF& scenePos) const;
    /// Whether @p viewPos is on the selected part's pivot handle.
    bool onPivotHandle(const QPoint& viewPos) const;
    /// Where the selected part's turn handle is, in scene coordinates, if it has one.
    std::optional<QPointF> turnHandle() const;
    bool onTurnHandle(const QPoint& viewPos) const;
    /// The marker of an attach target nearest to @p viewPos, if it is close enough.
    Target targetAt(const QPoint& viewPos) const;
    void cancelAttach();
    void drawLabel(QPainter* painter, const QPointF& scenePos, const QString& text, const QColor& color);

    void addCross(const QPointF& at, int halfSize, const QColor& color, qreal z);
    void updateSceneRect();

    QGraphicsScene* m_scene = nullptr;
    /// The scene point the last resize kept in the middle, with the scroll position and zoom
    /// it left: the next resize starts from it while they are unchanged, so the view doesn't
    /// creep by a rounded pixel per resize.
    struct KeptCenter {
        QPointF center;
        QPoint scroll;
        double zoom = 0;
    };
    std::optional<KeptCenter> m_keptCenter;
    QRectF m_puppetBounds;  ///< The parts and the origin, with a margin: what a fit shows
    QVector<Part> m_parts;  ///< As last shown
    QVector<Bone> m_bones;
    int m_selectedPart = -1;
    std::optional<QPointF> m_selectedPivot;

    int m_pressedPart = -1;   ///< The movable part under the left button, or -1
    QPoint m_pressPos;
    QPointF m_pressScenePos;
    bool m_dragging = false;

    int m_pullPart = -1;         ///< The attached part pulled by the left button, or -1
    bool m_pulling = false;      ///< Past the drag distance
    int m_turnPart = -1;         ///< The part turned or drifted by the left button, or -1
    bool m_drift = false;        ///< Shift was down: moving its joint, not turning
    bool m_turning = false;      ///< Past the drag distance
    double m_turnLastAngle = 0;  ///< The pointer's angle around the joint at the last move
    double m_turnTotal = 0;      ///< Degrees turned since the press, unwrapped
    std::optional<JointRange> m_selectedJoint;

    bool m_posing = false;
    bool m_editable = true;
    bool m_handlePressed = false;  ///< The left button went down on the pivot handle
    bool m_attaching = false;      ///< The pivot handle is being dragged
    QPointF m_attachPointer;       ///< Where it is, in scene coordinates
    Target m_attachTarget;
};
