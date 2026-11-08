#include "PuppetCanvas.h"
#include "AppSettings.h"

#include <cmath>
#include <QApplication>
#include <QGraphicsItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
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
/// Screen pixels around the selected part's pivot that grab its handle.
constexpr int PIVOT_HANDLE_RADIUS = 7;
/// Screen pixels from a marker within which the dragged handle snaps to it.
constexpr int MARKER_SNAP_DISTANCE = 12;
/// Screen size of a point marker's dot while attaching.
constexpr qreal MARKER_DOT_RADIUS = 3.5;
/// Screen radius of the selected part's turn range arc.
constexpr qreal TURN_ARC_RADIUS = 36;
/// Opacity of a part the shown pose doesn't set.
constexpr qreal EXCLUDED_OPACITY = 0.35;
/// Onion skins: how opaque, and their depth, behind every part.
constexpr qreal ONION_SKIN_OPACITY = 0.3;
constexpr qreal ONION_SKIN_Z = -1;
/// Screen radius of the selected part's turn handle, at the end of its angle line.
constexpr qreal TURN_HANDLE_RADIUS = 5;
/// Degrees between the points of a drawn arc.
constexpr double ARC_STEP = 4;

/// The point @p radius from @p center in direction @p degrees (clockwise, y down).
QPointF pointAt(const QPointF& center, double degrees, qreal radius) {
    const double radians = qDegreesToRadians(degrees);
    return center + QPointF(std::cos(radians), std::sin(radians)) * radius;
}

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
    // The workspace grey the other canvases use (Settings changes it): light sprites and
    // their edges show on it, unlike on white.
    setBackgroundBrush(AppSettings().workspaceColor);
    setZoomRange(0.1, 16.0);
    setAlignment(Qt::AlignCenter);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    // A view zoomed out shows more than the scene rect held.
    connect(this, &ZoomableGraphicsView::zoomChanged, this, [this]() { updateSceneRect(); });
    clearPuppet();
}

void PuppetCanvas::setPosing(bool posing) {
    m_posing = posing;
}

void PuppetCanvas::setEditable(bool editable) {
    m_editable = editable;
}

void PuppetCanvas::setRigEditable(bool rigEditable) {
    m_rigEditable = rigEditable;
}

void PuppetCanvas::setOnionSkins(const QVector<OnionSkin>& skins) {
    m_onionSkins = skins;
}

void PuppetCanvas::clearPuppet() {
    showPuppet({}, {}, -1, std::nullopt);
    // No puppet, no origin: the view is left for its empty text.
    m_scene->clear();
}


QPixmap PuppetCanvas::tinted(const QPixmap& image, const QColor& tint) {
    if (image.isNull() || tint.rgb() == QColor(Qt::white).rgb()) return image;
    // Premultiplied channels times the tint stay within their alpha, which is kept.
    QImage result = image.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int red = tint.red();
    const int green = tint.green();
    const int blue = tint.blue();
    for (int y = 0; y < result.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const QRgb pixel = line[x];
            line[x] = qRgba(qRed(pixel) * red / 255, qGreen(pixel) * green / 255, qBlue(pixel) * blue / 255,
                            qAlpha(pixel));
        }
    }
    return QPixmap::fromImage(result);
}

