#include "PuppetCanvas.h"

#include <QApplication>
#include <QGraphicsItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

namespace {

/// Space around the puppet, in scene pixels, so it never touches the view's edge.
constexpr qreal SCENE_MARGIN = 64;
/// Most a fit zooms in, so a tiny puppet isn't blown up to a blur of pixels.
constexpr double MAX_FIT_ZOOM = 4.0;
/// Share of the view a fit fills: an exact fit makes the scroll bars flicker on and off.
constexpr double FIT_FILL = 0.95;
constexpr int ORIGIN_CROSS_HALF_SIZE = 24;
constexpr int PIVOT_CROSS_HALF_SIZE = 8;
/// Pixels Shift+arrow moves a part; an arrow alone moves it one.
constexpr int NUDGE_STEP_LARGE = 10;

/// The key of a pixmap item's part index (QGraphicsItem::data).
constexpr int PART_INDEX_KEY = 0;

/// A cross of a fixed size on screen, whatever the zoom.
class CrossItem : public QGraphicsItem {
public:
    CrossItem(int halfSize, const QColor& color) : m_halfSize(halfSize), m_color(color) {
        setFlag(ItemIgnoresTransformations);
        setAcceptedMouseButtons(Qt::NoButton);
    }

    QRectF boundingRect() const override {
        return QRectF(-m_halfSize - 2, -m_halfSize - 2, 2 * m_halfSize + 4, 2 * m_halfSize + 4);
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
        // A dark outline under the colored line keeps the cross visible on any sprite.
        for (const auto& [color, width] : {std::pair{QColor(0, 0, 0, 160), 3.0}, std::pair{m_color, 1.0}}) {
            painter->setPen(QPen(color, width));
            painter->drawLine(QPointF(-m_halfSize, 0), QPointF(m_halfSize, 0));
            painter->drawLine(QPointF(0, -m_halfSize), QPointF(0, m_halfSize));
        }
    }

private:
    int m_halfSize;
    QColor m_color;
};

}  // namespace

PuppetCanvas::PuppetCanvas(QWidget* parent)
    : ZoomableGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setZoomRange(0.1, 16.0);
    setAlignment(Qt::AlignCenter);
    clearPuppet();
}

void PuppetCanvas::clearPuppet() {
    showPuppet({}, -1, std::nullopt);
}

void PuppetCanvas::showPuppet(const QVector<Part>& parts, int selectedPart, std::optional<QPointF> selectedPivot) {
    m_scene->clear();
    m_movable.clear();
    for (const Part& part : parts) m_movable.append(part.placed && part.movable);
    m_selectedPart = selectedPart;
    for (int i = 0; i < parts.size(); ++i) {
        const Part& part = parts[i];
        if (!part.placed || part.image.isNull()) continue;
        auto* item = m_scene->addPixmap(part.image);
        item->setTransform(part.transform);
        item->setShapeMode(QGraphicsPixmapItem::MaskShape);
        item->setTransformationMode(Qt::FastTransformation);
        item->setData(PART_INDEX_KEY, i);
        item->setZValue(i);
    }
    if (selectedPart >= 0 && selectedPart < parts.size() && parts[selectedPart].placed) {
        const Part& part = parts[selectedPart];
        auto* outline = m_scene->addPolygon(part.transform.map(QRectF(part.image.rect())));
        QPen pen(palette().color(QPalette::Highlight), 0);
        pen.setStyle(Qt::DashLine);
        outline->setPen(pen);
        outline->setZValue(parts.size());
        outline->setAcceptedMouseButtons(Qt::NoButton);
    }
    addCross(QPointF(0, 0), ORIGIN_CROSS_HALF_SIZE, QColor(255, 255, 255), parts.size() + 1);
    if (selectedPivot)
        addCross(*selectedPivot, PIVOT_CROSS_HALF_SIZE, palette().color(QPalette::Highlight), parts.size() + 2);
    // A new scene rect while dragging would shift the view under the pointer.
    if (!m_dragging) updateSceneRect();
}

