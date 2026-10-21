#pragma once

#include "IWorkspace.h"
#include "NormalMapPaintCanvas.h"
#include "ProjectModels.h"

#include <QColor>
#include <QSet>
#include <QWidget>

class NavigatorPanel;
class NormalWheelWidget;
class ProjectSession;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;
class QSlider;
class QSpinBox;
class QTreeWidgetItem;

/**
 * @brief Workspace for painting normal maps directly on sprites.
 *
 * Mirrors CollisionMapWorkspace: three-panel (Navigator | Canvas | Right Panel).
 * The right panel has tools, a NormalWheelWidget for direction picking, a light
 * setup section, and a layer visibility toggle.
 */
class NormalMapWorkspace : public QWidget, public IWorkspace {
    Q_OBJECT
public:
    explicit NormalMapWorkspace(ProjectSession* session, QWidget* parent = nullptr);

    // IWorkspace
    void     enter()  override;
    void     leave()  override;
    QWidget* widget() override { return this; }

    NavigatorPanel* navigatorPanel() const { return m_navigatorPanel; }

signals:
    void definitionsChanged();
    void atlasIndexChanged(int sessionAtlasIndex);

public:
    /** Populate / refresh the atlas combo and sync the toggle button state. */
    void updateAtlasCombo(const QVector<AtlasEntry>& atlases, int activeSessionIndex);

private:
    void updateAtlasToggle();
    void setupUi();
    QWidget* buildPreviewBar();
    QWidget* buildRightPanel();

    void loadSprite(const QString& path);
    void saveCurrentSprite();
    void setExclusiveTool(QPushButton* active);
    void updateToolOptions();

    // Slots
    void onNavigatorSelectionChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onPickLightColor();
    void onCanvasZoomChanged(double zoom);
    void onSelectionChanged();
    void onLayersChanged();

    ProjectSession*       m_session          = nullptr;
    QString               m_currentPath;
    int                   m_activeAtlasIndex = -1;
    QSet<QString>         m_spritesWithNormalMap;

    // ── Panels ────────────────────────────────────────────────────────────
    NavigatorPanel*       m_navigatorPanel    = nullptr;
    QPushButton*          m_atlasNormalMapBtn = nullptr;
    NormalMapPaintCanvas* m_canvas            = nullptr;
    QWidget*              m_lightBar          = nullptr;  // Row 2 of preview bar
    QScrollArea*          m_rightPanel        = nullptr;  // Right tools panel

    // ── Preview ───────────────────────────────────────────────────────────
    QDoubleSpinBox*  m_zoomSpin   = nullptr;
    QSlider*         m_viewSlider = nullptr;

    // ── Tool buttons ──────────────────────────────────────────────────────
    QPushButton* m_pencilBtn         = nullptr;
    QPushButton* m_eraserBtn         = nullptr;
    QPushButton* m_blurBtn           = nullptr;
    QPushButton* m_smudgeBtn         = nullptr;
    QPushButton* m_fillBtn           = nullptr;
    QPushButton* m_curveBtn          = nullptr;
    QPushButton* m_blendBtn          = nullptr;
    QPushButton* m_selectRectBtn     = nullptr;
    QPushButton* m_selectEllipseBtn  = nullptr;
    QPushButton* m_selectPolyBtn     = nullptr;
    QPushButton* m_selectFreehandBtn = nullptr;
    QPushButton* m_selectWandBtn     = nullptr;

