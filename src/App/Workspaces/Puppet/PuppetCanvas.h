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
 *        small one at the selected part's pivot. Clicking a part selects it.
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

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    void addCross(const QPointF& at, int halfSize, const QColor& color, qreal z);
    void updateSceneRect();

    QGraphicsScene* m_scene = nullptr;
};