void PuppetCanvas::addCross(const QPointF& at, int halfSize, const QColor& color, qreal z) {
    auto* cross = new CrossItem(halfSize, color);
    cross->setPos(at);
    cross->setZValue(z);
    m_scene->addItem(cross);
}

void PuppetCanvas::updateSceneRect() {
    // The origin is always in view, even when every part is off to one side.
    QRectF bounds(0, 0, 0, 0);
    for (QGraphicsItem* item : m_scene->items()) {
        if (item->data(PART_INDEX_KEY).isValid()) bounds |= item->sceneBoundingRect();
    }
    m_scene->setSceneRect(bounds.adjusted(-SCENE_MARGIN, -SCENE_MARGIN, SCENE_MARGIN, SCENE_MARGIN));
}

void PuppetCanvas::fitPuppet() {
    setZoomManual(false);
    initialFit();
}

void PuppetCanvas::initialFit() {
    const QRectF rect = m_scene->sceneRect();
    const QRect view = viewport()->rect().adjusted(1, 1, -1, -1);
    if (rect.isEmpty() || view.isEmpty()) return;
    const double zoom = qMin(MAX_FIT_ZOOM,
                             FIT_FILL * qMin(view.width() / rect.width(), view.height() / rect.height()));
    setZoom(zoom);
    centerOn(rect.center());
}

int PuppetCanvas::partAt(const QPoint& viewPos) const {
    for (QGraphicsItem* item : items(viewPos)) {
        const QVariant index = item->data(PART_INDEX_KEY);
        if (index.isValid()) return index.toInt();
    }
    return -1;
}

void PuppetCanvas::mousePressEvent(QMouseEvent* event) {
    ZoomableGraphicsView::mousePressEvent(event);
    if (event->button() != Qt::LeftButton || m_isPanning) return;
    const int part = partAt(event->pos());
    m_pressedPart = (part >= 0 && part < m_movable.size() && m_movable[part]) ? part : -1;
    m_pressPos = event->pos();
    m_pressScenePos = mapToScene(event->pos());
    m_dragging = false;
    emit partClicked(part);
}

void PuppetCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (m_pressedPart >= 0 && (event->buttons() & Qt::LeftButton) && !m_isPanning) {
        if (!m_dragging) {
            if ((event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) return;
            m_dragging = true;
            emit partDragStarted(m_pressedPart);
        }
        const QPointF offset = mapToScene(event->pos()) - m_pressScenePos;
        emit partDragged(m_pressedPart, QPoint(qRound(offset.x()), qRound(offset.y())));
        return;
    }
    ZoomableGraphicsView::mouseMoveEvent(event);
    if (event->buttons() == Qt::NoButton) {
        const int part = partAt(event->pos());
        const bool movable = part >= 0 && part < m_movable.size() && m_movable[part];
        viewport()->setCursor(movable ? Qt::SizeAllCursor : Qt::ArrowCursor);
    }
}

void PuppetCanvas::mouseReleaseEvent(QMouseEvent* event) {
    const bool dragged = m_dragging;
    if (event->button() == Qt::LeftButton) {
        m_pressedPart = -1;
        m_dragging = false;
    }
    if (dragged) updateSceneRect();
    ZoomableGraphicsView::mouseReleaseEvent(event);
}

void PuppetCanvas::keyPressEvent(QKeyEvent* event) {
    const bool movable = m_selectedPart >= 0 && m_selectedPart < m_movable.size() && m_movable[m_selectedPart];
    const int step = (event->modifiers() & Qt::ShiftModifier) ? NUDGE_STEP_LARGE : 1;
    QPoint offset;
    switch (event->key()) {
    case Qt::Key_Left:  offset = QPoint(-step, 0); break;
    case Qt::Key_Right: offset = QPoint(step, 0); break;
    case Qt::Key_Up:    offset = QPoint(0, -step); break;
    case Qt::Key_Down:  offset = QPoint(0, step); break;
    default: break;
    }
    if (movable && !offset.isNull()) {
        emit partNudged(m_selectedPart, offset);
        event->accept();
        return;
    }
    ZoomableGraphicsView::keyPressEvent(event);
}
