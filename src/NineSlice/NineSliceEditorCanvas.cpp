#include "NineSliceEditorCanvas.h"
#include "ViewUtils.h"

#include <QGraphicsSimpleTextItem>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QCursor>
#include <QSvgRenderer>

namespace {
class GridOverlayItem : public QGraphicsItem {
public:
    GridOverlayItem() { setZValue(0.25); }

    void setParams(int cellW, int cellH, int offX, int offY,
                   const QColor& color, const QSizeF& size)
    {
        m_cellW = qMax(1, cellW);
        m_cellH = qMax(1, cellH);
        m_offX  = offX;
        m_offY  = offY;
        m_color = color;
        m_size  = size;
        update();
    }

    QRectF boundingRect() const override {
        return QRectF(QPointF(0, 0), m_size);
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
        if (m_size.isEmpty()) return;
        QPen pen(m_color, 0);
        pen.setCosmetic(true);
        painter->setPen(pen);

        const int w = qRound(m_size.width());
        const int h = qRound(m_size.height());

        const int startX = ((m_offX % m_cellW) + m_cellW) % m_cellW;
        for (int x = startX; x < w; x += m_cellW)
            painter->drawLine(x, 0, x, h);

        const int startY = ((m_offY % m_cellH) + m_cellH) % m_cellH;
        for (int y = startY; y < h; y += m_cellH)
            painter->drawLine(0, y, w, y);
    }

private:
    int    m_cellW = 16, m_cellH = 16;
    int    m_offX  = 0,  m_offY  = 0;
    QColor m_color = QColor(255, 255, 255, 80);
    QSizeF m_size;
};
} // namespace

// ============================================================
// SliceLineItem
// ============================================================

SliceLineItem::SliceLineItem(Edge edge, QGraphicsItem* parent)
    : QGraphicsLineItem(parent), m_edge(edge)
{
    setPen(QPen(QColor(255, 80, 80, 200), 2));
    setFlag(QGraphicsItem::ItemIsMovable, false);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    setAcceptHoverEvents(true);

    bool horizontal = (edge == Left || edge == Right);
    setCursor(horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
    setZValue(100);
}

void SliceLineItem::setConstraint(qreal minVal, qreal maxVal)
{
    m_minVal = minVal;
    m_maxVal = maxVal;
}

void SliceLineItem::setPosition(int pos)
{
    m_pos = pos;
    prepareGeometryChange();
    if (m_edge == Left || m_edge == Right) {
        setLine(pos, line().y1(), pos, line().y2());
    } else {
        setLine(line().x1(), pos, line().x2(), pos);
    }
}

void SliceLineItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        event->accept();
    } else {
        QGraphicsLineItem::mousePressEvent(event);
    }
}

void SliceLineItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (!m_dragging) {
        QGraphicsLineItem::mouseMoveEvent(event);
        return;
    }

    QPointF scenePos = event->scenePos();
    int newPos;

    if (m_edge == Left || m_edge == Right) {
        newPos = qBound(static_cast<int>(m_minVal), static_cast<int>(scenePos.x()), static_cast<int>(m_maxVal));
    } else {
        newPos = qBound(static_cast<int>(m_minVal), static_cast<int>(scenePos.y()), static_cast<int>(m_maxVal));
    }

    if (newPos != m_pos) {
        setPosition(newPos);
        emit positionChanged(m_edge, m_pos);
    }
    event->accept();
}

void SliceLineItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
    } else {
        QGraphicsLineItem::mouseReleaseEvent(event);
    }
}

QVariant SliceLineItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    return QGraphicsLineItem::itemChange(change, value);
}

// ============================================================
// ResizeHandleItem
// ============================================================

ResizeHandleItem::ResizeHandleItem(Axis axis, QGraphicsItem* parent)
    : QGraphicsRectItem(parent), m_axis(axis)
{
    setBrush(QBrush(QColor(255, 200, 80, 160)));
    setPen(Qt::NoPen);
    setAcceptHoverEvents(true);
    setZValue(200);
    if (axis == H)      setCursor(Qt::SizeHorCursor);
    else if (axis == V) setCursor(Qt::SizeVerCursor);
    else                setCursor(Qt::SizeFDiagCursor);
}