void PuppetCanvas::showPuppet(const QVector<Part>& parts, const QVector<Bone>& bones, int selectedPart,
                              std::optional<QPointF> selectedPivot, std::optional<JointRange> selectedJoint) {
    m_scene->clear();
    m_selectedJoint = selectedJoint;
    m_parts = parts;
    m_bones = bones;
    m_selectedPart = selectedPart;
    m_selectedPivot = selectedPivot;
    // Onion skins behind everything, tinted, and not parts: clicks and fits ignore them.
    for (int skin = 0; skin < m_onionSkins.size(); ++skin) {
        for (const OnionSkin::Ghost& ghost : m_onionSkins[skin].parts) {
            if (ghost.image.isNull()) continue;
            QPixmap tinted = ghost.image;
            {
                QPainter painter(&tinted);
                painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                painter.fillRect(tinted.rect(), m_onionSkins[skin].tint);
            }
            auto* item = m_scene->addPixmap(tinted);
            item->setTransform(ghost.transform);
            item->setTransformationMode(Qt::FastTransformation);
            item->setZValue(ONION_SKIN_Z - skin);
            item->setOpacity(ONION_SKIN_OPACITY * ghost.opacity);
            item->setAcceptedMouseButtons(Qt::NoButton);
        }
    }
    for (int i = 0; i < parts.size(); ++i) {
        const Part& part = parts[i];
        if (!part.placed || part.image.isNull()) continue;
        auto* item = m_scene->addPixmap(tinted(part.image, part.tint));
        item->setTransform(part.transform);
        item->setShapeMode(QGraphicsPixmapItem::MaskShape);
        item->setTransformationMode(Qt::FastTransformation);
        item->setData(PART_INDEX_KEY, i);
        item->setZValue(i);
        // A part the pose leaves alone shows faintly, at rest.
        item->setOpacity(part.opacity * (part.included ? 1.0 : EXCLUDED_OPACITY));
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
    // The origin is always in view, even when every part is off to one side.
    QRectF bounds(0, 0, 0, 0);
    for (QGraphicsItem* item : m_scene->items()) {
        if (item->data(PART_INDEX_KEY).isValid()) bounds |= item->sceneBoundingRect();
    }
    bounds.adjust(-SCENE_MARGIN, -SCENE_MARGIN, SCENE_MARGIN, SCENE_MARGIN);
    // Once the puppet changes, resizing the view no longer refits it: the view stays put.
    if (bounds != m_puppetBounds && !m_puppetBounds.isNull()) setZoomManual(true);
    m_puppetBounds = bounds;
    updateSceneRect();
    viewport()->update();
}

void PuppetCanvas::addCross(const QPointF& at, int halfSize, const QColor& color, qreal z) {
    auto* cross = new CrossItem(halfSize, color);
    cross->setPos(at);
    cross->setZValue(z);
    m_scene->addItem(cross);
}

void PuppetCanvas::updateSceneRect() {
    // The scene rect holds what is in view, and some room around it to scroll to, so a
    // new one never moves the view: the puppet changes, the view stays where it is.
    QRectF rect = m_puppetBounds;
    const QRectF visible = mapToScene(viewport()->rect()).boundingRect();
    if (isVisible() && !visible.isEmpty()) {
        const qreal roomX = visible.width() / 2;
        const qreal roomY = visible.height() / 2;
        rect |= visible.adjusted(-roomX, -roomY, roomX, roomY);
    }
    if (rect == m_scene->sceneRect()) return;
    const QPointF originBefore = viewportTransform().map(QPointF(0, 0));
    m_scene->setSceneRect(rect);
    if (!isVisible() || visible.isEmpty()) return;
    // Scrolled back by whole pixels: centerOn() rounds, and the view would creep.
    const QPointF moved = viewportTransform().map(QPointF(0, 0)) - originBefore;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + qRound(moved.x()));
    verticalScrollBar()->setValue(verticalScrollBar()->value() + qRound(moved.y()));
}

void PuppetCanvas::fitPuppet() {
    setZoomManual(false);
    initialFit();
}

void PuppetCanvas::initialFit() {
    const QRectF rect = m_puppetBounds;
    const QRect view = viewport()->rect().adjusted(1, 1, -1, -1);
    if (rect.isEmpty() || view.isEmpty()) return;
    const double zoom = qMin(MAX_FIT_ZOOM,
                             FIT_FILL * qMin(view.width() / rect.width(), view.height() / rect.height()));
    m_scene->setSceneRect(rect);
    setZoom(zoom);
    centerOn(rect.center());
    updateSceneRect();
}

