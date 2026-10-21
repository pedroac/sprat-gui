#include "NormalMapWorkspace.h"
#include "NavigatorPanel.h"
#include "NavigatorTreeWidget.h"
#include "NormalWheelWidget.h"
#include "ProjectSession.h"
#include "SpriteModels.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

// ─────────────────────────────────────────────────────────────────────────────
// Local helpers
// ─────────────────────────────────────────────────────────────────────────────

static QLabel* makeSectionHeader(const QString& text, QWidget* parent)
{
    auto* lbl = new QLabel(text, parent);
    QFont f = lbl->font();
    f.setBold(true);
    lbl->setFont(f);
    lbl->setContentsMargins(0, 4, 0, 2);
    return lbl;
}

static QFrame* makeSeparator(QWidget* parent)
{
    auto* sep = new QFrame(parent);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    return sep;
}

static QFrame* makeVSeparator(QWidget* parent)
{
    auto* sep = new QFrame(parent);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    sep->setFixedWidth(2);
    return sep;
}

static QPushButton* makeIconBtn(const QString& iconPath, const QString& tip,
                                  bool checkable, QWidget* parent)
{
    auto* btn = new QPushButton(QIcon(iconPath), QString(), parent);
    btn->setCheckable(checkable);
    btn->setToolTip(tip);
    btn->setFixedSize(28, 28);
    btn->setIconSize(QSize(16, 16));
    return btn;
}

// ─────────────────────────────────────────────────────────────────────────────
// NormalMapWorkspace
// ─────────────────────────────────────────────────────────────────────────────

