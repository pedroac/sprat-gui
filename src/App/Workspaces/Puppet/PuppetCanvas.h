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
 * @brief Draws a puppet in its rest pose, with a large cross at the puppet's origin and a
 *        small one at the selected part's pivot.
 *
 * Clicking a part selects it; right-clicking it asks for its menu. Dragging a movable
 * (positioned) part, or the arrow keys on the selected one, asks to move it. Dragging an attached part asks to pull it (turning the parts
 * it hangs from); Shift+dragging it asks to move its joint. Dragging the selected part's turn
 * handle asks to turn it, whether attached or placed by position. Dragging the selected part's
 * pivot handle shows the markers it can hang from: dropping on one asks to attach the part
 * there, dropping on such a part away from its markers asks to hang it from a new marker
 * there, and dropping elsewhere (or with Shift held) asks to place it there by position.
 * Double-clicking it asks to hang the part where it is, from a part under its pivot.
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
        /// Why a new marker can't be added to it for the selected part to hang from; empty
        /// when one can (dropping the pivot handle on the part, away from its markers).
        QString newMarkerProblem;
        bool included = true;       ///< Set by the pose shown; drawn faint when not
        bool locked = false;        ///< Locked in the pose shown: it neither turns, drifts nor is pulled
        QVector<Marker> markers;    ///< Drawn faintly (the selected part's labelled); targets when attaching
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

    /// A puppet drawn faintly behind the shown one, in one colour: another frame of an
    /// animation (onion skin). Its parts are drawn as Part draws them.
    struct OnionSkin {
        struct Ghost {
            QPixmap image;
            QTransform transform;
        };
        QVector<Ghost> parts;
        QColor tint;
    };

    /// Lines every cell, from the puppet's origin moved by @p offset, over the parts.
    struct Grid {
        bool visible = false;
        QSize cell{16, 16};
        QPoint offset;
        QColor color{255, 255, 255, 80};
    };

    /// How the canvas looks and how near the pointer must be to its handles. Sizes are in
    /// screen pixels, so they stay the same at any zoom.
    struct CanvasSettings {
        int pivotRingRadius = 7;          ///< The pivot ring: pressing inside it attaches the part
        int pivotDoubleClickRadius = 10;  ///< A double-click this near the pivot hangs the part
        /// The turn handle's distance from the pivot, and the radius of the area around it where
        /// dragging moves the selected part; always beyond the pivot ring and the turn handle.
        int turnHandleDistance = 36;
        int turnHandleRadius = 5;
        int markerSnapDistance = 12;      ///< The dragged pivot ring snaps to markers this near
        int dragDistance = 0;             ///< Pointer travel before a drag starts; 0 = the system's
        int pivotCrossSize = 8;           ///< Half the selected part's pivot cross
        int originCrossSize = 24;         ///< Half the puppet's origin cross
        double markerDotRadius = 3.5;
        double handleLineWidth = 1.5;     ///< The pivot ring; the angle line is half a pixel wider
        QColor handleColor{61, 174, 233};  ///< The pivot ring and cross, angle line, turn handle, outline
        QColor rangeColor{255, 210, 0};    ///< Markers, the joint's area and the turn arc
        QColor originColor{255, 255, 255};
        QColor onionBeforeColor{220, 50, 50};
        QColor onionAfterColor{40, 170, 70};
        int onionSkinOpacity = 30;        ///< Percent
        int fadedPartOpacity = 35;        ///< Percent, for parts the pose leaves alone
    };

    explicit PuppetCanvas(QWidget* parent = nullptr);
    void setGrid(const Grid& grid);
    const Grid& grid() const { return m_grid; }
    /// Applies @p settings, keeping the move area beyond the pivot ring and the turn handle.
    void setCanvasSettings(const CanvasSettings& settings);
    const CanvasSettings& canvasSettings() const { return m_settings; }

    /// Shows @p parts, in draw order (the first at the back). @p selectedPart is outlined,
    /// with its pivot handle at @p selectedPivot.
    void showPuppet(const QVector<Part>& parts, int selectedPart,
                    std::optional<QPointF> selectedPivot, std::optional<JointRange> selectedJoint = std::nullopt);
    /// Whether a pose is shown: only then do the turn handle, pulling and moving joints work.
    /// Without one, dragging a part placed by position still moves it.
    void setPosing(bool posing);
    /// Off while an animation plays: clicks still select parts, but nothing moves or attaches.
    void setEditable(bool editable);
    /// Whether the pivot handle attaches and detaches parts (on by default). Off while
    /// editing an animation's frame, which changes the frame's pose, not the rig.
    void setRigEditable(bool rigEditable);
    /// Drawn behind the parts from the next showPuppet() on; empty for none.
    void setOnionSkins(const QVector<OnionSkin>& skins);
    /// Shows nothing (no puppet is selected).
    void clearPuppet();
    /// Zooms to fit the puppet, and keeps fitting it as the view resizes until the user zooms
    /// or the puppet changes. Nothing else moves the view: edits leave it where it is.
    void fitPuppet();
    /// Fits the whole puppet (the base class fits one side and scrolls the other).
    void initialFit() override;