    // ── Tool options ──────────────────────────────────────────────────────
    QWidget*           m_pencilTypeRow      = nullptr;
    QComboBox*         m_pencilTypeCombo    = nullptr;
    NormalWheelWidget* m_wheelWidget        = nullptr;
    QPushButton*       m_pickBtn            = nullptr;
    QPushButton*       m_measureBtn         = nullptr;
    NormalWheelWidget* m_fillEndWheelWidget = nullptr;
    QWidget*           m_fillToRow          = nullptr;
    QWidget*           m_fillTypeRow        = nullptr;
    QComboBox*         m_fillTypeCombo      = nullptr;
    QCheckBox*         m_fillFitSelChk      = nullptr;
    QCheckBox*         m_fillInvertChk      = nullptr;
    QWidget*           m_prismSizeRow        = nullptr;
    QDoubleSpinBox*    m_prismSideTopSpin    = nullptr;
    QDoubleSpinBox*    m_prismSideRightSpin  = nullptr;
    QDoubleSpinBox*    m_prismSideBottomSpin = nullptr;
    QDoubleSpinBox*    m_prismSideLeftSpin   = nullptr;
    QSpinBox*          m_prismAngleSpin      = nullptr;
    QWidget*           m_coneEdgeRow        = nullptr;
    QSpinBox*          m_coneEdgeXSpin      = nullptr;
    QSpinBox*          m_coneEdgeYSpin      = nullptr;
    QWidget*           m_donutLengthRow     = nullptr;
    QDoubleSpinBox*    m_donutLengthSpin    = nullptr;
    QWidget*           m_roundnessRow       = nullptr;
    QDoubleSpinBox*    m_roundnessSpin      = nullptr;
    QWidget*           m_embossRow              = nullptr;
    QDoubleSpinBox*    m_embossHeightSpin        = nullptr;
    QDoubleSpinBox*    m_embossStrengthSpin      = nullptr;
    QSpinBox*          m_embossAngleSpin         = nullptr;
    QDoubleSpinBox*    m_embossInnerWidthSpin    = nullptr;
    QDoubleSpinBox*    m_embossMidpointSpin      = nullptr;
    QComboBox*         m_embossBevelShapeCombo   = nullptr;
    QComboBox*         m_embossCornerCombo       = nullptr;
    QComboBox*         m_embossEdgeModeCombo     = nullptr;
    QWidget*           m_heightFieldRow          = nullptr;
    QComboBox*         m_heightFieldSourceCombo  = nullptr;
    QDoubleSpinBox*    m_heightFieldScaleSpin    = nullptr;
    QDoubleSpinBox*    m_heightFieldBlurSpin     = nullptr;
    QWidget*           m_edgeEmbossRow           = nullptr;
    QComboBox*         m_edgeEmbossSourceCombo   = nullptr;
    QDoubleSpinBox*    m_edgeEmbossThresholdSpin = nullptr;
    QDoubleSpinBox*    m_edgeEmbossBlurSpin      = nullptr;
    QDoubleSpinBox*    m_edgeEmbossWidthSpin     = nullptr;
    QDoubleSpinBox*    m_edgeEmbossHeightSpin    = nullptr;
    QDoubleSpinBox*    m_edgeEmbossMidpointSpin  = nullptr;
    QComboBox*         m_edgeEmbossBevelShapeCombo = nullptr;
    QComboBox*         m_edgeEmbossCornerCombo   = nullptr;
    QWidget*           m_edgeTraceRow            = nullptr;
    QComboBox*         m_edgeTraceSourceCombo    = nullptr;
    QDoubleSpinBox*    m_edgeTraceThresholdSpin  = nullptr;
    QDoubleSpinBox*    m_edgeTraceBlurSpin       = nullptr;
    QDoubleSpinBox*    m_edgeTraceScaleSpin      = nullptr;
    QDoubleSpinBox*    m_edgeTraceSpreadSpin     = nullptr;
    QWidget*           m_autoNormalRow           = nullptr;
    QComboBox*         m_autoSourceCombo         = nullptr;
    QDoubleSpinBox*    m_autoSmoothSpin          = nullptr;
    QDoubleSpinBox*    m_autoPillowStrengthSpin  = nullptr;
    QDoubleSpinBox*    m_autoDetailStrengthSpin  = nullptr;
    QDoubleSpinBox*    m_autoPanelStrengthSpin   = nullptr;
    QDoubleSpinBox*    m_autoPanelThresholdSpin  = nullptr;
    QDoubleSpinBox*    m_autoPanelWidthSpin      = nullptr;

    QWidget* m_paintOptionsWidget = nullptr;
    QWidget* m_sizeCtrlRow        = nullptr;
    QWidget* m_shapeCtrlRow       = nullptr;
    QWidget* m_hardnessCtrlRow    = nullptr;
    QWidget* m_blurStrengthRow    = nullptr;
    QWidget* m_toleranceRow       = nullptr;
    QWidget* m_featherRow         = nullptr;
    QWidget* m_clearSelRow        = nullptr;

    QSpinBox*    m_brushSpin          = nullptr;
    QPushButton* m_brushCircleBtn     = nullptr;
    QPushButton* m_brushSquareBtn     = nullptr;
    QSlider*     m_hardnessSlider     = nullptr;
    QSlider*     m_blurStrengthSlider = nullptr;
    QSpinBox*    m_wandToleranceSpin  = nullptr;
    QSpinBox*    m_featherSpin        = nullptr;
    QPushButton* m_clearSelBtn        = nullptr;
    QSpinBox*    m_selXSpin           = nullptr;
    QSpinBox*    m_selYSpin           = nullptr;
    QSpinBox*    m_selWSpin           = nullptr;
    QSpinBox*    m_selHSpin           = nullptr;

    // ── Light ─────────────────────────────────────────────────────────────
    QSpinBox*    m_lightXSpin     = nullptr;
    QSpinBox*    m_lightYSpin     = nullptr;
    QSpinBox*    m_lightZSpin     = nullptr;
    QPushButton* m_lightColorBtn  = nullptr;
    QSlider*     m_lightStrSlider = nullptr;
    QLabel*      m_lightStrLabel  = nullptr;
    QSlider*     m_ambientSlider  = nullptr;
    QLabel*      m_ambientLabel   = nullptr;
    QColor       m_lightColor     = Qt::white;

    // ── Layers ────────────────────────────────────────────────────────────
    QListWidget* m_layerList        = nullptr;
    QPushButton* m_addLayerBtn      = nullptr;
    QPushButton* m_removeLayerBtn   = nullptr;
    QPushButton* m_moveLayerUpBtn   = nullptr;
    QPushButton* m_moveLayerDownBtn = nullptr;

};
