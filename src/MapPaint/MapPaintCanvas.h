#pragma once

#include "ZoomableGraphicsView.h"
#include <QColor>
#include <QImage>
#include <QPoint>
#include <QVector>

class QContextMenuEvent;
class QGraphicsEllipseItem;
class QGraphicsItem;
class QGraphicsPathItem;
class QGraphicsPixmapItem;
class QGraphicsScene;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QTimer;

/**
 * @brief Base canvas for painting companion maps (collision, normal) on top of a sprite.
 *
 * Provides:
 *  - Checkerboard background
 *  - Sprite rendering with alpha-mask enforcement (transparent pixels reject strokes)
 *  - Companion QImage editing with pencil and eraser tools
 *  - Zoom/pan inherited from ZoomableGraphicsView
 *  - Selection system: rectangle, ellipse, polygon, freehand, magic-wand
 */
class MapPaintCanvas : public ZoomableGraphicsView {
    Q_OBJECT
public:
    enum class Tool        { Pencil, Eraser, Fill, Blur, Smudge, Sculpt, Curve, Blend };
    enum class BrushShape  { Circle, Square };

    enum class CanvasMode    { Paint, Select };
    enum class SelectionTool { Rectangle, Ellipse, Polygon, Freehand, MagicWand };
    enum class SelectionOp   { Replace, Add, Subtract };

    struct ShapeRecord {
        QImage      mask;  // Format_Grayscale8
        SelectionOp op;    // Add or Subtract
    };

    explicit MapPaintCanvas(QWidget* parent = nullptr);

    // Sprite management
    virtual void loadSprite(const QString& path);
    virtual void clearSprite();

    // Companion image
    void loadCompanion(const QImage& img);
    QImage companionImage() const { return m_companion; }

    // Tool settings
    void setBrushSize(int px);
    void setBrushShape(BrushShape shape);
    void setBrushHardness(float h);   // 0 = fully soft, 1 = fully sharp (default)
    void setBlurStrength(float s);    // 0 = no blur effect, 1 = full blur (default)
    void setToolMode(Tool tool);
    void setPaintColor(const QColor& color);

    BrushShape brushShape()    const { return m_brushShape;    }
    float      brushHardness() const { return m_brushHardness; }
    void clearCompanion();

    // Canvas / selection mode
    void setCanvasMode(CanvasMode mode);
    void setSelectionTool(SelectionTool tool);
    void setMagicWandTolerance(int t) { m_magicWandTolerance = t; }
    void setSelectionFeather(int px) { m_selectionFeather = qMax(0, px); }
    int  selectionFeather() const    { return m_selectionFeather; }
    void setSelectionOp(SelectionOp op) { m_selectionOp = op; }
    void clearSelection();
    void invertSelection();
    void growSelection(int px);
    void shrinkSelection(int px);
    virtual void undo();
    virtual void redo();
    bool hasSelection() const { return m_hasSelection; }
    QRect selectionBoundingRect() const;
    const QImage& selectionMask() const { return m_selectionMask; }

    bool isModified() const { return m_modified; }
    void setModified(bool modified) { m_modified = modified; }

    bool isReadOnly() const { return m_readOnly; }
    void setReadOnly(bool ro);   // updates cursor + triggers repaint

    void setCompanionOpacity(qreal opacity);
    void setCompanionVisible(bool visible);
    void setRectSelection(const QRect& rect);
    virtual void performFill();

    void setSelectionOverlayColor(const QColor& color);
    void removeSelectedShape();
    int  selectedShapeIndex() const { return m_selectedShape; }
    QVector<ShapeRecord> shapes() const { return m_shapes; }
    void setShapes(const QVector<ShapeRecord>& shapes);

signals:
    void companionModified();
    void selectionChanged();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;

    virtual void drawStroke(QPoint from, QPoint to);
    virtual void paintBrushAt(QPoint center);
    virtual void updateCompanionItem();

    QImage m_sprite;     // loaded sprite for alpha mask checks
    QImage m_companion;  // the companion map being painted