NormalMapWorkspace::NormalMapWorkspace(ProjectSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
{
    setupUi();
}

void NormalMapWorkspace::setupUi()
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    root->addWidget(splitter);

    // === Left: NavigatorPanel ===
    auto* leftWidget = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    m_navigatorPanel = new NavigatorPanel(leftWidget);
    m_navigatorPanel->configure({
        /* atlasCombo      */ true,
        /* showHidden      */ false,
        /* checkboxes      */ false,
        /* filterBar       */ true,
        /* addSourceButton */ false,
        /* selectionMode   */ QAbstractItemView::SingleSelection
    });
    m_navigatorPanel->setSpriteBadgeCallback([this](const SpritePtr& sp) -> QIcon {
        if (sp && !sp->path.isEmpty() && m_spritesWithNormalMap.contains(sp->path))
            return QIcon(QStringLiteral(":/icons/normal-map.svg"));
        return {};
    });
    leftLayout->addWidget(m_navigatorPanel, 1);
    splitter->addWidget(leftWidget);

    // === Center: Preview bar + Canvas ===
    auto* centerWidget = new QWidget(this);
    auto* centerLayout = new QVBoxLayout(centerWidget);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);
    centerLayout->addWidget(buildPreviewBar());
    m_canvas = new NormalMapPaintCanvas(this);
    m_canvas->setMinimumSize(200, 200);
    m_canvas->setSelectionOverlayColor(QColor(128, 128, 255));
    centerLayout->addWidget(m_canvas, 1);
    splitter->addWidget(centerWidget);

    // === Right: Properties panel ===
    splitter->addWidget(buildRightPanel());

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({220, 600, 200});

    // === Connections ===
    connect(m_navigatorPanel->tree(), &QTreeWidget::currentItemChanged,
            this, &NormalMapWorkspace::onNavigatorSelectionChanged);

    // Atlas combo: filter tree and update toggle state
    connect(m_navigatorPanel, &NavigatorPanel::atlasIndexChanged,
            this, [this](int atlasIdx) {
                m_activeAtlasIndex = atlasIdx;
                if (m_session)
                    m_navigatorPanel->refresh(m_session, false, m_activeAtlasIndex);
                updateAtlasToggle();
                emit atlasIndexChanged(atlasIdx);
            });

    // Atlas normal-map toggle
    connect(m_atlasNormalMapBtn, &QPushButton::toggled, this, [this](bool on) {
        if (!m_session || m_activeAtlasIndex < 0 || m_activeAtlasIndex >= m_session->atlases.size())
            return;
        m_session->atlases[m_activeAtlasIndex].hasNormalMap = on;
        m_navigatorPanel->updateAtlasCombo(m_session->atlases, m_activeAtlasIndex);
        updateAtlasToggle();
        emit definitionsChanged();
    });

    // View slider: 0 = raw normal map, 100 = lit preview
    connect(m_viewSlider, &QSlider::valueChanged, this, [this](int v) {
        m_canvas->setViewBlend(v / 100.0f);
    });

    // Zoom
    connect(m_canvas, &NormalMapPaintCanvas::zoomChanged,
            this, &NormalMapWorkspace::onCanvasZoomChanged);
    connect(m_zoomSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                m_canvas->setZoomManual(true);
                m_canvas->setZoom(v / 100.0);
            });

    // Layer panel: sync list when canvas layers change
    connect(m_canvas, &NormalMapPaintCanvas::layersChanged,
            this, &NormalMapWorkspace::onLayersChanged);

    // Normal wheel → canvas paint color
    connect(m_wheelWidget, &NormalWheelWidget::normalChanged,
            this, [this](float nx, float ny, float nz) {
                m_canvas->setNormal(nx, ny, nz);
            });

    // Measure button ↔ canvas measure mode
    connect(m_measureBtn, &QPushButton::toggled, this, [this](bool on) {
        if (on) m_canvas->startMeasure();
        else    m_canvas->cancelMeasure();
    });
    connect(m_canvas, &NormalMapPaintCanvas::anglesMeasured,
            this, [this](float azimuthDeg, float elevationDeg) {
        constexpr float kPi = 3.14159265f;
        const float elevRad = elevationDeg * kPi / 180.0f;
        const float aziRad  = azimuthDeg   * kPi / 180.0f;
        const float nz = std::cos(elevRad);
        const float r  = std::sin(elevRad);
        m_wheelWidget->setNormal(r * std::cos(aziRad), r * std::sin(aziRad), nz);
    });
    connect(m_canvas, &NormalMapPaintCanvas::measureCancelled, this, [this]() {
        QSignalBlocker b(m_measureBtn);
        m_measureBtn->setChecked(false);
    });

    // Pick mode
    connect(m_pickBtn, &QPushButton::toggled, this, [this](bool on) {
        if (on) m_canvas->startPick();
        else    m_canvas->cancelPick();
    });
    connect(m_canvas, &NormalMapPaintCanvas::normalPicked,
            this, [this](float nx, float ny, float nz) {
        m_wheelWidget->setNormal(nx, ny, nz);
    });
    connect(m_canvas, &NormalMapPaintCanvas::pickCancelled, this, [this]() {
        QSignalBlocker b(m_pickBtn);
        m_pickBtn->setChecked(false);
    });

    // Brush size
    connect(m_brushSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v) { m_canvas->setBrushSize(v); });

    // Hardness
    connect(m_hardnessSlider, &QSlider::valueChanged, this, [this](int v) {
        m_canvas->setBrushHardness(v / 100.0f);
    });

    // Blur strength
    connect(m_blurStrengthSlider, &QSlider::valueChanged, this, [this](int v) {
        m_canvas->setBlurStrength(v / 100.0f);
    });

    // Magic wand tolerance
    connect(m_wandToleranceSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v) { m_canvas->setMagicWandTolerance(v); });

    // Feather
    connect(m_featherSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v) { m_canvas->setSelectionFeather(v); });

    // Clear selection
    connect(m_clearSelBtn, &QPushButton::clicked,
            this, [this] { m_canvas->clearSelection(); });

    // Selection bounding rect
    auto applySelRect = [this] {
        if (!m_canvas->hasSelection()) return;
        const QRect r(m_selXSpin->value(), m_selYSpin->value(),
                      m_selWSpin->value(), m_selHSpin->value());
        if (r.isValid()) m_canvas->setRectSelection(r);
    };
    connect(m_selXSpin, qOverload<int>(&QSpinBox::valueChanged), this, applySelRect);
    connect(m_selYSpin, qOverload<int>(&QSpinBox::valueChanged), this, applySelRect);
    connect(m_selWSpin, qOverload<int>(&QSpinBox::valueChanged), this, applySelRect);
    connect(m_selHSpin, qOverload<int>(&QSpinBox::valueChanged), this, applySelRect);

    // Brush shape (exclusive pair)
    connect(m_brushCircleBtn, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            QSignalBlocker sb(m_brushSquareBtn);
            m_brushSquareBtn->setChecked(false);
            m_canvas->setBrushShape(MapPaintCanvas::BrushShape::Circle);
        } else if (!m_brushSquareBtn->isChecked()) {
            QSignalBlocker sb(m_brushCircleBtn);
            m_brushCircleBtn->setChecked(true);
        }
    });
    connect(m_brushSquareBtn, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            QSignalBlocker sb(m_brushCircleBtn);
            m_brushCircleBtn->setChecked(false);
            m_canvas->setBrushShape(MapPaintCanvas::BrushShape::Square);
        } else if (!m_brushCircleBtn->isChecked()) {
            QSignalBlocker sb(m_brushSquareBtn);
            m_brushSquareBtn->setChecked(true);
        }
    });

    // Fill "To" wheel
    connect(m_fillEndWheelWidget, &NormalWheelWidget::normalChanged,
            this, [this](float nx, float ny, float nz) {
                m_canvas->setFillEndNormal(nx, ny, nz);
            });

    // Pencil type combo
    connect(m_pencilTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                const auto type = static_cast<NormalMapPaintCanvas::PencilType>(
                    m_pencilTypeCombo->itemData(idx).toInt());
                m_canvas->setPencilType(type);
                updateToolOptions();
            });

    // Fill type combo
    connect(m_fillTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                const auto type = static_cast<NormalMapPaintCanvas::FillType>(
                    m_fillTypeCombo->itemData(idx).toInt());
                m_canvas->setFillType(type);
                updateToolOptions();
            });

    // Fit-to-selection checkbox
    connect(m_fillFitSelChk, &QCheckBox::toggled,
            this, [this](bool on) { m_canvas->setFillFitToSelection(on); });

    // Invert
    connect(m_fillInvertChk, &QCheckBox::toggled,
            this, [this](bool on) { m_canvas->setFillInvert(on); });

    // Prism side widths (T/R/B/L)
    auto updatePrismSides = [this] {
        m_canvas->setPrismSides(
            float(m_prismSideTopSpin->value()),
            float(m_prismSideRightSpin->value()),
            float(m_prismSideBottomSpin->value()),
            float(m_prismSideLeftSpin->value()));
    };
    connect(m_prismSideTopSpin,    qOverload<double>(&QDoubleSpinBox::valueChanged), this, updatePrismSides);
    connect(m_prismSideRightSpin,  qOverload<double>(&QDoubleSpinBox::valueChanged), this, updatePrismSides);
    connect(m_prismSideBottomSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, updatePrismSides);
    connect(m_prismSideLeftSpin,   qOverload<double>(&QDoubleSpinBox::valueChanged), this, updatePrismSides);

    // Prism side angle
    connect(m_prismAngleSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v) { m_canvas->setPrismAngle(float(v)); });

    // Cone edge X/Y
    auto updateConeEdge = [this] {
        m_canvas->setConeEdge(m_coneEdgeXSpin->value() / 100.0f,
                              m_coneEdgeYSpin->value() / 100.0f);
    };
    connect(m_coneEdgeXSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, updateConeEdge);
    connect(m_coneEdgeYSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, updateConeEdge);

    // Donut thickness
    connect(m_donutLengthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setDonutLength(float(v)); });
    connect(m_canvas, &NormalMapPaintCanvas::donutLengthChanged, this, [this](float v) {
        QSignalBlocker b(m_donutLengthSpin);
        m_donutLengthSpin->setValue(double(v));
    });
    connect(m_roundnessSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setFillRoundness(float(v)); });
    connect(m_canvas, &NormalMapPaintCanvas::prismSidesChanged,
            this, [this](float t, float r, float b, float l) {
        QSignalBlocker bt(m_prismSideTopSpin),    br(m_prismSideRightSpin),
                       bb(m_prismSideBottomSpin), bl(m_prismSideLeftSpin);
        m_prismSideTopSpin->setValue(double(t));
        m_prismSideRightSpin->setValue(double(r));
        m_prismSideBottomSpin->setValue(double(b));
        m_prismSideLeftSpin->setValue(double(l));
    });

    // Emboss options
    connect(m_embossHeightSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setEmbossHeight(float(v)); });
    connect(m_embossStrengthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setEmbossStrength(float(v)); });
    connect(m_embossAngleSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int v) { m_canvas->setEmbossAngle(float(v)); });
    connect(m_embossInnerWidthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setEmbossInnerWidth(float(v)); });
    connect(m_embossMidpointSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setEmbossMidpoint(float(v)); });
    connect(m_embossBevelShapeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                m_canvas->setEmbossBevelShape(m_embossBevelShapeCombo->itemData(idx).toInt());
            });
    connect(m_embossCornerCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                m_canvas->setEmbossCorner(m_embossCornerCombo->itemData(idx).toInt());
            });
    connect(m_embossEdgeModeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                m_canvas->setEmbossEdgeMode(m_embossEdgeModeCombo->itemData(idx).toInt());
            });

    // Height field options
    connect(m_heightFieldSourceCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                m_canvas->setHeightFieldSource(m_heightFieldSourceCombo->itemData(idx).toInt());
            });
    connect(m_heightFieldScaleSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setHeightFieldScale(float(v)); });
    connect(m_heightFieldBlurSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setHeightFieldBlur(float(v)); });

    // Edge emboss
    connect(m_edgeEmbossSourceCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) { m_canvas->setEdgeEmbossSource(m_edgeEmbossSourceCombo->itemData(idx).toInt()); });
    connect(m_edgeEmbossThresholdSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setEdgeEmbossThreshold(float(v)); });
    connect(m_edgeEmbossBlurSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setEdgeEmbossBlur(float(v)); });
    connect(m_edgeEmbossWidthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setEdgeEmbossWidth(float(v)); });
    connect(m_edgeEmbossHeightSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setEdgeEmbossHeight(float(v)); });
    connect(m_edgeEmbossMidpointSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setEdgeEmbossMidpoint(float(v)); });
    connect(m_edgeEmbossBevelShapeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) { m_canvas->setEdgeEmbossBevelShape(m_edgeEmbossBevelShapeCombo->itemData(idx).toInt()); });
    connect(m_edgeEmbossCornerCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) { m_canvas->setEdgeEmbossCorner(m_edgeEmbossCornerCombo->itemData(idx).toInt()); });

    // Edge trace
    connect(m_edgeTraceSourceCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) { m_canvas->setEdgeTraceSource(m_edgeTraceSourceCombo->itemData(idx).toInt()); });
    connect(m_edgeTraceThresholdSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setEdgeTraceThreshold(float(v)); });
    connect(m_edgeTraceBlurSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setEdgeTraceBlur(float(v)); });
    connect(m_edgeTraceScaleSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setEdgeTraceScale(float(v)); });
    connect(m_edgeTraceSpreadSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setEdgeTraceSpread(float(v)); });

    // Auto Normal
    connect(m_autoSourceCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx) { m_canvas->setAutoSource(m_autoSourceCombo->itemData(idx).toInt()); });
    connect(m_autoSmoothSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setAutoSmooth(float(v)); });
    connect(m_autoPillowStrengthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setAutoPillowStrength(float(v)); });
    connect(m_autoDetailStrengthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setAutoDetailStrength(float(v)); });
    connect(m_autoPanelStrengthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setAutoPanelStrength(float(v)); });
    connect(m_autoPanelThresholdSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setAutoPanelThreshold(float(v)); });
    connect(m_autoPanelWidthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_canvas->setAutoPanelWidth(float(v)); });

    // Tool buttons — exclusive group
    const QList<QPushButton*> allTools = {
        m_pencilBtn, m_eraserBtn, m_blurBtn, m_smudgeBtn, m_fillBtn, m_curveBtn, m_blendBtn,
        m_selectRectBtn, m_selectEllipseBtn, m_selectPolyBtn,
        m_selectFreehandBtn, m_selectWandBtn
    };

    connect(m_pencilBtn, &QPushButton::toggled, this, [this, allTools](bool checked) {
        if (checked) {
            setExclusiveTool(m_pencilBtn);
            m_canvas->setCanvasMode(MapPaintCanvas::CanvasMode::Paint);
            m_canvas->setToolMode(MapPaintCanvas::Tool::Pencil);
            updateToolOptions();
        } else {
            bool any = false;
            for (auto* b : allTools) if (b->isChecked()) { any = true; break; }
            if (!any) { QSignalBlocker sb(m_pencilBtn); m_pencilBtn->setChecked(true); }
        }
    });

    connect(m_eraserBtn, &QPushButton::toggled, this, [this, allTools](bool checked) {
        if (checked) {
            setExclusiveTool(m_eraserBtn);
            m_canvas->setCanvasMode(MapPaintCanvas::CanvasMode::Paint);
            m_canvas->setToolMode(MapPaintCanvas::Tool::Eraser);
            updateToolOptions();
        } else {
            bool any = false;
            for (auto* b : allTools) if (b->isChecked()) { any = true; break; }
            if (!any) { QSignalBlocker sb(m_eraserBtn); m_eraserBtn->setChecked(true); }
        }
    });

    connect(m_blurBtn, &QPushButton::toggled, this, [this, allTools](bool checked) {
        if (checked) {
            setExclusiveTool(m_blurBtn);
            m_canvas->setCanvasMode(MapPaintCanvas::CanvasMode::Paint);
            m_canvas->setToolMode(MapPaintCanvas::Tool::Blur);
            updateToolOptions();
        } else {
            bool any = false;
            for (auto* b : allTools) if (b->isChecked()) { any = true; break; }
            if (!any) { QSignalBlocker sb(m_blurBtn); m_blurBtn->setChecked(true); }
        }
    });

    connect(m_smudgeBtn, &QPushButton::toggled, this, [this, allTools](bool checked) {
        if (checked) {
            setExclusiveTool(m_smudgeBtn);
            m_canvas->setCanvasMode(MapPaintCanvas::CanvasMode::Paint);
            m_canvas->setToolMode(MapPaintCanvas::Tool::Smudge);
            updateToolOptions();
        } else {
            bool any = false;
            for (auto* b : allTools) if (b->isChecked()) { any = true; break; }
            if (!any) { QSignalBlocker sb(m_smudgeBtn); m_smudgeBtn->setChecked(true); }
        }
    });

    connect(m_fillBtn, &QPushButton::toggled, this, [this, allTools](bool checked) {
        if (checked) {
            setExclusiveTool(m_fillBtn);
            m_canvas->setCanvasMode(MapPaintCanvas::CanvasMode::Paint);
            m_canvas->setToolMode(MapPaintCanvas::Tool::Pencil);
            updateToolOptions();
        } else {
            bool any = false;
            for (auto* b : allTools) if (b->isChecked()) { any = true; break; }
            if (!any) { QSignalBlocker sb(m_fillBtn); m_fillBtn->setChecked(true); }
        }
    });

    connect(m_curveBtn, &QPushButton::toggled, this, [this, allTools](bool checked) {
        if (checked) {
            setExclusiveTool(m_curveBtn);
            m_canvas->setCanvasMode(MapPaintCanvas::CanvasMode::Paint);
            m_canvas->setToolMode(MapPaintCanvas::Tool::Curve);
            updateToolOptions();
        } else {
            bool any = false;
            for (auto* b : allTools) if (b->isChecked()) { any = true; break; }
            if (!any) { QSignalBlocker sb(m_curveBtn); m_curveBtn->setChecked(true); }
        }
    });

    connect(m_blendBtn, &QPushButton::toggled, this, [this, allTools](bool checked) {
        if (checked) {
            setExclusiveTool(m_blendBtn);
            m_canvas->setCanvasMode(MapPaintCanvas::CanvasMode::Paint);
            m_canvas->setToolMode(MapPaintCanvas::Tool::Blend);
            updateToolOptions();
        } else {
            bool any = false;
            for (auto* b : allTools) if (b->isChecked()) { any = true; break; }
            if (!any) { QSignalBlocker sb(m_blendBtn); m_blendBtn->setChecked(true); }
        }
    });

    using SelTool = MapPaintCanvas::SelectionTool;
    auto makeSelTool = [this, allTools](QPushButton* btn, SelTool selTool) {
        connect(btn, &QPushButton::toggled, this,
                [this, btn, selTool, allTools](bool checked) {
            if (checked) {
                setExclusiveTool(btn);
                m_canvas->setCanvasMode(MapPaintCanvas::CanvasMode::Select);
                m_canvas->setSelectionTool(selTool);
                updateToolOptions();
            } else {
                bool any = false;
                for (auto* b : allTools) if (b->isChecked()) { any = true; break; }
                if (!any) { QSignalBlocker sb(btn); btn->setChecked(true); }
            }
        });
    };
    makeSelTool(m_selectRectBtn,     SelTool::Rectangle);
    makeSelTool(m_selectEllipseBtn,  SelTool::Ellipse);
    makeSelTool(m_selectPolyBtn,     SelTool::Polygon);
    makeSelTool(m_selectFreehandBtn, SelTool::Freehand);
    makeSelTool(m_selectWandBtn,     SelTool::MagicWand);

    connect(m_canvas, &NormalMapPaintCanvas::selectionChanged,
            this, &NormalMapWorkspace::onSelectionChanged);

    connect(m_canvas, &NormalMapPaintCanvas::companionModified,
            this, [this] { emit definitionsChanged(); });

    // Light connections
    connect(m_lightXSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        m_canvas->setLightXY(v, m_lightYSpin->value());
    });
    connect(m_lightYSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        m_canvas->setLightXY(m_lightXSpin->value(), v);
    });
    connect(m_lightZSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        m_canvas->setLightZ(v);
    });
    connect(m_lightColorBtn, &QPushButton::clicked,
            this, &NormalMapWorkspace::onPickLightColor);
    connect(m_canvas, &NormalMapPaintCanvas::lightPositionChanged, this, [this](int x, int y) {
        QSignalBlocker bx(m_lightXSpin), by(m_lightYSpin);
        m_lightXSpin->setValue(x);
        m_lightYSpin->setValue(y);
    });
    connect(m_canvas, &NormalMapPaintCanvas::lightZChanged, this, [this](int z) {
        QSignalBlocker b(m_lightZSpin);
        m_lightZSpin->setValue(z);
    });
    connect(m_lightStrSlider, &QSlider::valueChanged, this, [this](int v) {
        m_lightStrLabel->setText(QString::number(v) + QStringLiteral("%"));
        m_canvas->setLightStrength(v / 100.0f);
    });
    connect(m_ambientSlider, &QSlider::valueChanged, this, [this](int v) {
        m_ambientLabel->setText(QString::number(v) + QStringLiteral("%"));
        m_canvas->setAmbientStrength(v / 100.0f);
    });

    // Set initial light color swatch
    {
        QPixmap pix(16, 16);
        pix.fill(m_lightColor);
        m_lightColorBtn->setIcon(QIcon(pix));
    }

    // Layer buttons
    connect(m_addLayerBtn, &QPushButton::clicked, this, [this] {
        m_canvas->addLayer();
    });

    connect(m_removeLayerBtn, &QPushButton::clicked, this, [this] {
        if (!m_layerList) return;
        const int row = m_layerList->currentRow();
        if (row < 0) return;
        const int idx = (m_canvas->layerCount() - 1) - row;
        m_canvas->removeLayer(idx);
    });

    connect(m_moveLayerUpBtn, &QPushButton::clicked, this, [this] {
        if (!m_layerList) return;
        const int row = m_layerList->currentRow();
        if (row < 0) return;
        // In the UI list, higher = top of stack; "Move Up" = raise draw order
        const int idx = (m_canvas->layerCount() - 1) - row;
        m_canvas->moveLayerUp(idx);
    });

    connect(m_moveLayerDownBtn, &QPushButton::clicked, this, [this] {
        if (!m_layerList) return;
        const int row = m_layerList->currentRow();
        if (row < 0) return;
        // In the UI list, higher = top of stack; "Move Down" = lower draw order
        const int idx = (m_canvas->layerCount() - 1) - row;
        m_canvas->moveLayerDown(idx);
    });

    // Layer list: selection → active layer
    connect(m_layerList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || m_canvas->layerCount() == 0) return;
        const int idx = (m_canvas->layerCount() - 1) - row;
        m_canvas->setActiveLayer(idx);
    });

    // Layer list: item changed → visibility or rename
    connect(m_layerList, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (!m_canvas || m_canvas->layerCount() == 0) return;
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= m_canvas->layerCount()) return;

        // Read all data before any canvas calls (canvas may rebuild the list)
        const bool   newVisible = (item->checkState() == Qt::Checked);
        const QString newName   = item->text().trimmed();

        const bool   curVisible = m_canvas->layer(idx).visible;
        const QString curName   = m_canvas->layer(idx).name;

        if (curVisible != newVisible)
            m_canvas->setLayerVisible(idx, newVisible);
        if (!newName.isEmpty() && curName != newName)
            m_canvas->setLayerName(idx, newName);
    });

    // Initialize canvas defaults
    m_canvas->setNormal(0.0f, 0.0f, 1.0f);  // flat up
    updateToolOptions();
}

