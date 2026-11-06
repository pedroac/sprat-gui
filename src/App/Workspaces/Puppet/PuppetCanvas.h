#pragma once
#include <optional>
#include <QPixmap>
#include <QPointF>
#include <QTransform>
#include <QVector>
#include "ZoomableGraphicsView.h"

class QGraphicsScene;

/**
 * @brief Draws a puppet in its rest pose, with a large cross at the puppet's origin and a
 *        small one at the selected part's pivot. Clicking a part selects it; dragging a
 *        movable part, or the arrow keys on the selected one, asks to move it.
 *
 * Scene coordinates are the puppet's space: origin = the puppet's pivot, Y down, in the
 * sprites' original-image pixels.
 */
class PuppetCanvas : public ZoomableGraphicsView {
    Q_OBJECT
public:
    /// A part as drawn: its shown sprite's image and where PuppetEvaluator placed it.
    struct Part {
        QPixmap image;
        QTransform transform;
        bool placed = false;
        bool movable = false;  ///< Placed by its position, so dragging it moves it
    };

    explicit PuppetCanvas(QWidget* parent = nullptr);

    /// Shows @p parts, in draw order (the first at the back). @p selectedPart is outlined,
    /// with a cross at @p selectedPivot.
    void showPuppet(const QVector<Part>& parts, int selectedPart, std::optional<QPointF> selectedPivot);
    /// Shows only the origin cross.
    void clearPuppet();
    /// Zooms to fit the puppet, and keeps fitting it as the view resizes until the user zooms.
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
    /// The arrow keys asked to move the selected part by @p offset.
    void partNudged(int partIndex, const QPoint& offset);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    /// The part drawn at @p viewPos (topmost), or -1.
    int partAt(const QPoint& viewPos) const;

    void addCross(const QPointF& at, int halfSize, const QColor& color, qreal z);
    void updateSceneRect();

    QGraphicsScene* m_scene = nullptr;
    QVector<bool> m_movable;  ///< By part index, as last shown
    int m_selectedPart = -1;
    int m_pressedPart = -1;   ///< The movable part under the left button, or -1
    QPoint m_pressPos;
    QPointF m_pressScenePos;
    bool m_dragging = false;
};