void ResizeHandleItem::setIconRenderer(QSvgRenderer* renderer, bool rotate)
{
    m_iconRenderer = renderer;
    m_rotateIcon   = rotate;
}

void ResizeHandleItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    QGraphicsRectItem::paint(painter, option, widget);
    if (!m_iconRenderer || !m_iconRenderer->isValid()) return;

    QRectF r = rect();
    // Size the icon to fill the short axis of the handle with 1 px padding each side.
    const qreal iconSize = qMin(r.width(), r.height()) - 2.0;
    if (iconSize <= 0) return;

    const QPointF centre = r.center();
    QRectF iconRect(centre.x() - iconSize / 2, centre.y() - iconSize / 2, iconSize, iconSize);

    if (m_rotateIcon) {
        painter->save();
        painter->translate(centre);
        painter->rotate(90);
        painter->translate(-iconSize / 2, -iconSize / 2);
        m_iconRenderer->render(painter, QRectF(0, 0, iconSize, iconSize));
        painter->restore();
    } else {
        m_iconRenderer->render(painter, iconRect);
    }
}

void ResizeHandleItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        // Record how far inside the handle the click landed (from the controlled edge).
        // rect().left()/top() is the scene position of the target edge.
        m_clickOffsetX = (m_axis != V) ? event->scenePos().x() - rect().left() : 0.0;
        m_clickOffsetY = (m_axis != H) ? event->scenePos().y() - rect().top()  : 0.0;
        event->accept();
    } else {
        QGraphicsRectItem::mousePressEvent(event);
    }
}

void ResizeHandleItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (!m_dragging) {
        QGraphicsRectItem::mouseMoveEvent(event);
        return;
    }
    // Compute the absolute desired edge position in scene space by subtracting the
    // fixed click offset.  This keeps the edge pinned to the cursor regardless of
    // zoom level or how far the handle has moved since the press.
    int newW = (m_axis != V) ? qRound(event->scenePos().x() - m_clickOffsetX) : 0;
    int newH = (m_axis != H) ? qRound(event->scenePos().y() - m_clickOffsetY) : 0;
    emit dragged(newW, newH);
    event->accept();
}

void ResizeHandleItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
    } else {
        QGraphicsRectItem::mouseReleaseEvent(event);
    }
}

// ============================================================
// NineSliceEditorCanvas
// ============================================================

NineSliceEditorCanvas::NineSliceEditorCanvas(QWidget* parent)
    : ZoomableGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing, false);
    setDragMode(QGraphicsView::NoDrag);
    setBackgroundBrush(m_settings.workspaceColor);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);

    // Slice lines
    m_lineLeft   = new SliceLineItem(SliceLineItem::Left);
    m_lineTop    = new SliceLineItem(SliceLineItem::Top);
    m_lineRight  = new SliceLineItem(SliceLineItem::Right);
    m_lineBottom = new SliceLineItem(SliceLineItem::Bottom);

    m_scene->addItem(m_lineLeft);
    m_scene->addItem(m_lineTop);
    m_scene->addItem(m_lineRight);
    m_scene->addItem(m_lineBottom);

    m_lineLeft->hide();
    m_lineTop->hide();
    m_lineRight->hide();
    m_lineBottom->hide();

    connect(m_lineLeft,   &SliceLineItem::positionChanged, this, &NineSliceEditorCanvas::onLinePositionChanged);
    connect(m_lineTop,    &SliceLineItem::positionChanged, this, &NineSliceEditorCanvas::onLinePositionChanged);
    connect(m_lineRight,  &SliceLineItem::positionChanged, this, &NineSliceEditorCanvas::onLinePositionChanged);
    connect(m_lineBottom, &SliceLineItem::positionChanged, this, &NineSliceEditorCanvas::onLinePositionChanged);

    // Resize handles
    m_rightHandle  = new ResizeHandleItem(ResizeHandleItem::H);
    m_bottomHandle = new ResizeHandleItem(ResizeHandleItem::V);
    m_cornerHandle = new ResizeHandleItem(ResizeHandleItem::Both);

    m_scene->addItem(m_rightHandle);
    m_scene->addItem(m_bottomHandle);
    m_scene->addItem(m_cornerHandle);

    m_rightHandle->hide();
    m_bottomHandle->hide();
    m_cornerHandle->hide();

    connect(m_rightHandle,  &ResizeHandleItem::dragged, this, [this](int w, int  ) { resizeTo(w,                    m_targetSize.height()); });
    connect(m_bottomHandle, &ResizeHandleItem::dragged, this, [this](int  , int h) { resizeTo(m_targetSize.width(), h                   ); });
    connect(m_cornerHandle, &ResizeHandleItem::dragged, this, [this](int w, int h) { resizeTo(w,                    h                   ); });

    // Handle icons
    m_dragHandleRenderer   = new QSvgRenderer(QStringLiteral(":/icons/drag-handle.svg"), this);
    m_resizeHandleRenderer = new QSvgRenderer(QStringLiteral(":/icons/resize-handle.svg"), this);
    m_rightHandle ->setIconRenderer(m_dragHandleRenderer);
    m_bottomHandle->setIconRenderer(m_dragHandleRenderer, /*rotate=*/true);
    m_cornerHandle->setIconRenderer(m_resizeHandleRenderer);

    // Line labels
    QFont labelFont;
    labelFont.setPixelSize(11);
    labelFont.setBold(true);

    auto makeLabel = [&](const QString& text) -> QGraphicsSimpleTextItem* {
        auto* item = new QGraphicsSimpleTextItem(text);
        item->setFont(labelFont);
        item->setBrush(QColor(255, 80, 80, 220));
        item->setZValue(150);
        item->hide();
        m_scene->addItem(item);
        return item;
    };
    m_labelLeft   = makeLabel(QStringLiteral("L"));
    m_labelRight  = makeLabel(QStringLiteral("R"));
    m_labelTop    = makeLabel(QStringLiteral("T"));
    m_labelBottom = makeLabel(QStringLiteral("B"));
}