QWidget* NormalMapWorkspace::buildPreviewBar()
{
    auto* container = new QWidget(this);
    auto* vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);

    auto makeIconLbl = [](const QString& path, const QString& tip, QWidget* parent) {
        auto* lbl = new QLabel(parent);
        lbl->setPixmap(QIcon(path).pixmap(16, 16));
        lbl->setToolTip(tip);
        return lbl;
    };

    // === Row 1: Zoom + View blend ===
    {
        auto* bar = new QFrame(container);
        bar->setFrameShape(QFrame::StyledPanel);
        bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto* row = new QHBoxLayout(bar);
        row->setContentsMargins(6, 3, 6, 3);
        row->setSpacing(6);

        // Atlas normal-map toggle
        m_atlasNormalMapBtn = new QPushButton(
            QIcon(QStringLiteral(":/icons/normal-map.svg")), tr("Normal Map"), bar);
        m_atlasNormalMapBtn->setCheckable(true);
        m_atlasNormalMapBtn->setEnabled(false);
        m_atlasNormalMapBtn->setToolTip(tr("Mark this atlas as having a normal map"));
        row->addWidget(m_atlasNormalMapBtn);
        row->addWidget(makeVSeparator(bar));

        // Zoom (magnifying glass — matches SpriteEditorPanel / NineSliceWorkspace style)
        {
            QPixmap pix(16, 16);
            pix.fill(Qt::transparent);
            QPainter painter(&pix);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(palette().color(QPalette::WindowText), 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QPointF(6, 6), 4.5, 4.5);
            painter.drawLine(QPointF(9.2, 9.2), QPointF(14, 14));
            painter.end();
            auto* zoomLabel = new QLabel(bar);
            zoomLabel->setPixmap(pix);
            zoomLabel->setToolTip(tr("Zoom"));
            row->addWidget(zoomLabel);
        }
        m_zoomSpin = new QDoubleSpinBox(bar);
        m_zoomSpin->setRange(10.0, 1600.0);
        m_zoomSpin->setValue(100.0);
        m_zoomSpin->setSuffix(QStringLiteral("%"));
        m_zoomSpin->setSingleStep(10.0);
        m_zoomSpin->setFixedWidth(80);
        row->addWidget(m_zoomSpin);

        row->addWidget(makeVSeparator(bar));

        // View slider: Normal Map ←—— slider ——→ Lit Preview
        row->addWidget(makeIconLbl(":/icons/normal-map.png", tr("Normal map"), bar));
        m_viewSlider = new QSlider(Qt::Horizontal, bar);
        m_viewSlider->setRange(0, 100);
        m_viewSlider->setValue(100);
        m_viewSlider->setFixedWidth(120);
        m_viewSlider->setToolTip(tr("0 = raw normal map  ·  100 = lit preview"));
        row->addWidget(m_viewSlider);
        row->addWidget(makeIconLbl(":/icons/scan.svg", tr("Lit preview"), bar));

        row->addStretch();
        vbox->addWidget(bar);
    }

    // === Row 2: Light options ===
    {
        auto* bar = new QFrame(container);
        bar->setFrameShape(QFrame::StyledPanel);
        bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto* row = new QHBoxLayout(bar);
        row->setContentsMargins(6, 3, 6, 3);
        row->setSpacing(6);

        row->addWidget(makeIconLbl(":/icons/normal-map.svg", tr("Light"), bar));
        row->addWidget(makeVSeparator(bar));

        row->addWidget(new QLabel(tr("X:"), bar));
        m_lightXSpin = new QSpinBox(bar);
        m_lightXSpin->setRange(-9999, 9999);
        m_lightXSpin->setValue(0);
        m_lightXSpin->setFixedWidth(60);
        m_lightXSpin->setToolTip(tr("Light X offset from sprite centre"));
        row->addWidget(m_lightXSpin);

        row->addWidget(new QLabel(tr("Y:"), bar));
        m_lightYSpin = new QSpinBox(bar);
        m_lightYSpin->setRange(-9999, 9999);
        m_lightYSpin->setValue(0);
        m_lightYSpin->setFixedWidth(60);
        m_lightYSpin->setToolTip(tr("Light Y offset from sprite centre"));
        row->addWidget(m_lightYSpin);

        row->addWidget(new QLabel(tr("Z:"), bar));
        m_lightZSpin = new QSpinBox(bar);
        m_lightZSpin->setRange(0, 9999);
        m_lightZSpin->setValue(100);
        m_lightZSpin->setFixedWidth(60);
        m_lightZSpin->setToolTip(tr("Light Z height above sprite"));
        row->addWidget(m_lightZSpin);

        row->addWidget(makeVSeparator(bar));

        m_lightColorBtn = new QPushButton(bar);
        m_lightColorBtn->setFixedSize(28, 28);
        m_lightColorBtn->setIconSize(QSize(16, 16));
        m_lightColorBtn->setToolTip(tr("Choose light color"));
        row->addWidget(m_lightColorBtn);

        row->addWidget(makeVSeparator(bar));

        row->addWidget(makeIconLbl(":/icons/speed.svg", tr("Light strength"), bar));
        m_lightStrSlider = new QSlider(Qt::Horizontal, bar);
        m_lightStrSlider->setRange(0, 200);
        m_lightStrSlider->setValue(100);
        m_lightStrSlider->setToolTip(tr("Light strength"));
        row->addWidget(m_lightStrSlider, 1);
        m_lightStrLabel = new QLabel(QStringLiteral("100%"), bar);
        m_lightStrLabel->setFixedWidth(38);
        m_lightStrLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(m_lightStrLabel);

        row->addWidget(makeVSeparator(bar));

        row->addWidget(makeIconLbl(":/icons/amb.svg", tr("Ambient light level"), bar));
        m_ambientSlider = new QSlider(Qt::Horizontal, bar);
        m_ambientSlider->setRange(0, 100);
        m_ambientSlider->setValue(20);
        m_ambientSlider->setToolTip(tr("Ambient light level"));
        row->addWidget(m_ambientSlider, 1);
        m_ambientLabel = new QLabel(QStringLiteral("20%"), bar);
        m_ambientLabel->setFixedWidth(38);
        m_ambientLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(m_ambientLabel);

        m_lightBar = bar;
        vbox->addWidget(bar);
    }

    return container;
}