void PuppetCanvas::resizeEvent(QResizeEvent* event) {
    // The scene point in the middle of the view before the resize: the viewport has its new
    // size already (the event carries the viewport's sizes), but the scroll bars and the
    // transform are still the old ones.
    const QSize oldViewport = event->oldSize();
    const bool keepCenter = isZoomManual() && oldViewport.isValid() && !oldViewport.isEmpty();
    const QPoint scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
    const bool sameView = m_keptCenter && m_keptCenter->scroll == scroll && m_keptCenter->zoom == zoom();
    const QPointF center = sameView ? m_keptCenter->center : mapToScene(QRect(QPoint(), oldViewport).center());
    ZoomableGraphicsView::resizeEvent(event);
    if (!keepCenter) {
        m_keptCenter.reset();
        return;
    }
    // A bigger view may show past the scene rect, which the alignment would re-center.
    updateSceneRect();
    centerOn(center);
    m_keptCenter = KeptCenter{center, QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value()), zoom()};
}

int PuppetCanvas::partAt(const QPoint& viewPos) const {
    for (QGraphicsItem* item : items(viewPos)) {
        const QVariant index = item->data(PART_INDEX_KEY);
        if (index.isValid()) return index.toInt();
    }
    return -1;
}

bool PuppetCanvas::isMovable(int part) const {
    return part >= 0 && part < m_parts.size() && m_parts[part].placed && m_parts[part].movable;
}

bool PuppetCanvas::isAttached(int part) const {
    return part >= 0 && part < m_parts.size() && m_parts[part].placed && m_parts[part].attached;
}

double PuppetCanvas::angleAround(const QPointF& scenePos) const {
    const QPointF from = m_parts[m_turnPart].joint;
    return qRadiansToDegrees(std::atan2(scenePos.y() - from.y(), scenePos.x() - from.x()));
}

bool PuppetCanvas::onPivotHandle(const QPoint& viewPos) const {
    if (!m_selectedPivot) return false;
    const QPoint handle = mapFromScene(*m_selectedPivot);
    return (viewPos - handle).manhattanLength() <= 2 * PIVOT_HANDLE_RADIUS
        && QLineF(viewPos, handle).length() <= PIVOT_HANDLE_RADIUS;
}

std::optional<QPointF> PuppetCanvas::turnHandle() const {
    if (!m_posing || !m_selectedJoint || m_selectedPart < 0 || m_selectedPart >= m_parts.size()) return std::nullopt;
    const JointRange& range = *m_selectedJoint;
    return pointAt(range.point, range.restDirection + range.angle, TURN_ARC_RADIUS / qMax(0.01, zoom()));
}

bool PuppetCanvas::onTurnHandle(const QPoint& viewPos) const {
    const std::optional<QPointF> handle = turnHandle();
    // A little more than the drawn dot, so it's easy to grab.
    return handle && QLineF(QPointF(viewPos), QPointF(mapFromScene(*handle))).length() <= TURN_HANDLE_RADIUS + 3;
}

PuppetCanvas::Target PuppetCanvas::targetAt(const QPoint& viewPos) const {
    Target nearest;
    qreal nearestDistance = MARKER_SNAP_DISTANCE;
    Target inside;
    const QPointF scenePos = mapToScene(viewPos);
    // Front parts first, so a marker drawn over another wins a tie.
    for (int p = m_parts.size() - 1; p >= 0; --p) {
        const Part& part = m_parts[p];
        if (!part.attachTarget || !part.placed) continue;
        for (int m = 0; m < part.markers.size(); ++m) {
            const Marker& marker = part.markers[m];
            const qreal distance = QLineF(QPointF(viewPos), QPointF(mapFromScene(marker.point))).length();
            if (distance <= nearestDistance) {
                nearestDistance = distance;
                nearest = {p, m};
            }
            if (inside.part < 0 && !marker.shape.isEmpty() && marker.shape.contains(scenePos)) inside = {p, m};
        }
    }
    return nearest.part >= 0 ? nearest : inside;
}