void NineSliceEditorCanvas::setSpriteImage(const QString& path)
{
    if (path == m_currentPath) return;
    m_currentPath = path;

    QPixmap pix(path);
    if (pix.isNull()) {
        clearSprite();
        return;
    }

    m_sourcePixmap = pix;
    m_targetSize   = pix.size();

    int w = pix.width();
    int h = pix.height();
    m_insetLeft   = w / 3;
    m_insetTop    = h / 3;
    m_insetRight  = w / 3;
    m_insetBottom = h / 3;

    m_lineLeft->show();
    m_lineTop->show();
    m_lineRight->show();
    m_lineBottom->show();
    m_rightHandle->show();
    m_bottomHandle->show();
    m_cornerHandle->show();
    m_labelLeft->show();
    m_labelRight->show();
    m_labelTop->show();
    m_labelBottom->show();

    renderComposite();
    initialFit();
    emit insetsChanged(m_insetLeft, m_insetTop, m_insetRight, m_insetBottom);
    emit targetSizeChanged(m_targetSize.width(), m_targetSize.height());
}

void NineSliceEditorCanvas::clearSprite()
{
    if (m_imageItem) {
        m_scene->removeItem(m_imageItem);
        delete m_imageItem;
        m_imageItem = nullptr;
    }
    if (m_bgItem) {
        m_scene->removeItem(m_bgItem);
        delete m_bgItem;
        m_bgItem = nullptr;
    }
    if (m_gridItem) {
        m_scene->removeItem(m_gridItem);
        delete m_gridItem;
        m_gridItem = nullptr;
    }
    m_currentPath.clear();
    m_sourcePixmap = QPixmap();
    m_targetSize   = QSize();
    m_insetLeft = m_insetTop = m_insetRight = m_insetBottom = 0;

    m_lineLeft->hide();
    m_lineTop->hide();
    m_lineRight->hide();
    m_lineBottom->hide();
    m_rightHandle->hide();
    m_bottomHandle->hide();
    m_cornerHandle->hide();
    m_labelLeft->hide();
    m_labelRight->hide();
    m_labelTop->hide();
    m_labelBottom->hide();

    for (auto* r : m_regionItems) {
        m_scene->removeItem(r);
        delete r;
    }
    m_regionItems.clear();
}

void NineSliceEditorCanvas::setInsets(int left, int top, int right, int bottom)
{
    if (m_sourcePixmap.isNull()) return;

    int sw = m_sourcePixmap.width();
    int sh = m_sourcePixmap.height();
    m_insetLeft   = qBound(0, left,   sw);
    m_insetTop    = qBound(0, top,    sh);
    m_insetRight  = qBound(0, right,  sw);
    m_insetBottom = qBound(0, bottom, sh);

    renderComposite();
}

