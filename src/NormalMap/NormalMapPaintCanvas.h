#pragma once

#include "MapPaintCanvas.h"
#include <QColor>
#include <QImage>
#include <QString>
#include <QVector>

class QGraphicsEllipseItem;
class QGraphicsLineItem;
class QGraphicsPathItem;
class QGraphicsPolygonItem;
class QGraphicsRectItem;
class QGraphicsPixmapItem;
class QDir;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFramebufferObject;
class QOpenGLShaderProgram;
class QOpenGLTexture;
class QOpenGLVertexArrayObject;
class QOpenGLBuffer;

/**
 * @brief Canvas for directly painting normal maps on sprites.
 *
 * Extends MapPaintCanvas with:
 *  - GPU-based lighting composite (OpenGL offscreen; falls back to CPU)
 *  - setNormal(nx, ny, nz) encodes a direction as the current paint color
 *  - Eraser paints flat normal (128, 128, 255, 255) instead of transparent
 *  - loadSprite initializes the companion with flat normals and loads _normal.png
 *
 * View:
 *  - m_companionItem (z=1)  always shows the raw normal map
 *  - m_litItem       (z=1.5) always shows the lit composite; opacity = m_viewBlend
 *  - setViewBlend(0) → only normal map; setViewBlend(1) → only lit preview
 */
class NormalMapPaintCanvas : public MapPaintCanvas {
    Q_OBJECT
public:
    enum class FillType    { Slope, TwoSides, Prism, Dome, Cone, Donut, Cylinder, Emboss, HeightField, EdgeEmboss, EdgeTrace, AutoNormal };
    enum class PencilType  { Flat, BevelRise, BevelCarve };
    enum class MeasureState { Idle, WaitingP1, WaitingP2, WaitingP3 };

    struct NormalLayer {
        QImage  image;
        QString name;
        bool    visible = true;
        float   opacity = 1.0f;
    };

    explicit NormalMapPaintCanvas(QWidget* parent = nullptr);
    ~NormalMapPaintCanvas() override;

    // Sprite
    void loadSprite(const QString& path) override;
    void clearSprite() override;

    // Normal direction → paint color
    void setNormal(float nx, float ny, float nz);

    // Pencil type
    void setPencilType(PencilType type);

    // Fill tool
    void setFillMode(bool on);
    void setFillType(FillType type);
    void setFillEndNormal(float nx, float ny, float nz);
    void setFillFitToSelection(bool fit);

    // Fill extra params
    void setFillInvert(bool invert);
    void setPrismSides(float t, float r, float b, float l); // [0,1] fraction per side
    void setPrismAngle(float degrees);   // [0,90]; 90=vertical sides, 0=flat
    void setConeEdge(float x, float y);  // [0,1] fraction; default 0.5,0.5
    void setDonutLength(float length);   // [0.1,1.0]; 1.0=full ring width
    void setFillRoundness(float r);      // [0.01,1.0]; 1.0=full hemisphere/cone/cylinder

    // Emboss params
    void setEmbossHeight(float h);          // [0,1] bevel steepness at peak
    void setEmbossStrength(float s);        // [0,1] blend: 0=symmetric, 1=angle-directed
    void setEmbossAngle(float deg);         // global tilt direction (degrees)
    void setEmbossInnerWidth(float px);     // bevel width inward from boundary (pixels)
    void setEmbossOuterWidth(float px);     // bevel width outward (pixels; future use)
    void setEmbossMidpoint(float mp);       // [0,1] peak position; 0=at boundary, >0=ridge
    void setEmbossEdgeMode(int mode);       // 0=clamp, 1=wrap, 2=mirror
    void setEmbossCorner(int corner);       // 0=round, 1=sharp
    void setEmbossBevelShape(int shape);    // 0=linear,1=smoothstep,2=rounded,3=cosine,4=sine

    // Height field
    void setHeightFieldSource(int source);  // 0=luminance,1=R,2=G,3=B,4=A
    void setHeightFieldScale(float scale);  // steepness multiplier
    void setHeightFieldBlur(float blur);    // pre-blur sigma (0=none)

    // Edge trace
    void setEdgeTraceSource(int source);        // 0=luminance,1=R,2=G,3=B
    void setEdgeTraceThreshold(float t);        // edge detection threshold [0,1]
    void setEdgeTraceBlur(float blur);          // pre-blur sigma before edge detection
    void setEdgeTraceScale(float scale);        // normal tilt steepness
    void setEdgeTraceSpread(float spread);      // post-blur sigma to spread edge normals