void PuppetCanvas::cancelAttach() {
    m_handlePressed = false;
    m_attaching = false;
    m_attachTarget = {};
    viewport()->update();
}

void PuppetCanvas::mousePressEvent(QMouseEvent* event) {
    if (m_editable && m_rigEditable && event->button() == Qt::LeftButton && !m_spacePressed
        && onPivotHandle(event->pos())) {
        setFocus();
        m_handlePressed = true;
        m_pressPos = event->pos();
        event->accept();
        return;
    }
    ZoomableGraphicsView::mousePressEvent(event);
    if (event->button() != Qt::LeftButton || m_isPanning) return;
    const bool onHandle = m_editable && onTurnHandle(event->pos());
    const int part = onHandle ? m_selectedPart : partAt(event->pos());
    if (!m_editable) {
        m_pressedPart = m_pullPart = m_turnPart = -1;
        emit partClicked(part);
        return;
    }
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    // The handle turns any part; dragging a part moves it: a positioned one to a new place,
    // an attached one by pulling it (or, with Shift, by moving its joint).
    m_pressedPart = !onHandle && isMovable(part) ? part : -1;
    m_pullPart = m_posing && !onHandle && isAttached(part) && !shift ? part : -1;
    m_turnPart = onHandle || (m_posing && isAttached(part) && shift) ? part : -1;
    m_drift = !onHandle && shift;
    m_pulling = false;
    m_turning = false;
    m_pressPos = event->pos();
    m_pressScenePos = mapToScene(event->pos());
    m_dragging = false;
    emit partClicked(part);
}

void PuppetCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (m_handlePressed && (event->buttons() & Qt::LeftButton)) {
        if (!m_attaching && (event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance())
            return;
        m_attaching = true;
        m_attachTarget = targetAt(event->pos());
        m_attachPointer = mapToScene(event->pos());
        viewport()->update();
        return;
    }
    if (m_pullPart >= 0 && (event->buttons() & Qt::LeftButton) && !m_isPanning) {
        if (!m_pulling) {
            if ((event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) return;
            m_pulling = true;
            m_dragging = true;
            emit partPullStarted(m_pullPart, m_pressScenePos);
        }
        emit partPulled(m_pullPart, mapToScene(event->pos()));
        return;
    }
    if (m_turnPart >= 0 && (event->buttons() & Qt::LeftButton) && !m_isPanning) {
        const QPointF scenePos = mapToScene(event->pos());
        if (!m_turning) {
            if ((event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) return;
            m_turning = true;
            m_dragging = true;
            m_turnTotal = 0;
            m_turnLastAngle = angleAround(m_pressScenePos);
            emit partTurnStarted(m_turnPart, m_drift);
        }
        if (m_drift) {
            emit partDrifted(m_turnPart, scenePos - m_pressScenePos);
        } else {
            // Unwrapped, so a part can be turned past the half turn and round again.
            const double angle = angleAround(scenePos);
            double step = angle - m_turnLastAngle;
            if (step > 180) step -= 360;
            if (step < -180) step += 360;
            m_turnLastAngle = angle;
            m_turnTotal += step;
            emit partTurned(m_turnPart, m_turnTotal);
        }
        return;
    }
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
        const int hovered = partAt(event->pos());
        const Qt::CursorShape cursor = !m_editable                  ? Qt::ArrowCursor
            : m_rigEditable && onPivotHandle(event->pos())          ? Qt::CrossCursor
            : onTurnHandle(event->pos())                            ? Qt::OpenHandCursor
            : isMovable(hovered)                                    ? Qt::SizeAllCursor
            : isAttached(hovered) && m_posing                       ? Qt::SizeAllCursor
                                                                    : Qt::ArrowCursor;
        viewport()->setCursor(cursor);
    }
}

void PuppetCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (m_handlePressed && event->button() == Qt::LeftButton) {
        const bool attaching = m_attaching;
        const Target target = m_attachTarget;
        const QPointF dropped = mapToScene(event->pos());
        const int part = m_selectedPart;
        cancelAttach();
        if (attaching && target.part >= 0) {
            emit attachRequested(part, target.part, m_parts[target.part].markers[target.marker].name);
        } else if (attaching) {
            emit detachRequested(part, dropped);
        }
        event->accept();
        return;
    }
    const bool dragged = m_dragging;
    if (event->button() == Qt::LeftButton) {
        m_pressedPart = -1;
        m_turnPart = -1;
        m_turning = false;
        m_pullPart = -1;
        m_pulling = false;
        m_dragging = false;
    }
    if (dragged) setZoomManual(true);
    ZoomableGraphicsView::mouseReleaseEvent(event);
}

void PuppetCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && m_handlePressed) {
        cancelAttach();
        event->accept();
        return;
    }
    const int step = (event->modifiers() & Qt::ShiftModifier) ? NUDGE_STEP_LARGE : 1;
    QPoint offset;
    switch (event->key()) {
    case Qt::Key_Left:  offset = QPoint(-step, 0); break;
    case Qt::Key_Right: offset = QPoint(step, 0); break;
    case Qt::Key_Up:    offset = QPoint(0, -step); break;
    case Qt::Key_Down:  offset = QPoint(0, step); break;
    default: break;
    }
    if (m_editable && isMovable(m_selectedPart) && !offset.isNull()) {
        emit partNudged(m_selectedPart, offset);
        event->accept();
        return;
    }
    ZoomableGraphicsView::keyPressEvent(event);
}

void PuppetCanvas::drawLabel(QPainter* painter, const QPointF& scenePos, const QString& text, const QColor& color) {
    // Labels keep their size whatever the zoom: drawn in view pixels.
    painter->save();
    const QPointF at = painter->transform().map(scenePos) + QPointF(6, -6);
    painter->resetTransform();
    painter->setPen(QColor(0, 0, 0, 180));
    painter->drawText(at + QPointF(1, 1), text);
    painter->setPen(color);
    painter->drawText(at, text);
    painter->restore();
}