void NineSliceEditorCanvas::setTargetSize(int width, int height)
{
    QSize newSize(width, height);
    newSize.setWidth( qMax(newSize.width(),  qMax(10, m_insetLeft + m_insetRight  + 1)));
    newSize.setHeight(qMax(newSize.height(), qMax(10, m_insetTop  + m_insetBottom + 1)));
    if (newSize == m_targetSize) return;
    m_targetSize = newSize;
    renderComposite();
    emit targetSizeChanged(m_targetSize.width(), m_targetSize.height());
}

void NineSliceEditorCanvas::setFillModes(const QString& hMode, const QString& vMode)
{
    m_hMode = hMode;
    m_vMode = vMode;
    renderComposite();
}

void NineSliceEditorCanvas::initialFit()
{
    if (m_imageItem) {
        fitInView(m_imageItem, Qt::KeepAspectRatio);
        QTransform t = transform();
        double z = t.m11();
        if (z > m_maxZoom) setZoom(m_maxZoom);
        else if (z < m_minZoom) setZoom(m_minZoom);
    }
}

void NineSliceEditorCanvas::onLinePositionChanged(SliceLineItem::Edge edge, int pos)
{
    switch (edge) {
        case SliceLineItem::Left:   m_insetLeft   = pos; break;
        case SliceLineItem::Top:    m_insetTop    = pos; break;
        case SliceLineItem::Right:  m_insetRight  = m_targetSize.width()  - pos; break;
        case SliceLineItem::Bottom: m_insetBottom = m_targetSize.height() - pos; break;
    }
    renderComposite();
    emit insetsChanged(m_insetLeft, m_insetTop, m_insetRight, m_insetBottom);
}

void NineSliceEditorCanvas::resizeTo(int w, int h)
{
    QSize newSize(w, h);
    newSize.setWidth( qMax(newSize.width(),  qMax(10, m_insetLeft + m_insetRight  + 1)));
    newSize.setHeight(qMax(newSize.height(), qMax(10, m_insetTop  + m_insetBottom + 1)));
    if (newSize == m_targetSize) return;
    m_targetSize = newSize;
    renderComposite();
    emit targetSizeChanged(m_targetSize.width(), m_targetSize.height());
}

// Draw a source tile into destRect, applying hMode / vMode (stretch / repeat / mirror).
// Mixed cases (one axis stretch, one axis tile) are handled correctly:
//   hStretch + vTile  → each tiled row is scaled to the full destination width
//   hTile   + vStretch → each tiled column is scaled to the full destination height
static void drawTiled(QPainter& painter, const QPixmap& tile,
                      const QRect& destRect,
                      const QString& hMode, const QString& vMode)
{
    if (tile.isNull() || destRect.isEmpty()) return;
    const int tw = tile.width();
    const int th = tile.height();
    if (tw <= 0 || th <= 0) return;

    const bool hStretch = (hMode == QLatin1String("stretch"));
    const bool vStretch = (vMode == QLatin1String("stretch"));

    if (hStretch && vStretch) {
        painter.drawPixmap(destRect, tile);
        return;
    }

    QPixmap tiled(destRect.size());
    tiled.fill(Qt::transparent);
    QPainter tp(&tiled);

    if (hStretch) {
        // Stretch horizontally, tile vertically
        for (int y = 0; y < destRect.height(); y += th) {
            QPixmap src = tile;
            if (vMode == QLatin1String("mirror") && ((y / th) & 1))
                src = src.transformed(QTransform().scale(1, -1));
            int drawH = qMin(th, destRect.height() - y);
            tp.drawPixmap(QRect(0, y, destRect.width(), drawH),
                          src, QRect(0, 0, tw, drawH));
        }
    } else if (vStretch) {
        // Tile horizontally, stretch vertically
        for (int x = 0; x < destRect.width(); x += tw) {
            QPixmap src = tile;
            if (hMode == QLatin1String("mirror") && ((x / tw) & 1))
                src = src.transformed(QTransform().scale(-1, 1));
            int drawW = qMin(tw, destRect.width() - x);
            tp.drawPixmap(QRect(x, 0, drawW, destRect.height()),
                          src, QRect(0, 0, drawW, th));
        }
    } else {
        // Tile both axes
        for (int y = 0; y < destRect.height(); y += th) {
            for (int x = 0; x < destRect.width(); x += tw) {
                QPixmap src = tile;
                if (hMode == QLatin1String("mirror") && ((x / tw) & 1))
                    src = src.transformed(QTransform().scale(-1, 1));
                if (vMode == QLatin1String("mirror") && ((y / th) & 1))
                    src = src.transformed(QTransform().scale(1, -1));
                int drawW = qMin(tw, destRect.width()  - x);
                int drawH = qMin(th, destRect.height() - y);
                tp.drawPixmap(x, y, src, 0, 0, drawW, drawH);
            }
        }
    }

    tp.end();
    painter.drawPixmap(destRect.topLeft(), tiled);
}