    QGraphicsScene*      m_scene         = nullptr;
    QGraphicsPixmapItem* m_spriteItem    = nullptr;
    QGraphicsPixmapItem* m_companionItem = nullptr;

    Tool       m_tool             = Tool::Pencil;
    BrushShape m_brushShape       = BrushShape::Circle;
    QColor     m_paintColor       = Qt::red;
    int        m_brushSize        = 4;
    float      m_brushHardness    = 1.0f;  // 0 = fully soft, 1 = fully sharp
    float      m_blurStrength     = 1.0f;
    qreal      m_companionOpacity = 1.0;

    bool   m_painting     = false;
    QPoint m_lastPaintPos;
    bool   m_modified     = false;
    bool   m_readOnly     = false;

    // --- Curve tool state ---
    enum class CurveState { Idle, AwaitingP2, Curving };
    CurveState            m_curveState   = CurveState::Idle;
    QPointF               m_curveP1;
    QPointF               m_curveP2;
    QGraphicsEllipseItem* m_curveDot1    = nullptr;
    QGraphicsEllipseItem* m_curveDot2    = nullptr;
    QGraphicsPathItem*    m_curvePreview = nullptr;

    void clearCurveItems();
    void updateCurvePreview(const QPointF& cursor);
    void paintCurveStroke(const QPointF& p1, const QPointF& p2, const QPointF& ctrl);
    void paintBlendAt(QPoint center);

    // --- Selection state ---
    CanvasMode    m_canvasMode         = CanvasMode::Paint;
    SelectionTool m_selectionTool      = SelectionTool::Rectangle;
    int           m_magicWandTolerance = 30;
    int           m_selectionFeather   = 0;

    QImage          m_selectionMask;       // Format_Grayscale8: 255=selected, 0=not
    bool            m_hasSelection  = false;
    SelectionOp     m_selectionOp   = SelectionOp::Replace;

    bool            m_selecting     = false;
    QPoint          m_selectionStart;
    QVector<QPoint> m_selectionPoints;     // polygon / freehand vertices

    QGraphicsPixmapItem* m_selectionOverlayItem     = nullptr;  // z=2
    QGraphicsItem*       m_selectionInProgressItem  = nullptr;  // z=3
    QGraphicsPixmapItem* m_selectedShapeOverlayItem = nullptr;  // z=4

    QColor m_selectionOverlayColor { 50, 130, 255 };  // tinted by mode

    QVector<ShapeRecord> m_shapes;
    int                  m_selectedShape = -1;
    bool                 m_hasDragged    = false;

    virtual void pushUndoSnapshot();
    void updateSelectionOverlay();

protected:
    void updateCursor();

private:
    struct UndoState {
        QImage               companion;
        QImage               selectionMask;
        bool                 hasSelection  = false;
        QVector<ShapeRecord> shapes;
        int                  selectedShape = -1;
    };
    static constexpr int kMaxUndoSteps = 20;
    QVector<UndoState> m_undoHistory;
    QVector<UndoState> m_redoHistory;
    void applyMaskWithModifier(QImage newMask, Qt::KeyboardModifiers mods);
    static void featherMask(QImage& mask, int radius);

    void commitRectangleSelection(QRect rect, Qt::KeyboardModifiers mods);
    void commitEllipseSelection(QRect rect, Qt::KeyboardModifiers mods);
    void commitPolygonSelection(Qt::KeyboardModifiers mods);
    void commitFreehandSelection(Qt::KeyboardModifiers mods);
    void magicWandFill(QPoint seed, Qt::KeyboardModifiers mods);

    void createInProgressItem();
    void clearInProgressItem();
    void updateInProgressItem(QPoint cursorPos);

    bool trySelectShapeAt(QPoint scenePos);   // returns true if a shape was hit
    void recomputeSelectionFromShapes();
    int  hitTestShapes(QPoint scenePos) const;
    void updateSelectedShapeOverlay();

    void updateSelectionOutline();

    QTimer*        m_selectionTimer    = nullptr;
    int            m_selectionPhase    = 0;
    QGraphicsItem* m_marchingAntsItem  = nullptr;
};