signals:
    /// A part was clicked (its index in draw order), or -1 for empty space.
    void partClicked(int partIndex);
    /// Right-click on a part (its index in draw order), or -1 for empty space, at @p globalPos.
    void partContextMenuRequested(int partIndex, const QPoint& globalPos);
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
    /// The selected part's pivot handle was dropped on part @p parentIndex, away from its
    /// markers, at @p scenePos: hang it there from a new marker.
    void attachToNewMarkerRequested(int partIndex, int parentIndex, const QPointF& scenePos);
    /// The selected part's pivot handle was double-clicked. @p overlapping lists the parts it
    /// may hang from that are drawn under its pivot, topmost first.
    void pivotDoubleClicked(int partIndex, const QVector<int>& overlapping);
    /// The selected part's pivot handle was dropped away from any part (or with Shift held),
    /// at @p scenePos.
    void detachRequested(int partIndex, const QPointF& scenePos);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
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
    /// Whether @p viewPos is on the selected part's pivot handle, inside its ring; with
    /// @p doubleClick, within the double-click radius.
    bool onPivotHandle(const QPoint& viewPos, bool doubleClick = false) const;
    /// Whether @p viewPos is where dragging changes the selected part: within its move area
    /// (around its pivot, as far as the turn handle) or on its pixels, even under another part.
    bool onSelectedPart(const QPoint& viewPos) const;
    /// Where the selected part's turn handle is, in scene coordinates, if it has one.
    std::optional<QPointF> turnHandle() const;
    bool onTurnHandle(const QPoint& viewPos) const;
    /// The marker of an attach target nearest to @p viewPos, if it is close enough.
    Target targetAt(const QPoint& viewPos) const;
    /// The topmost attach target drawn at @p viewPos, or -1.
    int attachTargetPartAt(const QPoint& viewPos) const;
    void cancelAttach();
    void drawLabel(QPainter* painter, const QPointF& scenePos, const QString& text, const QColor& color);
    /// Draws the grid over @p rect (scene coordinates), unless its cells are too small to see.
    void drawGrid(QPainter* painter, const QRectF& rect);

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
    int m_selectedPart = -1;
    std::optional<QPointF> m_selectedPivot;

    int m_pressedPart = -1;   ///< The movable part under the left button, or -1
    QPoint m_pressPos;
    QPointF m_pressScenePos;
    bool m_dragging = false;

    /// The part a press selects on release when it isn't dragged: the topmost part under it,
    /// pressed where a drag changes the selected part instead. -2 when the press selected at once.
    int m_clickPart = -2;
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
    bool m_rigEditable = true;
    QVector<OnionSkin> m_onionSkins;
    Grid m_grid;
    CanvasSettings m_settings;
    /// Pointer travel, in screen pixels, before a press becomes a drag.
    int dragDistance() const;
    bool m_handlePressed = false;  ///< The left button went down on the pivot handle
    bool m_attaching = false;      ///< The pivot handle is being dragged
    QPointF m_attachPointer;       ///< Where it is, in scene coordinates
    Target m_attachTarget;
    int m_newMarkerPart = -1;  ///< The part a new marker would go on, away from its markers
};