    // Edge emboss
    void setEdgeEmbossSource(int source);       // 0=luminance,1=R,2=G,3=B
    void setEdgeEmbossThreshold(float t);       // edge detection threshold [0,1]
    void setEdgeEmbossBlur(float blur);         // pre-blur sigma before edge detection
    void setEdgeEmbossWidth(float px);          // bevel width from detected edge
    void setEdgeEmbossHeight(float h);          // bevel steepness [0,1]
    void setEdgeEmbossMidpoint(float mp);       // peak position [0,0.99]
    void setEdgeEmbossBevelShape(int shape);    // 0=linear,1=smoothstep,2=rounded,3=cosine,4=sine
    void setEdgeEmbossCorner(int corner);       // 0=round, 1=sharp

    // Auto Normal (smart generator: pillow + detail + panel blend)
    void setAutoSource(int source);         // 0=luminance,1=R,2=G,3=B
    void setAutoSmooth(float blur);         // pre-blur sigma for source channel
    void setAutoPillowStrength(float s);    // pillow (alpha-BFS dome) weight
    void setAutoDetailStrength(float s);    // height-field detail weight
    void setAutoPanelStrength(float s);     // panel groove weight
    void setAutoPanelThreshold(float t);    // edge threshold for panel [0,1]
    void setAutoPanelWidth(float px);       // panel bevel width in pixels

    // View
    void setViewBlend(float blend);    // 0 = normal map, 1 = lit preview
    void setViewVisible(bool visible); // hides/shows both normal-map and lit layers

    // Measure-angle mode
    void startMeasure();
    void cancelMeasure();

    // Color-pick mode
    void startPick();
    void cancelPick();

    // Layer management
    int                layerCount()       const { return m_layers.size(); }
    int                activeLayerIndex() const { return m_activeLayerIdx; }
    const NormalLayer& layer(int idx)     const { return m_layers[idx]; }
    QImage             flattenedNormalMap() const;

    void addLayer();
    void removeLayer(int idx);
    void setActiveLayer(int idx);
    void setLayerName(int idx, const QString& name);
    void setLayerVisible(int idx, bool visible);
    void moveLayerUp(int idx);
    void moveLayerDown(int idx);
    void saveLayers(const QDir& dir, const QString& baseName);

    // Lighting
    void setLightColor(const QColor& color);
    void setLightStrength(float strength);
    void setAmbientStrength(float ambient);
    void setLightXY(int x, int y);
    void setLightZ(int z);
    int  lightX() const { return m_lightX; }
    int  lightY() const { return m_lightY; }
    int  lightZ() const { return m_lightZ; }

signals:
    void lightPositionChanged(int x, int y);
    void lightStrengthChanged(float strength);
    void lightZChanged(int z);
    void donutLengthChanged(float length);
    void prismSidesChanged(float t, float r, float b, float l);
    void layersChanged();
    void anglesMeasured(float azimuthDeg, float elevationDeg);
    void measureCancelled();
    void normalPicked(float nx, float ny, float nz);
    void pickCancelled();

protected:
    void paintBrushAt(QPoint center) override;
    void updateCompanionItem() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // Measure-angle mode
    MeasureState           m_measureState   = MeasureState::Idle;
    QPointF                m_measureP1;
    QPointF                m_measureP2;
    bool                   m_measureFlipped = false;
    QGraphicsEllipseItem*  m_measureDot1    = nullptr;
    QGraphicsEllipseItem*  m_measureDot2    = nullptr;
    QGraphicsLineItem*     m_measureLine1   = nullptr;
    QGraphicsLineItem*     m_measureLine2   = nullptr;
    QGraphicsPolygonItem*  m_measurePoly    = nullptr;
    QGraphicsPathItem*     m_measureArrow   = nullptr;
    void removeMeasureItems();
    void computeMeasureAngles(const QPointF& p1, const QPointF& p2, const QPointF& p3,
                              float& azimuthDeg, float& elevationDeg) const;
    void updateMeasurePreview(const QPointF& curP3);

    // Color-pick mode
    bool m_pickMode = false;

    void updateDonutPreviewEllipses();
    void updatePrismPreviewRects();

    // GL offscreen compositing
    void initGL();
    void destroyGL();
    void recomputeLit();
    void recomputeLitCPU();
    void recomputeLitGL();

    // ── State ──────────────────────────────────────────────────────────────

    float  m_viewBlend       = 1.0f;   // 0 = normal map, 1 = lit preview
    float  m_lightStrength   = 1.0f;
    float  m_ambientStrength = 0.2f;
    QColor m_lightColor      = Qt::white;
    int    m_lightX          = 0;
    int    m_lightY          = 0;
    int    m_lightZ          = 100;

    QImage m_litCache;

    // Lit preview layer (above companion/normal-map layer)
    QGraphicsPixmapItem* m_litItem = nullptr;

    // Light indicator
    QGraphicsEllipseItem* m_lightIndicator  = nullptr;
    QGraphicsLineItem*    m_lightRays[8]    = {};
    bool                  m_draggingLight   = false;
    bool                  m_lightHovered    = false;

    qreal lightIndicatorRadius() const;
    void  updateLightIndicator();

    // Pencil type
    PencilType            m_pencilType           = PencilType::Flat;