void NineSliceEditorCanvas::renderComposite()
{
    if (m_sourcePixmap.isNull() || m_targetSize.isEmpty()) return;

    int tw = m_targetSize.width();
    int th = m_targetSize.height();
    int sw = m_sourcePixmap.width();
    int sh = m_sourcePixmap.height();

    // Clamp insets so corners never exceed half the source size
    int l = qBound(0, m_insetLeft,   sw / 2);
    int r = qBound(0, m_insetRight,  sw / 2);
    int t = qBound(0, m_insetTop,    sh / 2);
    int b = qBound(0, m_insetBottom, sh / 2);

    QPixmap composite(tw, th);
    composite.fill(Qt::transparent);
    QPainter painter(&composite);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    // Source rects
    QRect srcTL(0,      0,      l,          t         );
    QRect srcT (l,      0,      sw - l - r, t         );
    QRect srcTR(sw - r, 0,      r,          t         );
    QRect srcL (0,      t,      l,          sh - t - b);
    QRect srcC (l,      t,      sw - l - r, sh - t - b);
    QRect srcR (sw - r, t,      r,          sh - t - b);
    QRect srcBL(0,      sh - b, l,          b         );
    QRect srcB (l,      sh - b, sw - l - r, b         );
    QRect srcBR(sw - r, sh - b, r,          b         );

    // Destination rects
    QRect dstTL(0,      0,      l,          t         );
    QRect dstT (l,      0,      tw - l - r, t         );
    QRect dstTR(tw - r, 0,      r,          t         );
    QRect dstL (0,      t,      l,          th - t - b);
    QRect dstC (l,      t,      tw - l - r, th - t - b);
    QRect dstR (tw - r, t,      r,          th - t - b);
    QRect dstBL(0,      th - b, l,          b         );
    QRect dstB (l,      th - b, tw - l - r, b         );
    QRect dstBR(tw - r, th - b, r,          b         );

    static const QString kStretch = QStringLiteral("stretch");

    // Corners: always 1:1 copy
    auto drawCorner = [&](const QRect& src, const QRect& dst) {
        if (!src.isEmpty() && !dst.isEmpty())
            painter.drawPixmap(dst, m_sourcePixmap, src);
    };
    drawCorner(srcTL, dstTL);
    drawCorner(srcTR, dstTR);
    drawCorner(srcBL, dstBL);
    drawCorner(srcBR, dstBR);

    // Horizontal edges: H fill applies, V is fixed
    drawTiled(painter, m_sourcePixmap.copy(srcT),  dstT,  m_hMode,  kStretch);
    drawTiled(painter, m_sourcePixmap.copy(srcB),  dstB,  m_hMode,  kStretch);

    // Vertical edges: V fill applies, H is fixed
    drawTiled(painter, m_sourcePixmap.copy(srcL),  dstL,  kStretch, m_vMode);
    drawTiled(painter, m_sourcePixmap.copy(srcR),  dstR,  kStretch, m_vMode);

    // Center: both fill modes apply
    drawTiled(painter, m_sourcePixmap.copy(srcC),  dstC,  m_hMode,  m_vMode);

    painter.end();

    if (m_imageItem) {
        m_scene->removeItem(m_imageItem);
        delete m_imageItem;
    }
    m_imageItem = m_scene->addPixmap(composite);
    m_imageItem->setZValue(0);

    m_scene->setSceneRect(-20, -20, tw + 40, th + 40);
    updateBgItem();
    updateGridItem();
    updateLineGeometry();
    updateHandlePositions();
    rebuildOverlay();
}

