#pragma once
#include "AppSettings.h"
#include "ZoomableGraphicsView.h"
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QPixmap>
#include <QString>

class QSvgRenderer;

class QGraphicsPixmapItem;
class QGraphicsSimpleTextItem;

/**
 * @brief A draggable line item for the nine-slice editor.
 *
 * Emits positionChanged() when the user drags the line.
 * Orientation determines whether the line moves horizontally or vertically.
 */
class SliceLineItem : public QObject, public QGraphicsLineItem {
    Q_OBJECT
public:
    enum Edge { Left, Top, Right, Bottom };

    SliceLineItem(Edge edge, QGraphicsItem* parent = nullptr);

    void setConstraint(qreal minVal, qreal maxVal);
    void setPosition(int pos);
    int position() const { return m_pos; }
    Edge edge() const { return m_edge; }

signals:
    void positionChanged(SliceLineItem::Edge edge, int pos);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    Edge m_edge;
    int m_pos = 0;
    qreal m_minVal = 0;
    qreal m_maxVal = 1000;
    bool m_dragging = false;
};

/**
 * @brief A draggable handle on the edge or corner of the canvas image.
 *
 * Emits dragged(dx, dy) as the user moves it. Constrained to one or both axes
 * depending on which edge it sits on.
 */
class ResizeHandleItem : public QObject, public QGraphicsRectItem {
    Q_OBJECT
public:
    enum Axis { H, V, Both };

    ResizeHandleItem(Axis axis, QGraphicsItem* parent = nullptr);

    // Pass a renderer whose SVG will be drawn centred inside the handle.
    // For the V (bottom) handle pass rotate=true to turn vertical dots sideways.
    void setIconRenderer(QSvgRenderer* renderer, bool rotate = false);

signals:
    void dragged(int dx, int dy);

protected:
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    Axis           m_axis;
    bool           m_dragging      = false;
    qreal          m_clickOffsetX  = 0;
    qreal          m_clickOffsetY  = 0;
    QSvgRenderer*  m_iconRenderer  = nullptr;
    bool           m_rotateIcon    = false;
};

/**
 * @brief QGraphicsView-based canvas for nine-slice editing and preview.
 *
 * Displays the 9-slice composite rendered at a configurable target size, with
 * four draggable lines to adjust insets. Resize handles on the right/bottom
 * edges let the user expand or shrink the target size to preview how the
 * nine-slice looks at different render sizes.
 */
class NineSliceEditorCanvas : public ZoomableGraphicsView {
    Q_OBJECT
public:
    explicit NineSliceEditorCanvas(QWidget* parent = nullptr);

    void setSpriteImage(const QString& path);
    void clearSprite();

    void setInsets(int left, int top, int right, int bottom);
    void setFillModes(const QString& hMode, const QString& vMode);
    void setSettings(const AppSettings& settings);
    void setOverlayVisible(bool visible);
    void setSliceLinesVisible(bool visible);
    void setTargetSize(int width, int height);
    int leftInset()   const { return m_insetLeft;   }
    int topInset()    const { return m_insetTop;    }
    int rightInset()  const { return m_insetRight;  }
    int bottomInset() const { return m_insetBottom; }

    void initialFit() override;

signals:
    void insetsChanged(int left, int top, int right, int bottom);
    void targetSizeChanged(int width, int height);
    void spriteDropped(const QString& path);
    void imageFileDropped(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onLinePositionChanged(SliceLineItem::Edge edge, int pos);

private:
    void renderComposite();
    void resizeTo(int w, int h);
    void updateLineGeometry();
    void updateHandlePositions();
    void rebuildOverlay();
    void updateBgItem();
    void updateGridItem();

    QGraphicsScene*      m_scene       = nullptr;
    QGraphicsPixmapItem* m_imageItem   = nullptr;
    SliceLineItem*       m_lineLeft    = nullptr;
    SliceLineItem*       m_lineTop     = nullptr;
    SliceLineItem*       m_lineRight   = nullptr;
    SliceLineItem*       m_lineBottom  = nullptr;
    ResizeHandleItem*    m_rightHandle  = nullptr;
    ResizeHandleItem*    m_bottomHandle = nullptr;
    ResizeHandleItem*    m_cornerHandle = nullptr;

    // SVG renderers for handle icons (owned by the canvas)
    QSvgRenderer* m_dragHandleRenderer   = nullptr;
    QSvgRenderer* m_resizeHandleRenderer = nullptr;

    QList<QGraphicsRectItem*> m_regionItems;

    // Labels shown above vertical lines (L, R) and left of horizontal lines (T, B)
    QGraphicsSimpleTextItem* m_labelLeft   = nullptr;
    QGraphicsSimpleTextItem* m_labelRight  = nullptr;
    QGraphicsSimpleTextItem* m_labelTop    = nullptr;
    QGraphicsSimpleTextItem* m_labelBottom = nullptr;

    QGraphicsRectItem* m_bgItem   = nullptr;
    QGraphicsItem*     m_gridItem = nullptr;

    AppSettings m_settings;
    bool        m_overlayVisible = true;
    QPixmap m_sourcePixmap;
    QSize   m_targetSize;
    QString m_currentPath;
    QString m_hMode = QStringLiteral("stretch");
    QString m_vMode = QStringLiteral("stretch");
    int m_insetLeft   = 0;
    int m_insetTop    = 0;
    int m_insetRight  = 0;
    int m_insetBottom = 0;
};