    // Fill tool
    FillType              m_fillType             = FillType::Slope;
    bool                  m_fillMode             = false;
    bool                  m_fillFitToSel         = false;
    bool                  m_fillInvert           = false;
    float                 m_prismSideT           = 0.5f;
    float                 m_prismSideR           = 0.5f;
    float                 m_prismSideB           = 0.5f;
    float                 m_prismSideL           = 0.5f;
    float                 m_prismAngle           = 90.0f;
    float                 m_coneEdgeX            = 0.5f;
    float                 m_coneEdgeY            = 0.5f;
    float                 m_donutLength          = 1.0f;
    float                 m_fillRoundness        = 1.0f;
    // Emboss
    float                 m_embossHeight         = 0.5f;
    float                 m_embossStrength       = 0.0f;
    float                 m_embossAngle          = 45.0f;
    float                 m_embossInnerWidth     = 10.0f;
    float                 m_embossOuterWidth     = 0.0f;
    float                 m_embossMidpoint       = 0.0f;
    int                   m_embossEdgeMode       = 0;
    int                   m_embossCorner         = 0;
    int                   m_embossBevelShape     = 0;
    // Height field
    int                   m_heightFieldSource    = 0;
    float                 m_heightFieldScale     = 5.0f;
    float                 m_heightFieldBlur      = 0.0f;
    // Edge trace
    int                   m_edgeTraceSource      = 0;
    float                 m_edgeTraceThreshold   = 0.1f;
    float                 m_edgeTraceBlur        = 1.0f;
    float                 m_edgeTraceScale       = 5.0f;
    float                 m_edgeTraceSpread      = 0.0f;
    // Edge emboss
    int                   m_edgeEmbossSource     = 0;
    float                 m_edgeEmbossThreshold  = 0.1f;
    float                 m_edgeEmbossBlur       = 1.0f;
    float                 m_edgeEmbossWidth      = 10.0f;
    float                 m_edgeEmbossHeight     = 0.5f;
    float                 m_edgeEmbossMidpoint   = 0.5f;
    int                   m_edgeEmbossBevelShape = 0;
    int                   m_edgeEmbossCorner     = 0;
    // Auto Normal
    int                   m_autoSource          = 0;
    float                 m_autoSmooth          = 1.0f;
    float                 m_autoPillowStrength  = 1.0f;
    float                 m_autoDetailStrength  = 3.0f;
    float                 m_autoPanelStrength   = 0.5f;
    float                 m_autoPanelThreshold  = 0.2f;
    float                 m_autoPanelWidth      = 5.0f;
    bool                  m_fillActive           = false;
    QPoint                m_fillPressPos;

    // Bevel stroke height buffer (ephemeral, one per stroke)
    std::vector<float>    m_bevelHeightBuf;
    QImage                m_bevelBase;
    bool                  m_bevelStrokeActive    = false;

    void applyBevelHeight(int imgW, int imgH, const QRect& region,
                          float maxTilt, float fhalf, bool carve);
    QColor                m_fillNormalFrom  = QColor(128, 128, 255);
    QColor                m_fillNormalTo    = QColor(128, 128, 255);
    QGraphicsLineItem*    m_fillPreviewLine        = nullptr;
    QGraphicsEllipseItem* m_fillPreviewCircle      = nullptr;
    QGraphicsEllipseItem* m_fillPreviewInnerCircle = nullptr;
    QGraphicsRectItem*    m_fillPreviewRect        = nullptr;
    QGraphicsRectItem*    m_fillPreviewInnerRect   = nullptr;
    QPointF               m_fillCurScenePos;

    void applyFill(QPoint start, QPoint end);

    // Layer helpers
    void   syncActiveLayerToCache();
    void   syncCompanionFromActiveLayer();
    QImage compositeLayers() const;
    void   saveLayersJson(const QString& jsonPath, const QString& dir,
                          const QString& baseName) const;
    void   loadLayersFromJson(const QString& jsonPath, const QDir& dir,
                               const QString& baseName);

    // Layer state
    QVector<NormalLayer> m_layers;
    int                  m_activeLayerIdx = 0;
    QImage               m_compositeCache;

    // GL
    bool                      m_glInitAttempted = false;
    bool                      m_glOk            = false;
    QOffscreenSurface*        m_glSurface       = nullptr;
    QOpenGLContext*           m_glCtx           = nullptr;
    QOpenGLShaderProgram*     m_glProgram       = nullptr;
    QOpenGLVertexArrayObject* m_glVao           = nullptr;
    QOpenGLBuffer*            m_glQuadVbo       = nullptr;
    QOpenGLFramebufferObject* m_glFbo           = nullptr;
    QOpenGLTexture*           m_glSpriteTex     = nullptr;
    QOpenGLTexture*           m_glNormalTex     = nullptr;
    bool                      m_glSpriteDirty   = true;
    bool                      m_glNormalDirty   = true;
};