void NineSliceEditorCanvas::updateLineGeometry()
{
    int tw = m_targetSize.width();
    int th = m_targetSize.height();

    m_lineLeft->setConstraint(0, tw);
    m_lineLeft->setLine(0, 0, 0, th);
    m_lineLeft->setPosition(m_insetLeft);

    m_lineTop->setConstraint(0, th);
    m_lineTop->setLine(0, 0, tw, 0);
    m_lineTop->setPosition(m_insetTop);

    m_lineRight->setConstraint(0, tw);
    m_lineRight->setLine(0, 0, 0, th);
    m_lineRight->setPosition(tw - m_insetRight);

    m_lineBottom->setConstraint(0, th);
    m_lineBottom->setLine(0, 0, tw, 0);
    m_lineBottom->setPosition(th - m_insetBottom);

    // Position labels: L/R centered above their vertical lines, T/B left of their horizontal lines
    auto posAbove = [](QGraphicsSimpleTextItem* label, qreal lineX) {
        QRectF br = label->boundingRect();
        label->setPos(lineX - br.width() / 2, -br.height() - 3);
    };
    auto posLeft = [](QGraphicsSimpleTextItem* label, qreal lineY) {
        QRectF br = label->boundingRect();
        label->setPos(-br.width() - 3, lineY - br.height() / 2);
    };
    posAbove(m_labelLeft,   m_insetLeft);
    posAbove(m_labelRight,  tw - m_insetRight);
    posLeft (m_labelTop,    m_insetTop);
    posLeft (m_labelBottom, th - m_insetBottom);
}

void NineSliceEditorCanvas::updateHandlePositions()
{
    int tw = m_targetSize.width();
    int th = m_targetSize.height();
    const int hs = 8;  // handle thickness in scene pixels

    m_rightHandle->setRect(tw,  0,  hs, th);
    m_bottomHandle->setRect(0,  th, tw, hs);
    m_cornerHandle->setRect(tw, th, hs, hs);
}

void NineSliceEditorCanvas::setOverlayVisible(bool visible)
{
    m_overlayVisible = visible;
    rebuildOverlay();
}

void NineSliceEditorCanvas::setSliceLinesVisible(bool visible)
{
    if (m_lineLeft)    m_lineLeft->setVisible(visible);
    if (m_lineTop)     m_lineTop->setVisible(visible);
    if (m_lineRight)   m_lineRight->setVisible(visible);
    if (m_lineBottom)  m_lineBottom->setVisible(visible);
    if (m_labelLeft)   m_labelLeft->setVisible(visible);
    if (m_labelRight)  m_labelRight->setVisible(visible);
    if (m_labelTop)    m_labelTop->setVisible(visible);
    if (m_labelBottom) m_labelBottom->setVisible(visible);
}

void NineSliceEditorCanvas::setSettings(const AppSettings& settings)
{
    m_settings = settings;
    setBackgroundBrush(settings.workspaceColor);
    updateBgItem();
    updateGridItem();

    QPen linePen(settings.nineSliceLineColor, 2, settings.nineSliceLineStyle);
    m_lineLeft->setPen(linePen);
    m_lineTop->setPen(linePen);
    m_lineRight->setPen(linePen);
    m_lineBottom->setPen(linePen);

    QBrush labelBrush(settings.nineSliceLabelColor);
    m_labelLeft->setBrush(labelBrush);
    m_labelRight->setBrush(labelBrush);
    m_labelTop->setBrush(labelBrush);
    m_labelBottom->setBrush(labelBrush);

    QBrush handleBrush(settings.nineSliceResizeHandleColor);
    m_rightHandle->setBrush(handleBrush);
    m_bottomHandle->setBrush(handleBrush);
    m_cornerHandle->setBrush(handleBrush);

    rebuildOverlay();
}