QWidget* NormalMapWorkspace::buildRightPanel()
{
    m_rightPanel = new QScrollArea(this);
    auto* scroll = m_rightPanel;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumWidth(180);
    scroll->setMaximumWidth(240);

    auto* content = new QWidget;
    auto* layout  = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    // ── Tools ─────────────────────────────────────────────────────────────────
    layout->addWidget(makeSectionHeader("Tools", content));

    {
        auto* toolsWidget = new QWidget(content);
        auto* grid = new QGridLayout(toolsWidget);
        grid->setSpacing(3);
        grid->setContentsMargins(0, 0, 0, 4);

        m_pencilBtn = makeIconBtn(QStringLiteral(":/icons/pencil.svg"),
                                    tr("Pencil"), true, toolsWidget);
        m_pencilBtn->setChecked(true);

        m_eraserBtn = makeIconBtn(QStringLiteral(":/icons/eraser.svg"),
                                    tr("Eraser (paints flat normal)"), true, toolsWidget);

        m_blurBtn = makeIconBtn(QStringLiteral(":/icons/blur.svg"),
                                  tr("Blur"), true, toolsWidget);

        m_smudgeBtn = makeIconBtn(QStringLiteral(":/icons/smooth.svg"),
                                    tr("Smudge"), true, toolsWidget);

        m_blendBtn = makeIconBtn(QStringLiteral(":/icons/blend.svg"),
                                   tr("Blend: smooth transition between two adjacent flat-color areas.\n"
                                      "Paint over the boundary; bails out if the area already has a transition."),
                                   true, toolsWidget);

        m_fillBtn = makeIconBtn(QStringLiteral(":/icons/gradient.svg"),
                                  tr("Fill (drag to fill selection with normals)"),
                                  true, toolsWidget);

        m_curveBtn = makeIconBtn(QStringLiteral(":/icons/curve.svg"),
                                   tr("Curve: click P1, click P2, move to bend, click to commit\n"
                                      "RMB commits as straight line  |  Esc cancels"),
                                   true, toolsWidget);

        m_selectRectBtn = makeIconBtn(QStringLiteral(":/icons/rectangle-wide.svg"),
                                        tr("Rect selection"), true, toolsWidget);
        m_selectEllipseBtn = makeIconBtn(QStringLiteral(":/icons/circle.svg"),
                                           tr("Ellipse selection"), true, toolsWidget);
        m_selectPolyBtn = makeIconBtn(QStringLiteral(":/icons/polygon.svg"),
                                        tr("Polygon selection (dblclick or RMB to close)"),
                                        true, toolsWidget);
        m_selectFreehandBtn = makeIconBtn(QStringLiteral(":/icons/freehand.svg"),
                                            tr("Freehand selection"), true, toolsWidget);
        m_selectWandBtn = makeIconBtn(QStringLiteral(":/icons/magic-wand.svg"),
                                        tr("Magic wand"), true, toolsWidget);

        // Row 0: primary paint tools
        grid->addWidget(m_pencilBtn,         0, 0);
        grid->addWidget(m_eraserBtn,         0, 1);
        grid->addWidget(m_curveBtn,          0, 2);
        grid->addWidget(m_fillBtn,           0, 3);
        // Row 1: secondary paint tools
        grid->addWidget(m_smudgeBtn,         1, 0);
        grid->addWidget(m_blurBtn,           1, 1);
        grid->addWidget(m_blendBtn,          1, 2);
        // Row 2: selection tools
        grid->addWidget(m_selectRectBtn,     2, 0);
        grid->addWidget(m_selectEllipseBtn,  2, 1);
        grid->addWidget(m_selectPolyBtn,     2, 2);
        grid->addWidget(m_selectFreehandBtn, 2, 3);
        grid->addWidget(m_selectWandBtn,     2, 4);

        layout->addWidget(toolsWidget);
    }

    layout->addWidget(makeSeparator(content));

    // ── Tool Options ──────────────────────────────────────────────────────────
    layout->addWidget(makeSectionHeader("Tool Options", content));

    // Pencil type selector (flat / bevel rise / bevel carve)
    m_pencilTypeRow = new QWidget(content);
    {
        auto* row = new QHBoxLayout(m_pencilTypeRow);
        row->setContentsMargins(0, 0, 0, 4);
        row->setSpacing(4);
        row->addWidget(new QLabel(tr("Type:"), m_pencilTypeRow));
        m_pencilTypeCombo = new QComboBox(m_pencilTypeRow);
        m_pencilTypeCombo->addItem(tr("Flat"),        int(NormalMapPaintCanvas::PencilType::Flat));
        m_pencilTypeCombo->addItem(tr("Bevel Rise"),  int(NormalMapPaintCanvas::PencilType::BevelRise));
        m_pencilTypeCombo->addItem(tr("Bevel Carve"), int(NormalMapPaintCanvas::PencilType::BevelCarve));
        m_pencilTypeCombo->setToolTip(tr("Pencil type"));
        row->addWidget(m_pencilTypeCombo, 1);
    }
    layout->addWidget(m_pencilTypeRow);
    m_pencilTypeRow->hide();

    // Normal wheel — "From" (pencil + Slope/TwoSides fill types)
    // Measure button sits beside the angle spinboxes at the bottom of the wheel.
    {
        m_wheelWidget = new NormalWheelWidget(content);

        m_pickBtn = new QPushButton(
            QIcon(QStringLiteral(":/icons/eyedropper.svg")), QString(), content);
        m_pickBtn->setCheckable(true);
        m_pickBtn->setFixedSize(24, 24);
        m_pickBtn->setIconSize(QSize(16, 16));
        m_pickBtn->setToolTip(tr("Pick normal direction from a pixel in the sprite."));

        m_measureBtn = new QPushButton(
            QIcon(QStringLiteral(":/icons/elevation.svg")), QString(), content);
        m_measureBtn->setCheckable(true);
        m_measureBtn->setFixedSize(24, 24);
        m_measureBtn->setIconSize(QSize(16, 16));
        m_measureBtn->setToolTip(tr(
            "Measure surface angles from the sprite.\n"
            "Click 3 points: two along a surface edge,\n"
            "then one more to set the elevation angle."));

        auto* centerRow = new QHBoxLayout;
        centerRow->addStretch();
        centerRow->addWidget(m_pickBtn,    0, Qt::AlignBottom);
        centerRow->addWidget(m_wheelWidget, 0, Qt::AlignBottom);
        centerRow->addWidget(m_measureBtn, 0, Qt::AlignBottom);
        centerRow->addStretch();
        layout->addLayout(centerRow);
    }

    // Fill "To" wheel (Two Sides type only)
    m_fillToRow = new QWidget(content);
    {
        auto* vbox = new QVBoxLayout(m_fillToRow);
        vbox->setContentsMargins(0, 6, 0, 0);
        vbox->setSpacing(2);
        auto* lbl = new QLabel(tr("To:"), m_fillToRow);
        lbl->setAlignment(Qt::AlignHCenter);
        vbox->addWidget(lbl);
        m_fillEndWheelWidget = new NormalWheelWidget(m_fillToRow);
        auto* centerRow2 = new QHBoxLayout;
        centerRow2->addStretch();
        centerRow2->addWidget(m_fillEndWheelWidget);
        centerRow2->addStretch();
        vbox->addLayout(centerRow2);
    }
    layout->addWidget(m_fillToRow);
    m_fillToRow->hide();

    // Fill type selector
    m_fillTypeRow = new QWidget(content);
    {
        auto* row = new QHBoxLayout(m_fillTypeRow);
        row->setContentsMargins(0, 0, 0, 4);
        row->setSpacing(4);
        row->addWidget(new QLabel(tr("Type:"), m_fillTypeRow));
        m_fillTypeCombo = new QComboBox(m_fillTypeRow);
        m_fillTypeCombo->addItem(tr("Slope"),       int(NormalMapPaintCanvas::FillType::Slope));
        m_fillTypeCombo->addItem(tr("Two Sides"),   int(NormalMapPaintCanvas::FillType::TwoSides));
        m_fillTypeCombo->addItem(tr("Prism"),       int(NormalMapPaintCanvas::FillType::Prism));
        m_fillTypeCombo->addItem(tr("Dome"),        int(NormalMapPaintCanvas::FillType::Dome));
        m_fillTypeCombo->addItem(tr("Cone"),        int(NormalMapPaintCanvas::FillType::Cone));
        m_fillTypeCombo->addItem(tr("Donut"),       int(NormalMapPaintCanvas::FillType::Donut));
        m_fillTypeCombo->addItem(tr("Cylinder"),    int(NormalMapPaintCanvas::FillType::Cylinder));
        m_fillTypeCombo->addItem(tr("Emboss"),      int(NormalMapPaintCanvas::FillType::Emboss));
        m_fillTypeCombo->addItem(tr("Height Field"), int(NormalMapPaintCanvas::FillType::HeightField));
        m_fillTypeCombo->addItem(tr("Edge Emboss"), int(NormalMapPaintCanvas::FillType::EdgeEmboss));
        m_fillTypeCombo->addItem(tr("Edge Trace"),  int(NormalMapPaintCanvas::FillType::EdgeTrace));
        m_fillTypeCombo->addItem(tr("Auto Normal"), int(NormalMapPaintCanvas::FillType::AutoNormal));
        m_fillTypeCombo->setToolTip(tr("Fill shape"));
        row->addWidget(m_fillTypeCombo, 1);
    }
    layout->addWidget(m_fillTypeRow);
    m_fillTypeRow->hide();

    // "Fit to selection" checkbox (fill tool only)
    m_fillFitSelChk = new QCheckBox(tr("Fit to selection"), content);
    m_fillFitSelChk->setToolTip(
        tr("Stretch the fill to span exactly the selected pixels.\n"
           "Linear types: fit along the drag axis.\n"
           "Radial types: fit out to the furthest selected pixel."));
    layout->addWidget(m_fillFitSelChk);
    m_fillFitSelChk->hide();

    // "Invert" checkbox (all fill types)
    m_fillInvertChk = new QCheckBox(tr("Invert"), content);
    m_fillInvertChk->setToolTip(tr("Reverse the fill direction"));
    layout->addWidget(m_fillInvertChk);
    m_fillInvertChk->hide();

    // Prism side widths (T/R/B/L, like CSS border-width)
    m_prismSizeRow = new QWidget(content);
    {
        auto* grid = new QGridLayout(m_prismSizeRow);
        grid->setContentsMargins(0, 0, 0, 4);
        grid->setSpacing(3);

        auto makePrismSpin = [&](QWidget* parent, const QString& tip) -> QDoubleSpinBox* {
            auto* spin = new QDoubleSpinBox(parent);
            spin->setRange(0.0, 1.0);
            spin->setSingleStep(0.1);
            spin->setDecimals(2);
            spin->setValue(0.5);
            spin->setFixedWidth(55);
            spin->setToolTip(tip);
            return spin;
        };

        grid->addWidget(new QLabel(tr("T:"), m_prismSizeRow), 0, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_prismSideTopSpin = makePrismSpin(m_prismSizeRow, tr("Top border width (0=none, 1=full half-height)"));
        grid->addWidget(m_prismSideTopSpin, 0, 1);

        grid->addWidget(new QLabel(tr("R:"), m_prismSizeRow), 0, 2, Qt::AlignRight | Qt::AlignVCenter);
        m_prismSideRightSpin = makePrismSpin(m_prismSizeRow, tr("Right border width (0=none, 1=full half-width)"));
        grid->addWidget(m_prismSideRightSpin, 0, 3);

        grid->addWidget(new QLabel(tr("B:"), m_prismSizeRow), 1, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_prismSideBottomSpin = makePrismSpin(m_prismSizeRow, tr("Bottom border width (0=none, 1=full half-height)"));
        grid->addWidget(m_prismSideBottomSpin, 1, 1);

        grid->addWidget(new QLabel(tr("L:"), m_prismSizeRow), 1, 2, Qt::AlignRight | Qt::AlignVCenter);
        m_prismSideLeftSpin = makePrismSpin(m_prismSizeRow, tr("Left border width (0=none, 1=full half-width)"));
        grid->addWidget(m_prismSideLeftSpin, 1, 3);

        grid->addWidget(new QLabel(tr("Angle:"), m_prismSizeRow), 2, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_prismAngleSpin = new QSpinBox(m_prismSizeRow);
        m_prismAngleSpin->setRange(0, 90);
        m_prismAngleSpin->setValue(90);
        m_prismAngleSpin->setSuffix(QStringLiteral("°"));
        m_prismAngleSpin->setToolTip(tr("Side inclination (90°=vertical, 0°=flat)"));
        grid->addWidget(m_prismAngleSpin, 2, 1);
    }
    layout->addWidget(m_prismSizeRow);
    m_prismSizeRow->hide();

    // Cone edge row
    m_coneEdgeRow = new QWidget(content);
    {
        auto* row = new QHBoxLayout(m_coneEdgeRow);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        row->addWidget(new QLabel(tr("Edge X:"), m_coneEdgeRow));
        m_coneEdgeXSpin = new QSpinBox(m_coneEdgeRow);
        m_coneEdgeXSpin->setRange(0, 100);
        m_coneEdgeXSpin->setValue(50);
        m_coneEdgeXSpin->setSuffix(QStringLiteral("%"));
        m_coneEdgeXSpin->setToolTip(tr("Cone apex X position as % of sprite width"));
        row->addWidget(m_coneEdgeXSpin);
        row->addWidget(new QLabel(tr("Y:"), m_coneEdgeRow));
        m_coneEdgeYSpin = new QSpinBox(m_coneEdgeRow);
        m_coneEdgeYSpin->setRange(0, 100);
        m_coneEdgeYSpin->setValue(50);
        m_coneEdgeYSpin->setSuffix(QStringLiteral("%"));
        m_coneEdgeYSpin->setToolTip(tr("Cone apex Y position as % of sprite height"));
        row->addWidget(m_coneEdgeYSpin);
        row->addStretch();
    }
    layout->addWidget(m_coneEdgeRow);
    m_coneEdgeRow->hide();

    // Donut length row
    m_donutLengthRow = new QWidget(content);
    {
        auto* row = new QHBoxLayout(m_donutLengthRow);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        row->addWidget(new QLabel(tr("Thickness:"), m_donutLengthRow));
        m_donutLengthSpin = new QDoubleSpinBox(m_donutLengthRow);
        m_donutLengthSpin->setRange(0.01, 1.0);
        m_donutLengthSpin->setSingleStep(0.05);
        m_donutLengthSpin->setDecimals(2);
        m_donutLengthSpin->setValue(0.5);
        m_donutLengthSpin->setToolTip(tr("Donut thickness (1.0 = solid disc, 0.0 = no ring).\nScroll mouse wheel while dragging to adjust."));
        row->addWidget(m_donutLengthSpin);
        row->addStretch();
    }
    layout->addWidget(m_donutLengthRow);
    m_donutLengthRow->hide();

    // Roundness (Dome / Cone / Cylinder / Donut)
    m_roundnessRow = new QWidget(content);
    {
        auto* row = new QHBoxLayout(m_roundnessRow);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        row->addWidget(new QLabel(tr("Roundness:"), m_roundnessRow));
        m_roundnessSpin = new QDoubleSpinBox(m_roundnessRow);
        m_roundnessSpin->setRange(0.01, 1.0);
        m_roundnessSpin->setSingleStep(0.05);
        m_roundnessSpin->setDecimals(2);
        m_roundnessSpin->setValue(1.0);
        m_roundnessSpin->setToolTip(tr(
            "Controls how much of the full shape arc is used.\n"
            "1.0 = full hemisphere/cone/cylinder (max tilt at edge).\n"
            "0.5 = softer, only half the arc (gentler slopes)."));
        row->addWidget(m_roundnessSpin);
        row->addStretch();
    }
    layout->addWidget(m_roundnessRow);
    m_roundnessRow->hide();

    // Emboss options
    m_embossRow = new QWidget(content);
    {
        auto* grid = new QGridLayout(m_embossRow);
        grid->setContentsMargins(0, 0, 0, 4);
        grid->setSpacing(3);
        grid->setColumnStretch(1, 1);

        int row = 0;

        // Height
        grid->addWidget(new QLabel(tr("Height:"), m_embossRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_embossHeightSpin = new QDoubleSpinBox(m_embossRow);
        m_embossHeightSpin->setRange(0.0, 1.0);
        m_embossHeightSpin->setSingleStep(0.05);
        m_embossHeightSpin->setDecimals(2);
        m_embossHeightSpin->setValue(0.5);
        m_embossHeightSpin->setToolTip(tr("Bevel steepness at peak (0=flat, 1=vertical)"));
        grid->addWidget(m_embossHeightSpin, row++, 1);

        // Inner width
        grid->addWidget(new QLabel(tr("Width:"), m_embossRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_embossInnerWidthSpin = new QDoubleSpinBox(m_embossRow);
        m_embossInnerWidthSpin->setRange(0.5, 999.0);
        m_embossInnerWidthSpin->setSingleStep(1.0);
        m_embossInnerWidthSpin->setDecimals(1);
        m_embossInnerWidthSpin->setValue(10.0);
        m_embossInnerWidthSpin->setSuffix(tr(" px"));
        m_embossInnerWidthSpin->setToolTip(tr("Bevel width inward from the boundary (pixels)"));
        grid->addWidget(m_embossInnerWidthSpin, row++, 1);

        // Midpoint
        grid->addWidget(new QLabel(tr("Midpoint:"), m_embossRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_embossMidpointSpin = new QDoubleSpinBox(m_embossRow);
        m_embossMidpointSpin->setRange(0.0, 0.99);
        m_embossMidpointSpin->setSingleStep(0.05);
        m_embossMidpointSpin->setDecimals(2);
        m_embossMidpointSpin->setValue(0.0);
        m_embossMidpointSpin->setToolTip(tr("Peak position within bevel (0=at boundary edge, >0=ridge)"));
        grid->addWidget(m_embossMidpointSpin, row++, 1);

        // Bevel shape
        grid->addWidget(new QLabel(tr("Shape:"), m_embossRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_embossBevelShapeCombo = new QComboBox(m_embossRow);
        m_embossBevelShapeCombo->addItem(tr("Linear"),      0);
        m_embossBevelShapeCombo->addItem(tr("Smoothstep"),  1);
        m_embossBevelShapeCombo->addItem(tr("Rounded"),     2);
        m_embossBevelShapeCombo->addItem(tr("Cosine"),      3);
        m_embossBevelShapeCombo->addItem(tr("Sine"),        4);
        m_embossBevelShapeCombo->setToolTip(tr("Bevel height profile curve"));
        grid->addWidget(m_embossBevelShapeCombo, row++, 1);

        // Corner
        grid->addWidget(new QLabel(tr("Corner:"), m_embossRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_embossCornerCombo = new QComboBox(m_embossRow);
        m_embossCornerCombo->addItem(tr("Round"),  0);
        m_embossCornerCombo->addItem(tr("Sharp"),  1);
        m_embossCornerCombo->setToolTip(tr("Corner style for the bevel"));
        grid->addWidget(m_embossCornerCombo, row++, 1);

        // Strength
        grid->addWidget(new QLabel(tr("Strength:"), m_embossRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_embossStrengthSpin = new QDoubleSpinBox(m_embossRow);
        m_embossStrengthSpin->setRange(0.0, 1.0);
        m_embossStrengthSpin->setSingleStep(0.05);
        m_embossStrengthSpin->setDecimals(2);
        m_embossStrengthSpin->setValue(0.0);
        m_embossStrengthSpin->setToolTip(tr("Blend toward global angle direction (0=symmetric outward, 1=fully directed)"));
        grid->addWidget(m_embossStrengthSpin, row++, 1);

        // Angle
        grid->addWidget(new QLabel(tr("Angle:"), m_embossRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_embossAngleSpin = new QSpinBox(m_embossRow);
        m_embossAngleSpin->setRange(0, 359);
        m_embossAngleSpin->setValue(45);
        m_embossAngleSpin->setSuffix(QStringLiteral("°"));
        m_embossAngleSpin->setWrapping(true);
        m_embossAngleSpin->setToolTip(tr("Global light angle for directed bevel (used when Strength > 0)"));
        grid->addWidget(m_embossAngleSpin, row++, 1);

        // Edge mode
        grid->addWidget(new QLabel(tr("Edges:"), m_embossRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_embossEdgeModeCombo = new QComboBox(m_embossRow);
        m_embossEdgeModeCombo->addItem(tr("Clamp"),  0);
        m_embossEdgeModeCombo->addItem(tr("Wrap"),   1);
        m_embossEdgeModeCombo->addItem(tr("Mirror"), 2);
        m_embossEdgeModeCombo->setToolTip(tr("How to handle the bevel at sprite/selection borders"));
        grid->addWidget(m_embossEdgeModeCombo, row++, 1);
    }
    layout->addWidget(m_embossRow);
    m_embossRow->hide();

    // Height field options
    m_heightFieldRow = new QWidget(content);
    {
        auto* grid = new QGridLayout(m_heightFieldRow);
        grid->setContentsMargins(0, 0, 0, 4);
        grid->setSpacing(3);
        grid->setColumnStretch(1, 1);

        int row = 0;

        // Source channel
        grid->addWidget(new QLabel(tr("Source:"), m_heightFieldRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_heightFieldSourceCombo = new QComboBox(m_heightFieldRow);
        m_heightFieldSourceCombo->addItem(tr("Luminance"), 0);
        m_heightFieldSourceCombo->addItem(tr("Red"),       1);
        m_heightFieldSourceCombo->addItem(tr("Green"),     2);
        m_heightFieldSourceCombo->addItem(tr("Blue"),      3);
        m_heightFieldSourceCombo->addItem(tr("Alpha"),     4);
        m_heightFieldSourceCombo->setToolTip(tr("Sprite channel to interpret as height"));
        grid->addWidget(m_heightFieldSourceCombo, row++, 1);

        // Scale
        grid->addWidget(new QLabel(tr("Scale:"), m_heightFieldRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_heightFieldScaleSpin = new QDoubleSpinBox(m_heightFieldRow);
        m_heightFieldScaleSpin->setRange(0.1, 50.0);
        m_heightFieldScaleSpin->setSingleStep(0.5);
        m_heightFieldScaleSpin->setDecimals(1);
        m_heightFieldScaleSpin->setValue(5.0);
        m_heightFieldScaleSpin->setToolTip(tr("Steepness multiplier (higher = stronger normals)"));
        grid->addWidget(m_heightFieldScaleSpin, row++, 1);

        // Blur
        grid->addWidget(new QLabel(tr("Smooth:"), m_heightFieldRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_heightFieldBlurSpin = new QDoubleSpinBox(m_heightFieldRow);
        m_heightFieldBlurSpin->setRange(0.0, 20.0);
        m_heightFieldBlurSpin->setSingleStep(0.5);
        m_heightFieldBlurSpin->setDecimals(1);
        m_heightFieldBlurSpin->setValue(0.0);
        m_heightFieldBlurSpin->setSuffix(tr(" px"));
        m_heightFieldBlurSpin->setToolTip(tr("Pre-blur the height field before computing normals (reduces noise)"));
        grid->addWidget(m_heightFieldBlurSpin, row++, 1);
    }
    layout->addWidget(m_heightFieldRow);
    m_heightFieldRow->hide();

    // Edge emboss options
    m_edgeEmbossRow = new QWidget(content);
    {
        auto* grid = new QGridLayout(m_edgeEmbossRow);
        grid->setContentsMargins(0, 0, 0, 4);
        grid->setSpacing(3);
        grid->setColumnStretch(1, 1);
        int row = 0;

        grid->addWidget(new QLabel(tr("Source:"), m_edgeEmbossRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_edgeEmbossSourceCombo = new QComboBox(m_edgeEmbossRow);
        m_edgeEmbossSourceCombo->addItem(tr("Luminance"), 0);
        m_edgeEmbossSourceCombo->addItem(tr("Red"),       1);
        m_edgeEmbossSourceCombo->addItem(tr("Green"),     2);
        m_edgeEmbossSourceCombo->addItem(tr("Blue"),      3);
        m_edgeEmbossSourceCombo->setToolTip(tr("Channel used for edge detection"));
        grid->addWidget(m_edgeEmbossSourceCombo, row++, 1);

        grid->addWidget(new QLabel(tr("Threshold:"), m_edgeEmbossRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_edgeEmbossThresholdSpin = new QDoubleSpinBox(m_edgeEmbossRow);
        m_edgeEmbossThresholdSpin->setRange(0.0, 1.0);
        m_edgeEmbossThresholdSpin->setSingleStep(0.01);
        m_edgeEmbossThresholdSpin->setDecimals(2);
        m_edgeEmbossThresholdSpin->setValue(0.1);
        m_edgeEmbossThresholdSpin->setToolTip(tr("Edge sensitivity (lower = detect more edges)"));
        grid->addWidget(m_edgeEmbossThresholdSpin, row++, 1);

        grid->addWidget(new QLabel(tr("Smooth:"), m_edgeEmbossRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_edgeEmbossBlurSpin = new QDoubleSpinBox(m_edgeEmbossRow);
        m_edgeEmbossBlurSpin->setRange(0.0, 10.0);
        m_edgeEmbossBlurSpin->setSingleStep(0.5);
        m_edgeEmbossBlurSpin->setDecimals(1);
        m_edgeEmbossBlurSpin->setValue(1.0);
        m_edgeEmbossBlurSpin->setSuffix(tr(" px"));
        m_edgeEmbossBlurSpin->setToolTip(tr("Pre-blur before edge detection (reduces noise sensitivity)"));
        grid->addWidget(m_edgeEmbossBlurSpin, row++, 1);

        grid->addWidget(new QLabel(tr("Width:"), m_edgeEmbossRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_edgeEmbossWidthSpin = new QDoubleSpinBox(m_edgeEmbossRow);
        m_edgeEmbossWidthSpin->setRange(0.5, 999.0);
        m_edgeEmbossWidthSpin->setSingleStep(1.0);
        m_edgeEmbossWidthSpin->setDecimals(1);
        m_edgeEmbossWidthSpin->setValue(10.0);
        m_edgeEmbossWidthSpin->setSuffix(tr(" px"));
        m_edgeEmbossWidthSpin->setToolTip(tr("Bevel width from detected edge"));
        grid->addWidget(m_edgeEmbossWidthSpin, row++, 1);

        grid->addWidget(new QLabel(tr("Height:"), m_edgeEmbossRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_edgeEmbossHeightSpin = new QDoubleSpinBox(m_edgeEmbossRow);
        m_edgeEmbossHeightSpin->setRange(0.0, 1.0);
        m_edgeEmbossHeightSpin->setSingleStep(0.05);
        m_edgeEmbossHeightSpin->setDecimals(2);
        m_edgeEmbossHeightSpin->setValue(0.5);
        m_edgeEmbossHeightSpin->setToolTip(tr("Bevel steepness at peak"));
        grid->addWidget(m_edgeEmbossHeightSpin, row++, 1);

        grid->addWidget(new QLabel(tr("Midpoint:"), m_edgeEmbossRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_edgeEmbossMidpointSpin = new QDoubleSpinBox(m_edgeEmbossRow);
        m_edgeEmbossMidpointSpin->setRange(0.0, 0.99);
        m_edgeEmbossMidpointSpin->setSingleStep(0.05);
        m_edgeEmbossMidpointSpin->setDecimals(2);
        m_edgeEmbossMidpointSpin->setValue(0.5);
        m_edgeEmbossMidpointSpin->setToolTip(tr("Peak position (0=at edge, 0.5=ridge, >0=symmetric bevel)"));
        grid->addWidget(m_edgeEmbossMidpointSpin, row++, 1);

        grid->addWidget(new QLabel(tr("Shape:"), m_edgeEmbossRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_edgeEmbossBevelShapeCombo = new QComboBox(m_edgeEmbossRow);
        m_edgeEmbossBevelShapeCombo->addItem(tr("Linear"),     0);
        m_edgeEmbossBevelShapeCombo->addItem(tr("Smoothstep"), 1);
        m_edgeEmbossBevelShapeCombo->addItem(tr("Rounded"),    2);
        m_edgeEmbossBevelShapeCombo->addItem(tr("Cosine"),     3);
        m_edgeEmbossBevelShapeCombo->addItem(tr("Sine"),       4);
        m_edgeEmbossBevelShapeCombo->setToolTip(tr("Bevel height profile curve"));
        grid->addWidget(m_edgeEmbossBevelShapeCombo, row++, 1);

        grid->addWidget(new QLabel(tr("Corner:"), m_edgeEmbossRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_edgeEmbossCornerCombo = new QComboBox(m_edgeEmbossRow);
        m_edgeEmbossCornerCombo->addItem(tr("Round"), 0);
        m_edgeEmbossCornerCombo->addItem(tr("Sharp"), 1);
        m_edgeEmbossCornerCombo->setToolTip(tr("Corner style where edges meet"));
        grid->addWidget(m_edgeEmbossCornerCombo, row++, 1);
    }
    layout->addWidget(m_edgeEmbossRow);
    m_edgeEmbossRow->hide();

    // Edge trace options
    m_edgeTraceRow = new QWidget(content);
    {
        auto* grid = new QGridLayout(m_edgeTraceRow);
        grid->setContentsMargins(0, 0, 0, 4);
        grid->setSpacing(3);
        grid->setColumnStretch(1, 1);
        int row = 0;

        grid->addWidget(new QLabel(tr("Source:"), m_edgeTraceRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_edgeTraceSourceCombo = new QComboBox(m_edgeTraceRow);
        m_edgeTraceSourceCombo->addItem(tr("Luminance"), 0);
        m_edgeTraceSourceCombo->addItem(tr("Red"),       1);
        m_edgeTraceSourceCombo->addItem(tr("Green"),     2);
        m_edgeTraceSourceCombo->addItem(tr("Blue"),      3);
        m_edgeTraceSourceCombo->setToolTip(tr("Channel to detect edges in"));
        grid->addWidget(m_edgeTraceSourceCombo, row++, 1);

        grid->addWidget(new QLabel(tr("Threshold:"), m_edgeTraceRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_edgeTraceThresholdSpin = new QDoubleSpinBox(m_edgeTraceRow);
        m_edgeTraceThresholdSpin->setRange(0.0, 1.0);
        m_edgeTraceThresholdSpin->setSingleStep(0.01);
        m_edgeTraceThresholdSpin->setDecimals(2);
        m_edgeTraceThresholdSpin->setValue(0.1);
        m_edgeTraceThresholdSpin->setToolTip(tr("Edge sensitivity — lower detects more edges"));
        grid->addWidget(m_edgeTraceThresholdSpin, row++, 1);

        grid->addWidget(new QLabel(tr("Smooth:"), m_edgeTraceRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_edgeTraceBlurSpin = new QDoubleSpinBox(m_edgeTraceRow);
        m_edgeTraceBlurSpin->setRange(0.0, 10.0);
        m_edgeTraceBlurSpin->setSingleStep(0.5);
        m_edgeTraceBlurSpin->setDecimals(1);
        m_edgeTraceBlurSpin->setValue(1.0);
        m_edgeTraceBlurSpin->setSuffix(tr(" px"));
        m_edgeTraceBlurSpin->setToolTip(tr("Pre-blur before edge detection (reduces noise)"));
        grid->addWidget(m_edgeTraceBlurSpin, row++, 1);

        grid->addWidget(new QLabel(tr("Scale:"), m_edgeTraceRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_edgeTraceScaleSpin = new QDoubleSpinBox(m_edgeTraceRow);
        m_edgeTraceScaleSpin->setRange(0.1, 50.0);
        m_edgeTraceScaleSpin->setSingleStep(0.5);
        m_edgeTraceScaleSpin->setDecimals(1);
        m_edgeTraceScaleSpin->setValue(5.0);
        m_edgeTraceScaleSpin->setToolTip(tr("Normal tilt steepness at detected edges"));
        grid->addWidget(m_edgeTraceScaleSpin, row++, 1);

        grid->addWidget(new QLabel(tr("Spread:"), m_edgeTraceRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_edgeTraceSpreadSpin = new QDoubleSpinBox(m_edgeTraceRow);
        m_edgeTraceSpreadSpin->setRange(0.0, 20.0);
        m_edgeTraceSpreadSpin->setSingleStep(0.5);
        m_edgeTraceSpreadSpin->setDecimals(1);
        m_edgeTraceSpreadSpin->setValue(0.0);
        m_edgeTraceSpreadSpin->setSuffix(tr(" px"));
        m_edgeTraceSpreadSpin->setToolTip(tr("Blur edge normals outward — creates a soft falloff around edges"));
        grid->addWidget(m_edgeTraceSpreadSpin, row++, 1);
    }
    layout->addWidget(m_edgeTraceRow);
    m_edgeTraceRow->hide();

    // Auto Normal options
    m_autoNormalRow = new QWidget(content);
    {
        auto* grid = new QGridLayout(m_autoNormalRow);
        grid->setContentsMargins(0, 0, 0, 4);
        grid->setSpacing(3);
        grid->setColumnStretch(1, 1);
        int row = 0;

        grid->addWidget(new QLabel(tr("Source:"), m_autoNormalRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_autoSourceCombo = new QComboBox(m_autoNormalRow);
        m_autoSourceCombo->addItem(tr("Luminance"), 0);
        m_autoSourceCombo->addItem(tr("Red"),       1);
        m_autoSourceCombo->addItem(tr("Green"),     2);
        m_autoSourceCombo->addItem(tr("Blue"),      3);
        m_autoSourceCombo->setToolTip(tr("Channel used for detail and panel edge detection"));
        grid->addWidget(m_autoSourceCombo, row++, 1);

        grid->addWidget(new QLabel(tr("Smooth:"), m_autoNormalRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_autoSmoothSpin = new QDoubleSpinBox(m_autoNormalRow);
        m_autoSmoothSpin->setRange(0.0, 10.0);
        m_autoSmoothSpin->setSingleStep(0.5);
        m_autoSmoothSpin->setDecimals(1);
        m_autoSmoothSpin->setValue(1.0);
        m_autoSmoothSpin->setSuffix(tr(" px"));
        m_autoSmoothSpin->setToolTip(tr("Pre-blur sigma applied to the source channel before Sobel"));
        grid->addWidget(m_autoSmoothSpin, row++, 1);

        grid->addWidget(new QLabel(tr("Pillow:"), m_autoNormalRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_autoPillowStrengthSpin = new QDoubleSpinBox(m_autoNormalRow);
        m_autoPillowStrengthSpin->setRange(0.0, 10.0);
        m_autoPillowStrengthSpin->setSingleStep(0.1);
        m_autoPillowStrengthSpin->setDecimals(2);
        m_autoPillowStrengthSpin->setValue(1.0);
        m_autoPillowStrengthSpin->setToolTip(tr("Strength of the dome/pillow effect (BFS from alpha boundary)"));
        grid->addWidget(m_autoPillowStrengthSpin, row++, 1);

        grid->addWidget(new QLabel(tr("Detail:"), m_autoNormalRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_autoDetailStrengthSpin = new QDoubleSpinBox(m_autoNormalRow);
        m_autoDetailStrengthSpin->setRange(0.0, 50.0);
        m_autoDetailStrengthSpin->setSingleStep(0.5);
        m_autoDetailStrengthSpin->setDecimals(1);
        m_autoDetailStrengthSpin->setValue(3.0);
        m_autoDetailStrengthSpin->setToolTip(tr("Strength of height-field detail (Sobel on source channel)"));
        grid->addWidget(m_autoDetailStrengthSpin, row++, 1);

        grid->addWidget(new QLabel(tr("Panel:"), m_autoNormalRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_autoPanelStrengthSpin = new QDoubleSpinBox(m_autoNormalRow);
        m_autoPanelStrengthSpin->setRange(0.0, 10.0);
        m_autoPanelStrengthSpin->setSingleStep(0.1);
        m_autoPanelStrengthSpin->setDecimals(2);
        m_autoPanelStrengthSpin->setValue(0.5);
        m_autoPanelStrengthSpin->setToolTip(tr("Strength of concave groove effect at detected edges"));
        grid->addWidget(m_autoPanelStrengthSpin, row++, 1);

        grid->addWidget(new QLabel(tr("P.Thresh:"), m_autoNormalRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_autoPanelThresholdSpin = new QDoubleSpinBox(m_autoNormalRow);
        m_autoPanelThresholdSpin->setRange(0.0, 1.0);
        m_autoPanelThresholdSpin->setSingleStep(0.01);
        m_autoPanelThresholdSpin->setDecimals(2);
        m_autoPanelThresholdSpin->setValue(0.2);
        m_autoPanelThresholdSpin->setToolTip(tr("Edge detection threshold for panel grooves (lower = more edges)"));
        grid->addWidget(m_autoPanelThresholdSpin, row++, 1);

        grid->addWidget(new QLabel(tr("P.Width:"), m_autoNormalRow), row, 0, Qt::AlignRight | Qt::AlignVCenter);
        m_autoPanelWidthSpin = new QDoubleSpinBox(m_autoNormalRow);
        m_autoPanelWidthSpin->setRange(0.5, 100.0);
        m_autoPanelWidthSpin->setSingleStep(1.0);
        m_autoPanelWidthSpin->setDecimals(1);
        m_autoPanelWidthSpin->setValue(5.0);
        m_autoPanelWidthSpin->setSuffix(tr(" px"));
        m_autoPanelWidthSpin->setToolTip(tr("Bevel width of panel grooves in pixels"));
        grid->addWidget(m_autoPanelWidthSpin, row++, 1);
    }
    layout->addWidget(m_autoNormalRow);
    m_autoNormalRow->hide();

    // Paint tool options (pencil, eraser, fill, blur)
    m_paintOptionsWidget = new QWidget(content);
    {
        auto* vbox = new QVBoxLayout(m_paintOptionsWidget);
        vbox->setContentsMargins(0, 0, 0, 4);
        vbox->setSpacing(4);

        const int labelW = QFontMetrics(m_paintOptionsWidget->font())
                               .horizontalAdvance(tr("Strength:")) + 8;
        auto makeRowLabel = [&](const QString& text) -> QLabel* {
            auto* lbl = new QLabel(text, m_paintOptionsWidget);
            lbl->setFixedWidth(labelW);
            lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return lbl;
        };

        // Size row (pencil + eraser + blur)
        m_sizeCtrlRow = new QWidget(m_paintOptionsWidget);
        {
            auto* row = new QHBoxLayout(m_sizeCtrlRow);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(4);
            row->addWidget(makeRowLabel(tr("Size:")));
            m_brushSpin = new QSpinBox(m_sizeCtrlRow);
            m_brushSpin->setRange(1, 64);
            m_brushSpin->setValue(4);
            m_brushSpin->setSuffix(tr(" px"));
            row->addWidget(m_brushSpin);
            row->addStretch();
        }
        vbox->addWidget(m_sizeCtrlRow);

        // Shape row (pencil + eraser + blur)
        m_shapeCtrlRow = new QWidget(m_paintOptionsWidget);
        {
            auto* row = new QHBoxLayout(m_shapeCtrlRow);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(3);
            row->addWidget(makeRowLabel(tr("Shape:")));

            auto makeShapeBtn = [&](const QString& tip, bool isCircle) -> QPushButton* {
                QPixmap pix(14, 14);
                pix.fill(Qt::transparent);
                QPainter p(&pix);
                p.setRenderHint(QPainter::Antialiasing, isCircle);
                p.setBrush(m_shapeCtrlRow->palette().color(QPalette::WindowText));
                p.setPen(Qt::NoPen);
                if (isCircle) p.drawEllipse(QRect(0, 0, 14, 14));
                else          p.drawRect(QRect(1, 1, 12, 12));
                p.end();
                auto* btn = new QPushButton(QIcon(pix), QString(), m_shapeCtrlRow);
                btn->setCheckable(true);
                btn->setFixedSize(24, 24);
                btn->setIconSize(QSize(14, 14));
                btn->setToolTip(tip);
                return btn;
            };

            m_brushCircleBtn = makeShapeBtn(tr("Circle brush"), true);
            m_brushCircleBtn->setChecked(true);
            m_brushSquareBtn = makeShapeBtn(tr("Square brush"), false);
            row->addWidget(m_brushCircleBtn);
            row->addWidget(m_brushSquareBtn);
            row->addStretch();
        }
        vbox->addWidget(m_shapeCtrlRow);

        // Hardness row (pencil + eraser + blur)
        m_hardnessCtrlRow = new QWidget(m_paintOptionsWidget);
        {
            auto* row = new QHBoxLayout(m_hardnessCtrlRow);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(4);
            row->addWidget(makeRowLabel(tr("Sharp:")));
            m_hardnessSlider = new QSlider(Qt::Horizontal, m_hardnessCtrlRow);
            m_hardnessSlider->setRange(0, 100);
            m_hardnessSlider->setValue(100);
            m_hardnessSlider->setToolTip(tr("0 = soft edge, 100 = sharp edge"));
            row->addWidget(m_hardnessSlider, 1);
        }
        vbox->addWidget(m_hardnessCtrlRow);

        // Blur strength row (blur only)
        m_blurStrengthRow = new QWidget(m_paintOptionsWidget);
        {
            auto* row = new QHBoxLayout(m_blurStrengthRow);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(4);
            row->addWidget(makeRowLabel(tr("Strength:")));
            m_blurStrengthSlider = new QSlider(Qt::Horizontal, m_blurStrengthRow);
            m_blurStrengthSlider->setRange(0, 100);
            m_blurStrengthSlider->setValue(100);
            m_blurStrengthSlider->setToolTip(tr("Blur strength (0 = no effect, 100 = full)"));
            row->addWidget(m_blurStrengthSlider, 1);
        }
        vbox->addWidget(m_blurStrengthRow);
    }
    layout->addWidget(m_paintOptionsWidget);

    // Tolerance (magic wand)
    m_toleranceRow = new QWidget(content);
    {
        auto* row = new QHBoxLayout(m_toleranceRow);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        row->addWidget(new QLabel(tr("Tolerance:"), m_toleranceRow));
        m_wandToleranceSpin = new QSpinBox(m_toleranceRow);
        m_wandToleranceSpin->setRange(0, 255);
        m_wandToleranceSpin->setValue(30);
        m_wandToleranceSpin->setToolTip(tr("Magic wand tolerance (0–255)"));
        row->addWidget(m_wandToleranceSpin);
        row->addStretch();
    }
    layout->addWidget(m_toleranceRow);
    m_toleranceRow->hide();

    // Feather (all selection tools)
    m_featherRow = new QWidget(content);
    {
        auto* row = new QHBoxLayout(m_featherRow);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        row->addWidget(new QLabel(tr("Feather:"), m_featherRow));
        m_featherSpin = new QSpinBox(m_featherRow);
        m_featherSpin->setRange(0, 50);
        m_featherSpin->setValue(0);
        m_featherSpin->setSuffix(tr(" px"));
        m_featherSpin->setToolTip(tr("Selection edge feather radius (0 = hard edge)"));
        row->addWidget(m_featherSpin);
        row->addStretch();
    }
    layout->addWidget(m_featherRow);
    m_featherRow->hide();

    // Clear selection + bounding rect
    m_clearSelRow = new QWidget(content);
    {
        auto* vbox = new QVBoxLayout(m_clearSelRow);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(3);

        auto* grid = new QGridLayout;
        grid->setSpacing(3);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(3, 1);

        auto makeSpin = [&](const QString& label, int row, int col) -> QSpinBox* {
            grid->addWidget(new QLabel(label, m_clearSelRow), row, col * 2,
                            Qt::AlignRight | Qt::AlignVCenter);
            auto* spin = new QSpinBox(m_clearSelRow);
            spin->setRange(0, 9999);
            spin->setValue(0);
            spin->setEnabled(false);
            grid->addWidget(spin, row, col * 2 + 1);
            return spin;
        };

        m_selXSpin = makeSpin(tr("X:"), 0, 0);
        m_selYSpin = makeSpin(tr("Y:"), 0, 1);
        m_selWSpin = makeSpin(tr("W:"), 1, 0);
        m_selHSpin = makeSpin(tr("H:"), 1, 1);
        vbox->addLayout(grid);

        m_clearSelBtn = new QPushButton(QIcon(QStringLiteral(":/icons/remove.svg")),
                                         tr("Clear Selection"), m_clearSelRow);
        vbox->addWidget(m_clearSelBtn);
    }
    layout->addWidget(m_clearSelRow);
    m_clearSelRow->hide();

    layout->addWidget(makeSeparator(content));

    // ── Layers ────────────────────────────────────────────────────────────────
    layout->addWidget(makeSectionHeader("Layers", content));

    // Layer control buttons
    {
        auto* btnRow = new QHBoxLayout;
        btnRow->setSpacing(3);
        m_addLayerBtn = makeIconBtn(QStringLiteral(":/icons/add-ellipse.svg"),
                                     tr("Add layer"), false, content);
        m_removeLayerBtn = makeIconBtn(QStringLiteral(":/icons/remove.svg"),
                                       tr("Remove layer"), false, content);
        m_moveLayerUpBtn = makeIconBtn(QStringLiteral(":/icons/raise.svg"),
                                       tr("Move layer up"), false, content);
        m_moveLayerDownBtn = makeIconBtn(QStringLiteral(":/icons/lower.svg"),
                                         tr("Move layer down"), false, content);
        btnRow->addWidget(m_addLayerBtn);
        btnRow->addWidget(m_removeLayerBtn);
        btnRow->addWidget(m_moveLayerUpBtn);
        btnRow->addWidget(m_moveLayerDownBtn);
        btnRow->addStretch();
        layout->addLayout(btnRow);
    }

    // Layer list widget
    m_layerList = new QListWidget(content);
    m_layerList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_layerList->setEditTriggers(QAbstractItemView::DoubleClicked);
    m_layerList->setMinimumHeight(80);
    m_layerList->setMaximumHeight(160);
    layout->addWidget(m_layerList);

    layout->addStretch();

    scroll->setWidget(content);
    return scroll;
}

// ─────────────────────────────────────────────────────────────────────────────
// IWorkspace
// ─────────────────────────────────────────────────────────────────────────────

void NormalMapWorkspace::enter()
{
    // Build set of sprite paths that already have a _normal.png on disk
    m_spritesWithNormalMap.clear();
    if (m_session) {
        for (const auto& atlas : m_session->atlases) {
            for (const auto& path : atlas.spritePaths) {
                const QFileInfo fi(path);
                if (QFileInfo::exists(
                        fi.dir().filePath(fi.completeBaseName() + QStringLiteral("_normal.png"))))
                    m_spritesWithNormalMap.insert(path);
            }
        }
        updateAtlasCombo(m_session->atlases, m_session->activeAtlasIndex);
    }

    const QString savedPath = m_currentPath;
    if (m_session)
        m_navigatorPanel->refresh(m_session, false, m_activeAtlasIndex);
    if (m_currentPath.isEmpty() && !savedPath.isEmpty())
        m_currentPath = savedPath;

    if (m_currentPath.isEmpty()) {
        auto* tree = m_navigatorPanel->tree();
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            auto sp = (*it)->data(0, Qt::UserRole).value<SpritePtr>();
            if (sp && !sp->path.isEmpty()) {
                m_currentPath = sp->path;
                tree->setCurrentItem(*it);
                break;
            }
            ++it;
        }
    }

    if (!m_currentPath.isEmpty())
        loadSprite(m_currentPath);
    else
        m_canvas->clearSprite();
}

void NormalMapWorkspace::leave()
{
    saveCurrentSprite();
}

// ─────────────────────────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────────────────────────

void NormalMapWorkspace::loadSprite(const QString& path)
{
    if (!m_session) return;

    m_canvas->setLightZ(m_lightZSpin->value());
    m_canvas->setLightXY(m_lightXSpin->value(), m_lightYSpin->value());
    m_canvas->setLightStrength(m_lightStrSlider->value() / 100.0f);
    m_canvas->setAmbientStrength(m_ambientSlider->value() / 100.0f);
    m_canvas->setLightColor(m_lightColor);
    m_canvas->setViewBlend(m_viewSlider->value() / 100.0f);

    m_canvas->loadSprite(path);  // also loads _normal.png if present; emits layersChanged
}

void NormalMapWorkspace::saveCurrentSprite()
{
    if (m_currentPath.isEmpty() || !m_canvas->isModified()) return;

    const QFileInfo fi(m_currentPath);
    const QString normalPath =
        fi.dir().filePath(fi.completeBaseName() + QStringLiteral("_normal.png"));

    // Save individual layers (syncs active layer; removes meta if single-layer)
    m_canvas->saveLayers(fi.dir(), fi.completeBaseName());

    // Save the flattened composite as the canonical normal map
    m_canvas->flattenedNormalMap().save(normalPath, "PNG");
    m_canvas->setModified(false);

    // Remember this sprite now has a normal map so the navigator badge updates
    m_spritesWithNormalMap.insert(m_currentPath);
}

void NormalMapWorkspace::updateAtlasCombo(const QVector<AtlasEntry>& atlases, int activeSessionIndex)
{
    m_navigatorPanel->updateAtlasCombo(atlases, activeSessionIndex);
    // Sync m_activeAtlasIndex from the combo that was just populated
    if (auto* combo = m_navigatorPanel->atlasCombo())
        m_activeAtlasIndex = combo->currentData().toInt();
    updateAtlasToggle();
}

void NormalMapWorkspace::updateAtlasToggle()
{
    if (!m_atlasNormalMapBtn || !m_session) return;

    const bool validAtlas = (m_activeAtlasIndex >= 0
                             && m_activeAtlasIndex < m_session->atlases.size());
    const bool hasNM = validAtlas && m_session->atlases[m_activeAtlasIndex].hasNormalMap;

    m_atlasNormalMapBtn->setEnabled(validAtlas);
    QSignalBlocker b(m_atlasNormalMapBtn);
    m_atlasNormalMapBtn->setChecked(hasNM);
    if (m_canvas)      m_canvas->setReadOnly(!hasNM);
    if (m_lightBar)    m_lightBar->setVisible(hasNM);
    if (m_rightPanel)  m_rightPanel->setEnabled(hasNM);
}

void NormalMapWorkspace::setExclusiveTool(QPushButton* active)
{
    const QList<QPushButton*> all = {
        m_pencilBtn, m_eraserBtn, m_blurBtn, m_smudgeBtn, m_fillBtn, m_curveBtn, m_blendBtn,
        m_selectRectBtn, m_selectEllipseBtn, m_selectPolyBtn,
        m_selectFreehandBtn, m_selectWandBtn
    };
    for (auto* btn : all) {
        if (btn != active) {
            QSignalBlocker b(btn);
            btn->setChecked(false);
        }
    }
}

void NormalMapWorkspace::updateToolOptions()
{
    const bool isPencil   = m_pencilBtn->isChecked();
    const bool isEraser   = m_eraserBtn->isChecked();
    const bool isBlur     = m_blurBtn->isChecked();
    const bool isSmudge   = m_smudgeBtn->isChecked();
    const bool isFillTool = m_fillBtn->isChecked();
    const bool isCurve    = m_curveBtn->isChecked();
    const bool isBlend    = m_blendBtn->isChecked();
    const bool isWand     = m_selectWandBtn->isChecked();
    const bool isSel      = m_selectRectBtn->isChecked() || m_selectEllipseBtn->isChecked() ||
                            m_selectPolyBtn->isChecked()  || m_selectFreehandBtn->isChecked() ||
                            m_selectWandBtn->isChecked();

    m_canvas->setFillMode(isFillTool);

    using FType  = NormalMapPaintCanvas::FillType;
    using PType  = NormalMapPaintCanvas::PencilType;
    const auto fillType = isFillTool
        ? static_cast<FType>(m_fillTypeCombo->currentData().toInt())
        : FType::Slope;
    const auto pencilType = static_cast<PType>(m_pencilTypeCombo->currentData().toInt());
    const bool isBevel       = (isPencil || isCurve) && (pencilType == PType::BevelRise || pencilType == PType::BevelCarve);
    const bool isSlope       = isFillTool && fillType == FType::Slope;
    const bool isTwoSides    = isFillTool && fillType == FType::TwoSides;
    const bool isPrism       = isFillTool && fillType == FType::Prism;
    const bool isDonut       = isFillTool && fillType == FType::Donut;
    const bool isEmboss      = isFillTool && fillType == FType::Emboss;
    const bool isHeightField = isFillTool && fillType == FType::HeightField;
    const bool isEdgeEmboss  = isFillTool && fillType == FType::EdgeEmboss;
    const bool isEdgeTrace   = isFillTool && fillType == FType::EdgeTrace;
    const bool isAutoNormal  = isFillTool && fillType == FType::AutoNormal;
    // Types that derive direction from content, not drag — no wheel, no fit-to-sel
    const bool isContentBased = isEmboss || isHeightField || isEdgeEmboss || isEdgeTrace || isAutoNormal;

    m_pencilTypeRow->setVisible(isPencil || isCurve);

    // Main wheel: flat pencil/curve and Slope/TwoSides From normal (hidden for bevel — direction is radial)
    const bool showWheel = ((isPencil || isCurve) && !isBevel) || isSlope || isTwoSides;
    m_wheelWidget->setVisible(showWheel);
    m_pickBtn->setVisible(showWheel);
    m_measureBtn->setVisible(showWheel);
    // Secondary "To" wheel: Two Sides only
    m_fillToRow->setVisible(isTwoSides);
    m_fillTypeRow->setVisible(isFillTool);
    m_fillFitSelChk->setVisible(isFillTool && !isContentBased);

    // Per-type extra options
    m_fillInvertChk->setVisible(isFillTool);
    const bool isDome     = isFillTool && fillType == FType::Dome;
    const bool isCone     = isFillTool && fillType == FType::Cone;
    const bool isCylinder = isFillTool && fillType == FType::Cylinder;
    m_prismSizeRow->setVisible(isPrism);
    m_coneEdgeRow->setVisible(false);
    m_donutLengthRow->setVisible(isDonut);
    m_roundnessRow->setVisible(isDome || isCone || isCylinder || isDonut);
    m_embossRow->setVisible(isEmboss);
    m_heightFieldRow->setVisible(isHeightField);
    m_edgeEmbossRow->setVisible(isEdgeEmboss);
    m_edgeTraceRow->setVisible(isEdgeTrace);
    m_autoNormalRow->setVisible(isAutoNormal);

    m_paintOptionsWidget->setVisible(isPencil || isEraser || isBlur || isSmudge || isCurve || isBlend);
    m_sizeCtrlRow->setVisible(isPencil || isEraser || isBlur || isSmudge || isCurve || isBlend);
    m_shapeCtrlRow->setVisible(isPencil || isEraser || isBlur || isSmudge || isCurve || isBlend);
    m_hardnessCtrlRow->setVisible(isPencil || isEraser || isBlur || isSmudge || isCurve || isBlend);
    m_blurStrengthRow->setVisible(isBlur || isSmudge || isBevel);
    m_toleranceRow->setVisible(isWand);
    m_featherRow->setVisible(isSel);
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots
// ─────────────────────────────────────────────────────────────────────────────

void NormalMapWorkspace::onNavigatorSelectionChanged(QTreeWidgetItem* current,
                                                      QTreeWidgetItem* /*previous*/)
{
    saveCurrentSprite();

    auto sprite = current ? current->data(0, Qt::UserRole).value<SpritePtr>() : nullptr;
    if (sprite && !sprite->path.isEmpty()) {
        m_currentPath = sprite->path;
        loadSprite(sprite->path);
    } else {
        m_currentPath.clear();
        m_canvas->clearSprite();
    }
}

void NormalMapWorkspace::onPickLightColor()
{
    const QColor c = QColorDialog::getColor(m_lightColor, this, tr("Select Light Color"));
    if (!c.isValid()) return;
    m_lightColor = c;
    m_canvas->setLightColor(c);
    QPixmap pix(16, 16);
    pix.fill(c);
    m_lightColorBtn->setIcon(QIcon(pix));
}

void NormalMapWorkspace::onCanvasZoomChanged(double zoom)
{
    QSignalBlocker b(m_zoomSpin);
    m_zoomSpin->setValue(zoom * 100.0);
}

void NormalMapWorkspace::onSelectionChanged()
{
    const bool hasSel = m_canvas->hasSelection();
    const QRect r = hasSel ? m_canvas->selectionBoundingRect() : QRect();

    for (auto* s : {m_selXSpin, m_selYSpin, m_selWSpin, m_selHSpin})
        s->setEnabled(hasSel);

    QSignalBlocker bx(m_selXSpin), by(m_selYSpin), bw(m_selWSpin), bh(m_selHSpin);
    m_selXSpin->setValue(r.x());
    m_selYSpin->setValue(r.y());
    m_selWSpin->setValue(r.width());
    m_selHSpin->setValue(r.height());

    m_clearSelRow->setVisible(hasSel);
}

void NormalMapWorkspace::onLayersChanged()
{
    if (!m_layerList || !m_canvas) return;

    QSignalBlocker b(m_layerList);
    m_layerList->clear();

    const int count = m_canvas->layerCount();
    // Display in reverse order: top of stack first (like Photoshop/GIMP)
    for (int i = count - 1; i >= 0; --i) {
        const auto& layer = m_canvas->layer(i);
        auto* item = new QListWidgetItem(layer.name, m_layerList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        item->setCheckState(layer.visible ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, i);
    }

    // Highlight the active layer
    const int active  = m_canvas->activeLayerIndex();
    const int listIdx = (count - 1) - active;
    if (listIdx >= 0 && listIdx < m_layerList->count())
        m_layerList->setCurrentRow(listIdx);

    // Update button states
    if (m_addLayerBtn)      m_addLayerBtn->setEnabled(count > 0);
    if (m_removeLayerBtn)   m_removeLayerBtn->setEnabled(count > 1);
    if (m_moveLayerUpBtn)   m_moveLayerUpBtn->setEnabled(count > 1);
    if (m_moveLayerDownBtn) m_moveLayerDownBtn->setEnabled(count > 1);
}