void PuppetCanvas::drawForeground(QPainter* painter, const QRectF& rect) {
    ZoomableGraphicsView::drawForeground(painter, rect);
    const QColor highlight = palette().color(QPalette::Highlight);

    painter->setRenderHint(QPainter::Antialiasing, true);

    // What hangs from what: faint for every part, strong for the selected one.
    for (const Bone& bone : m_bones) {
        // A dark line under a light one, like the crosses, shows on any sprite.
        QPen under(QColor(0, 0, 0, bone.selected ? 160 : 70), 3, Qt::SolidLine, Qt::RoundCap);
        under.setCosmetic(true);
        painter->setPen(under);
        painter->drawLine(bone.from, bone.to);
        QPen over(bone.selected ? highlight : QColor(255, 255, 255, 140), 1);
        over.setCosmetic(true);
        painter->setPen(over);
        painter->drawLine(bone.from, bone.to);
    }

    // The pivot handle: a ring around the selected part's pivot cross.
    if (m_selectedPivot && m_rigEditable) {
        const qreal radius = PIVOT_HANDLE_RADIUS / qMax(0.01, zoom());
        QPen pen(highlight, 1.5);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(*m_selectedPivot, radius, radius);
    }

    // The selected part's range: where its joint may drift, and how far it may turn.
    if (m_selectedJoint && !m_attaching) {
        const JointRange& range = *m_selectedJoint;
        QPen areaPen(QColor(255, 210, 0), 1, Qt::DashLine);
        areaPen.setCosmetic(true);
        if (!range.area.isEmpty()) {
            painter->setPen(areaPen);
            painter->setBrush(QColor(255, 210, 0, 40));
            painter->drawPath(range.area);
        }
        const qreal radius = TURN_ARC_RADIUS / qMax(0.01, zoom());
        const bool limited = range.angleMin && range.angleMax;
        const double from = range.restDirection + (limited ? *range.angleMin : -180.0);
        const double to = range.restDirection + (limited ? *range.angleMax : 180.0);
        QPolygonF arc;
        arc << range.point;
        for (double a = from; a < to; a += ARC_STEP) arc << pointAt(range.point, a, radius);
        arc << pointAt(range.point, to, radius) << range.point;
        painter->setPen(areaPen);
        painter->setBrush(QColor(255, 210, 0, limited ? 40 : 15));
        painter->drawPolygon(arc);
        QPen nowPen(highlight, 2);
        nowPen.setCosmetic(true);
        painter->setPen(nowPen);
        painter->drawLine(range.point, pointAt(range.point, range.restDirection + range.angle, radius));
        const QPointF handle = pointAt(range.point, range.restDirection + range.angle, radius);
        if (m_posing) {
            const qreal handleRadius = TURN_HANDLE_RADIUS / qMax(0.01, zoom());
            QPen handlePen(QColor(0, 0, 0, 160), 1);
            handlePen.setCosmetic(true);
            painter->setPen(handlePen);
            painter->setBrush(highlight);
            painter->drawEllipse(handle, handleRadius, handleRadius);
        }
        if (std::lround(range.angle) != 0)
            drawLabel(painter, handle, QStringLiteral("%1°").arg(std::lround(range.angle)), highlight);
    }

    const qreal dot = MARKER_DOT_RADIUS / qMax(0.01, zoom());
    if (!m_attaching) {
        // Every part's markers, faint, and the selected part's, labelled. Pivots have their
        // crosses and bones.
        for (int p = 0; p < m_parts.size(); ++p) {
            const Part& part = m_parts[p];
            if (!part.placed) continue;
            const bool selected = p == m_selectedPart;
            const QColor color(255, 210, 0, selected ? 230 : 120);
            QPen pen(color, 1);
            pen.setCosmetic(true);
            for (const Marker& marker : part.markers) {
                if (marker.name == QLatin1String("pivot")) continue;
                painter->setPen(pen);
                painter->setBrush(Qt::NoBrush);
                if (!marker.shape.isEmpty()) painter->drawPath(marker.shape);
                painter->setBrush(color);
                painter->drawEllipse(marker.point, dot * 0.7, dot * 0.7);
                if (selected) drawLabel(painter, marker.point, marker.name, color);
            }
        }
        return;
    }
    for (int p = 0; p < m_parts.size(); ++p) {
        const Part& part = m_parts[p];
        if (!part.attachTarget || !part.placed) continue;
        for (int m = 0; m < part.markers.size(); ++m) {
            const Marker& marker = part.markers[m];
            const bool hovered = m_attachTarget.part == p && m_attachTarget.marker == m;
            const QColor color = hovered ? highlight : QColor(255, 210, 0);
            QPen pen(color, hovered ? 2 : 1);
            pen.setCosmetic(true);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            if (!marker.shape.isEmpty()) painter->drawPath(marker.shape);
            painter->setBrush(color);
            painter->drawEllipse(marker.point, dot, dot);
            drawLabel(painter, marker.point, marker.name, color);
        }
    }
    // The joint being dragged, from where it is now to where it would go.
    if (m_selectedPivot) {
        const QPointF end = m_attachTarget.part >= 0
            ? m_parts[m_attachTarget.part].markers[m_attachTarget.marker].point
            : m_attachPointer;
        QPen pen(highlight, 1, Qt::DashLine);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->drawLine(*m_selectedPivot, end);
        painter->setBrush(highlight);
        painter->drawEllipse(end, dot, dot);
    }
}