void NineSliceEditorCanvas::updateBgItem()
{
    if (m_bgItem) {
        m_scene->removeItem(m_bgItem);
        delete m_bgItem;
        m_bgItem = nullptr;
    }
    if (m_targetSize.isEmpty()) return;

    m_bgItem = new QGraphicsRectItem(QRectF(QPointF(0, 0), m_targetSize));
    if (m_settings.showCheckerboard) {
        m_bgItem->setBrush(QBrush(createCheckerboardPixmap(m_settings.spriteFrameColor)));
    } else {
        m_bgItem->setBrush(m_settings.spriteFrameColor);
    }
    m_bgItem->setPen(Qt::NoPen);
    m_bgItem->setZValue(-1);
    m_scene->addItem(m_bgItem);
}

void NineSliceEditorCanvas::updateGridItem()
{
    if (m_gridItem) {
        m_scene->removeItem(m_gridItem);
        delete m_gridItem;
        m_gridItem = nullptr;
    }
    if (!m_settings.showGrid || m_targetSize.isEmpty()) return;

    auto* grid = new GridOverlayItem();
    grid->setParams(m_settings.gridCellWidth,  m_settings.gridCellHeight,
                    m_settings.gridOffsetX,     m_settings.gridOffsetY,
                    m_settings.gridColor,
                    QSizeF(m_targetSize));
    m_scene->addItem(grid);
    m_gridItem = grid;
}

void NineSliceEditorCanvas::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-sprat-sprite"))
        || event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        ZoomableGraphicsView::dragEnterEvent(event);
}

void NineSliceEditorCanvas::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-sprat-sprite"))
        || event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        ZoomableGraphicsView::dragMoveEvent(event);
}

void NineSliceEditorCanvas::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-sprat-sprite"))) {
        QByteArray data = event->mimeData()->data(QStringLiteral("application/x-sprat-sprite"));
        QString path = QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts).value(0).trimmed();
        if (!path.isEmpty()) {
            emit spriteDropped(path);
            event->acceptProposedAction();
        }
        return;
    }

    // File URL drops — hand off to MainWindow's image loading pipeline via imageFileDropped.
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                emit imageFileDropped(url.toLocalFile());
                event->acceptProposedAction();
                return;
            }
        }
    }

    ZoomableGraphicsView::dropEvent(event);
}

void NineSliceEditorCanvas::rebuildOverlay()
{
    for (auto* r : m_regionItems) {
        m_scene->removeItem(r);
        delete r;
    }
    m_regionItems.clear();

    if (!m_overlayVisible || m_targetSize.isEmpty()) return;

    int l = m_insetLeft;
    int t = m_insetTop;
    int r = m_targetSize.width()  - m_insetRight;
    int b = m_targetSize.height() - m_insetBottom;
    int w = m_targetSize.width();
    int h = m_targetSize.height();

    // Ensure valid order
    if (l > r) std::swap(l, r);
    if (t > b) std::swap(t, b);

    struct Region { QRectF rect; QColor color; };

    auto applyOpacity = [&](QColor c) -> QColor {
        c.setAlpha(qRound(255.0 * m_settings.nineSliceOverlayOpacity / 100.0));
        return c;
    };
    const QColor cornerColor = applyOpacity(m_settings.nineSliceCornerColor);
    const QColor edgeTBColor = applyOpacity(m_settings.nineSliceEdgeTBColor);
    const QColor edgeLRColor = applyOpacity(m_settings.nineSliceEdgeLRColor);
    const QColor centerColor = applyOpacity(m_settings.nineSliceCenterColor);

    Region regions[] = {
        { QRectF(0, 0, l, t),         cornerColor },  // top-left
        { QRectF(r, 0, w - r, t),     cornerColor },  // top-right
        { QRectF(0, b, l, h - b),     cornerColor },  // bottom-left
        { QRectF(r, b, w - r, h - b), cornerColor },  // bottom-right
        { QRectF(l, 0, r - l, t),     edgeTBColor },  // top
        { QRectF(l, b, r - l, h - b), edgeTBColor },  // bottom
        { QRectF(0, t, l, b - t),     edgeLRColor },  // left
        { QRectF(r, t, w - r, b - t), edgeLRColor },  // right
        { QRectF(l, t, r - l, b - t), centerColor },  // center
    };

    for (const auto& reg : regions) {
        if (reg.rect.width() <= 0 || reg.rect.height() <= 0) continue;
        auto* item = m_scene->addRect(reg.rect, Qt::NoPen, QBrush(reg.color));
        item->setZValue(50);
        m_regionItems.append(item);
    }
}
