// AtlasWorkspace.cpp
// Implements the Atlas (Sprites) workspace widget.

#include "AtlasWorkspace.h"
#include "LayoutCanvas.h"
#include "NavigatorPanel.h"
#include "NavigatorTreeWidget.h"
#include "SpriteEditorPanel.h"
#include "PreviewCanvas.h"
#include "EditorOverlayItem.h"
#include "UndoCommands.h"
#include "AnimationPreviewService.h"
#include "CliToolsConfig.h"
#include "MarkerRepository.h"
#include "MarkersDialog.h"
#include "../../../Project/ProjectSession.h"
#include "../../../Core/models.h"
#include "../../../Core/AppConstants.h"
#include "../../../Core/SpriteTreeUtils.h"
#include "../../../Profiles/SpratProfilesConfig.h"

#include <QIcon>
#include <QVBoxLayout>
#include <QSplitter>
#include <QStackedWidget>
#include <QComboBox>
#include <QPushButton>
#include <QTimer>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QMenu>
#include <QAction>
#include <QImageReader>
#include <QToolButton>
#include <QUndoStack>
#include <QFileInfo>
#include <QInputDialog>

// ---------------------------------------------------------------------------
// File-scope helpers (same as MainWindow.Ui.cpp)
// ---------------------------------------------------------------------------
namespace {

static double toDisplay(int px, int dim, CoordUnit unit, int origin = 0) {
    const int adjusted = px - origin;
    return (unit == CoordUnit::Percent && dim > 0)
        ? adjusted * 100.0 / dim : double(adjusted);
}

static int fromDisplay(double v, int dim, CoordUnit unit, int origin = 0) {
    const int raw = (unit == CoordUnit::Percent && dim > 0)
        ? qRound(v * dim / 100.0) : qRound(v);
    return raw + origin;
}

static void setCoordinateSpinValue(QDoubleSpinBox* spin, int px, int dim, CoordUnit unit, int origin = 0) {
    if (!spin) return;
    spin->blockSignals(true);
    spin->setValue(toDisplay(px, dim, unit, origin));
    spin->blockSignals(false);
}

} // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
AtlasWorkspace::AtlasWorkspace(ProjectSession* session,
                               QUndoStack*     undoStack,
                               AppSettings*    settings,
                               CliPaths*       cliPaths,
                               QWidget*        parent)
    : QWidget(parent)
    , m_session(session)
    , m_undoStack(undoStack)
    , m_settings(settings)
    , m_cliPaths(cliPaths)
{
    m_markerRepo = new MarkerRepository(this);
    connect(m_markerRepo, &MarkerRepository::markerTemplatesChanged,
            this, [this]() { refreshMarkerTemplatesMenu(m_markerRepo->markerTemplates()); });
    setupUi();
}

// ---------------------------------------------------------------------------
// setupUi
// ---------------------------------------------------------------------------
void AtlasWorkspace::setupUi() {
    const int groupMargin       = 4;
    const int groupTopPadding   = 12;
    const int groupBottomMargin = 0;

    // -- Hidden profile combo ------------------------------------------------
    m_profileCombo = new QComboBox(this);
    m_profileCombo->setVisible(false);
    m_profileCombo->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_profileCombo->setToolTip(tr("Layout profile"));
    m_profileCombo->setAccessibleName(tr("Layout profile"));
    connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (!m_profileCombo) return;
        const QString name = m_profileCombo->currentData().toString();
        emit profileChangeRequested(name);
    });

    // -- Hidden add-profiles button ------------------------------------------
    m_addProfilesBtn = new QPushButton(this);
    m_addProfilesBtn->setVisible(false);

    // -- Source resolution combo ---------------------------------------------
    m_sourceResolutionCombo = new QComboBox(this);
    m_sourceResolutionCombo->setToolTip(tr("Target source resolution for layout"));
    m_sourceResolutionCombo->setAccessibleName(tr("Source resolution"));
    m_sourceResolutionCombo->hide();
    m_sourceResolutionCombo->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    m_sourceResolutionDebounceTimer = new QTimer(this);
    m_sourceResolutionDebounceTimer->setSingleShot(true);

    connect(m_sourceResolutionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        const QString res = m_sourceResolutionCombo->currentText();
        emit resolutionChangeRequested(res);
        m_sourceResolutionDebounceTimer->start(AppConstants::kSourceResDebounceMs);
    });

    // -- Canvas content widget (page 0 of view stack) ------------------------
    QWidget* canvasContent = new QWidget(this);
    canvasContent->setStyleSheet("font-weight: normal;");
    QVBoxLayout* canvasLayout = new QVBoxLayout(canvasContent);
    canvasLayout->setContentsMargins(groupMargin, groupTopPadding, groupMargin, groupBottomMargin);

    m_canvas = new LayoutCanvas(canvasContent);
    canvasLayout->addWidget(m_canvas);

    QLabel* atlasDimsLabel = new QLabel(canvasContent);
    atlasDimsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    atlasDimsLabel->setContentsMargins(8, 2, 8, 2);
    atlasDimsLabel->setVisible(false);
    canvasLayout->addWidget(atlasDimsLabel);

    // Canvas signals
    connect(m_canvas, &LayoutCanvas::spriteSelected,
            this, [this](SpritePtr sprite) { emit spriteSelected(sprite); });

    connect(m_canvas, &LayoutCanvas::selectionChanged,
            this, [this](const QList<SpritePtr>& selection) {
        if (m_session) m_session->selectedSprites = selection;
        emit canvasSelectionChanged(selection);
    });

    connect(m_canvas, &LayoutCanvas::zoomChanged,
            this, [this](double zoom) {
        m_layoutZoom = zoom * 100.0;
        emit canvasZoomChanged(zoom * 100.0);
    });

    connect(m_canvas, &LayoutCanvas::splitModeChanged,
            this, [this](bool enabled) {
        emit statusMessage(enabled
            ? tr("Split mode \xe2\x80\x94 click a sprite edge to split it. Press S or right-click to exit.")
            : tr("Idle"));
    });

    // -- Navigator content widget (page 1 of view stack) ---------------------
    QWidget* navigatorContent = new QWidget(this);
    navigatorContent->setStyleSheet("font-weight: normal;");
    QVBoxLayout* navigatorLayout = new QVBoxLayout(navigatorContent);
    navigatorLayout->setContentsMargins(groupMargin, groupTopPadding, groupMargin, 8);

    m_navigatorPanel = new NavigatorPanel(navigatorContent);
    navigatorLayout->addWidget(m_navigatorPanel);

    m_navigatorPanel->setAddSourceButtonVisible(true);

    connect(m_navigatorPanel, &NavigatorPanel::showHiddenChanged,
            this, [this](bool show) {
        m_showHiddenItems = show;
        emit showHiddenToggled(show);
    });

    connect(m_navigatorPanel->tree(), &QWidget::customContextMenuRequested,
            this, &AtlasWorkspace::onSpriteTreeContextMenu);

    // Checkbox cascade + selectedSprites update
    connect(m_navigatorPanel->tree(), &QTreeWidget::itemChanged,
            this, [this](QTreeWidgetItem* item, int) {
        auto* tree = m_navigatorPanel->tree();
        tree->blockSignals(true);

        if (item->childCount() > 0 && item->checkState(0) != Qt::PartiallyChecked) {
            std::function<void(QTreeWidgetItem*, Qt::CheckState)> setDescendants;
            setDescendants = [&](QTreeWidgetItem* node, Qt::CheckState state) {
                for (int i = 0; i < node->childCount(); ++i) {
                    node->child(i)->setCheckState(0, state);
                    setDescendants(node->child(i), state);
                }
            };
            setDescendants(item, item->checkState(0));
        }

        for (QTreeWidgetItem* p = item->parent(); p; p = p->parent()) {
            int checked = 0, total = 0;
            for (int i = 0; i < p->childCount(); ++i) {
                auto s = p->child(i)->checkState(0);
                ++total;
                if (s == Qt::Checked) ++checked;
                else if (s == Qt::PartiallyChecked) { checked = -1; break; }
            }
            if (checked == -1 || (checked > 0 && checked < total))
                p->setCheckState(0, Qt::PartiallyChecked);
            else if (checked == total)
                p->setCheckState(0, Qt::Checked);
            else
                p->setCheckState(0, Qt::Unchecked);
        }

        tree->blockSignals(false);

        if (m_session) {
            const QList<SpritePtr> checkedSprites = SpriteTreeUtils::collectCheckedSprites(tree);
            m_session->selectedSprites = checkedSprites;

            if (m_spriteEditorPanel && m_spriteEditorPanel->multiSelectionLabel()) {
                const int n = checkedSprites.size();
                if (n > 1) {
                    m_spriteEditorPanel->multiSelectionLabel()->setText(
                        m_settings->propagateEditsToChecked
                            ? tr("%1 sprites selected \xe2\x80\x94 pivot and marker changes apply to all").arg(n)
                            : tr("%1 sprites selected \xe2\x80\x94 changes apply to current frame only").arg(n));
                    m_spriteEditorPanel->multiSelectionLabel()->setVisible(true);
                } else {
                    m_spriteEditorPanel->multiSelectionLabel()->setVisible(false);
                }
            }
        }
    });

    connect(m_navigatorPanel->tree(), &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (!current) return;
        QVariant v = current->data(0, Qt::UserRole);
        if (!v.isValid()) return;
        auto sprite = v.value<SpritePtr>();
        if (sprite) emit spriteSelected(sprite);
    });

    // -- Atlas view stack ----------------------------------------------------
    m_atlasViewStack = new QStackedWidget(this);
    m_atlasViewStack->addWidget(canvasContent);    // page 0
    m_atlasViewStack->addWidget(navigatorContent); // page 1
    auto* emptyAtlasWidget = new QWidget(m_atlasViewStack);
    auto* emptyAtlasLayout = new QVBoxLayout(emptyAtlasWidget);
    emptyAtlasLayout->setAlignment(Qt::AlignCenter);
    auto* emptyAtlasIcon = new QLabel(emptyAtlasWidget);
    emptyAtlasIcon->setPixmap(QIcon(":/icons/drag.svg").pixmap(48, 48));
    emptyAtlasIcon->setAlignment(Qt::AlignCenter);
    auto* emptyAtlasLabel = new QLabel(tr("Drag and drop folder, files or URLs"), emptyAtlasWidget);
    emptyAtlasLabel->setAlignment(Qt::AlignCenter);
    emptyAtlasLabel->setStyleSheet("font-size: 14px; color: #888;");
    emptyAtlasLayout->addStretch();
    emptyAtlasLayout->addWidget(emptyAtlasIcon);
    emptyAtlasLayout->addWidget(emptyAtlasLabel);
    emptyAtlasLayout->addStretch();
    m_atlasViewStack->addWidget(emptyAtlasWidget);  // page 2
    m_atlasViewStack->setCurrentIndex(1);

    // -- Sprite editor panel -------------------------------------------------
    m_spriteEditorPanel = new SpriteEditorPanel(this);

    if (m_settings && m_spriteEditorPanel->coordUnitCombo()) {
        m_spriteEditorPanel->coordUnitCombo()->setCurrentIndex(
            m_settings->coordUnit == CoordUnit::Percent ? 1 : 0);
    }

    connect(m_spriteEditorPanel->spriteNameEdit(), &QLineEdit::editingFinished,
            this, &AtlasWorkspace::onSpriteNameEditingFinished);

    connect(m_spriteEditorPanel->editAliasesBtn(), &QPushButton::clicked,
            this, [this]() {
        if (m_session) emit editAliasesRequested(m_session->selectedSprite);
    });

    connect(m_spriteEditorPanel->handleCombo(), QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AtlasWorkspace::onHandleComboChanged);

    connect(m_spriteEditorPanel->pivotXSpin(), QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { onPivotSpinChanged(); });

    connect(m_spriteEditorPanel->pivotYSpin(), QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { onPivotSpinChanged(); });

    connect(m_spriteEditorPanel->coordUnitCombo(), QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AtlasWorkspace::onCoordUnitChanged);

    connect(m_spriteEditorPanel->configPointsBtn(), &QPushButton::clicked,
            this, &AtlasWorkspace::onPointsConfigClicked);

    connect(m_spriteEditorPanel->previewCanvas(), &PreviewCanvas::pivotChanged,
            this, &AtlasWorkspace::onCanvasPivotChanged);

    connect(m_spriteEditorPanel->previewCanvas()->overlay(), &EditorOverlayItem::markerSelected,
            this, &AtlasWorkspace::onMarkerSelectedFromCanvas);

    connect(m_spriteEditorPanel->previewCanvas()->overlay(), &EditorOverlayItem::markerChanged,
            this, &AtlasWorkspace::onMarkerChangedFromCanvas);

    connect(m_spriteEditorPanel->previewCanvas(), &PreviewCanvas::copyMarkersRequested,
            this, &AtlasWorkspace::onCopyMarkersRequested);

    connect(m_spriteEditorPanel->previewCanvas(), &PreviewCanvas::pasteMarkersRequested,
            this, &AtlasWorkspace::onPasteMarkersRequested);

    connect(m_spriteEditorPanel->previewCanvas(), &PreviewCanvas::zoomChanged,
            this, [this](double zoom) {
        auto* spin = m_spriteEditorPanel->previewZoomSpin();
        if (spin) {
            spin->blockSignals(true);
            spin->setValue(zoom * 100.0);
            spin->blockSignals(false);
        }
    });

    connect(m_spriteEditorPanel->previewZoomSpin(), QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &AtlasWorkspace::onPreviewZoomChanged);

    {
        auto* btn = m_spriteEditorPanel->showTrimRectBtn();
        if (m_settings) btn->setChecked(m_settings->showTrimRect);
        connect(btn, &QPushButton::toggled, this, [this](bool checked) {
            if (m_settings) {
                m_settings->showTrimRect = checked;
                if (m_cliPaths) CliToolsConfig::saveAppSettings(*m_settings, *m_cliPaths);
                m_spriteEditorPanel->previewCanvas()->setSettings(*m_settings);
            }
            clearCoordinateFieldOverride();
            syncCoordinateSpinsFromSelection();
            emit showTrimRectToggled(checked);
        });
    }

    connect(m_spriteEditorPanel->onionSkinBtn(), &QPushButton::toggled,
            this, [this](bool) { emit onionSkinToggled(); });

    {
        auto* btn = m_spriteEditorPanel->showGridBtn();
        if (m_settings) btn->setChecked(m_settings->showGrid);
        connect(btn, &QPushButton::toggled, this, [this](bool checked) {
            if (m_settings) {
                m_settings->showGrid = checked;
                if (m_cliPaths) CliToolsConfig::saveAppSettings(*m_settings, *m_cliPaths);
                m_spriteEditorPanel->previewCanvas()->setSettings(*m_settings);
            }
            emit showGridToggled(checked);
        });
    }

    if (auto* btn = m_spriteEditorPanel->markerTemplatesBtn()) {
        auto* menu = new QMenu(btn);
        btn->setMenu(menu);
        menu->addAction(tr("Save current markers as template\xe2\x80\xa6"),
                        this, &AtlasWorkspace::onSaveMarkerTemplate);
    }

    // -- Atlas splitter ------------------------------------------------------
    m_atlasSplitter = new QSplitter(Qt::Horizontal, this);
    m_atlasSplitter->addWidget(m_atlasViewStack);
    m_atlasSplitter->addWidget(m_spriteEditorPanel);
    m_atlasSplitter->setStretchFactor(0, 0);
    m_atlasSplitter->setStretchFactor(1, 1);
    m_atlasSplitter->setSizes({270, 1030});

    // -- Main layout ---------------------------------------------------------
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(m_atlasSplitter);
}

// ---------------------------------------------------------------------------
// IWorkspace
// ---------------------------------------------------------------------------
void AtlasWorkspace::enter() {
    m_atlasViewStack->setCurrentIndex(1);

    if (!m_atlasSplitterHSizes.isEmpty()) {
        m_atlasSplitter->setSizes(m_atlasSplitterHSizes);
    } else {
        m_atlasSplitter->setSizes({270, 1030});
    }

    m_navigatorPanel->setAtlasComboVisible(false);
    m_navigatorPanel->setShowHiddenVisible(true);
    m_navigatorPanel->setCheckboxesEnabled(true);
    m_navigatorPanel->setAddSourceButtonVisible(true);

    if (m_savedPreviewZoom > 0.0) {
        m_spriteEditorPanel->previewCanvas()->setPendingRestore(m_savedPreviewZoom, m_savedPreviewCenter);
    }
}

void AtlasWorkspace::leave() {
    if (m_atlasSplitter->orientation() == Qt::Horizontal) {
        m_atlasSplitterHSizes = m_atlasSplitter->sizes();
    }

    auto* preview = m_spriteEditorPanel->previewCanvas();
    const double z = preview->zoom();
    if (z > 0.0) {
        m_savedPreviewZoom   = z;
        m_savedPreviewCenter = preview->viewportCenterInScene();
    }
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------
void AtlasWorkspace::setSession(ProjectSession* session) {
    m_session = session;
}

void AtlasWorkspace::setProfiles(const QVector<SpratProfile>& profiles, const QString& current) {
    m_profileCombo->blockSignals(true);
    m_profileCombo->clear();
    for (const auto& p : profiles) {
        const QString label = p.label.isEmpty() ? p.name : p.label;
        m_profileCombo->addItem(label, p.name);
    }
    if (!current.isEmpty()) {
        const int idx = m_profileCombo->findData(current);
        if (idx >= 0) m_profileCombo->setCurrentIndex(idx);
    }
    m_profileCombo->blockSignals(false);
}

void AtlasWorkspace::setResolutionOptions(const QStringList& options, const QString& current) {
    m_sourceResolutionCombo->blockSignals(true);
    m_sourceResolutionCombo->clear();
    m_sourceResolutionCombo->addItems(options);
    if (!current.isEmpty()) {
        const int idx = m_sourceResolutionCombo->findText(current);
        if (idx >= 0) m_sourceResolutionCombo->setCurrentIndex(idx);
    }
    m_sourceResolutionCombo->blockSignals(false);
}

void AtlasWorkspace::selectSprite(SpritePtr sprite) {
    Q_UNUSED(sprite);
    syncPivotSpinsFromSprite();
}

void AtlasWorkspace::refreshNavigator() {
    if (m_navigatorPanel && m_session) {
        m_navigatorPanel->refresh(m_session, m_showHiddenItems, -1);
    }
}

void AtlasWorkspace::refreshHandleCombo() {
    auto* combo = m_spriteEditorPanel->handleCombo();
    if (!combo || !m_session || !m_session->selectedSprite) return;

    combo->blockSignals(true);
    combo->clear();
    combo->addItem(tr("Pivot"));

    for (const auto& point : m_session->selectedSprite->points) {
        combo->addItem(point.name);
    }

    if (!m_session->selectedPointName.isEmpty()) {
        const int idx = combo->findText(m_session->selectedPointName);
        if (idx >= 0)
            combo->setCurrentIndex(idx);
        else {
            combo->setCurrentIndex(0);
            m_session->selectedPointName.clear();
        }
    } else {
        combo->setCurrentIndex(0);
    }

    combo->blockSignals(false);
}

void AtlasWorkspace::refreshMarkerDisplay() {
    m_spriteEditorPanel->previewCanvas()->overlay()->updateLayout();
    refreshHandleCombo();
}

void AtlasWorkspace::updateUiState(bool hasProject, bool isLoading, bool cliReady) {
    const bool canEdit = hasProject && !isLoading && cliReady;
    if (m_addProfilesBtn) m_addProfilesBtn->setEnabled(canEdit);
    if (m_sourceResolutionCombo) m_sourceResolutionCombo->setEnabled(canEdit);
}

void AtlasWorkspace::setCanvasZoom(double percent) {
    if (m_canvas) m_canvas->setZoom(percent / 100.0);
}

void AtlasWorkspace::applyMarkers(SpritePtr sprite, const QVector<NamedPoint>& points) {
    if (!sprite || !m_undoStack) return;
    const QVector<NamedPoint> old = sprite->points;
    sprite->points = points;
    m_undoStack->push(new SetMarkersCommand(
        sprite, old, points,
        [this]() { refreshMarkerDisplay(); }
    ));
    refreshMarkerDisplay();
}

void AtlasWorkspace::refreshMarkerTemplatesMenu(const QVector<MarkerTemplate>& templates) {
    auto* btn = m_spriteEditorPanel->markerTemplatesBtn();
    if (!btn) return;
    auto* menu = btn->menu();
    if (!menu) { menu = new QMenu(btn); btn->setMenu(menu); }
    menu->clear();
    menu->addAction(tr("Save current markers as template\xe2\x80\xa6"),
                    this, &AtlasWorkspace::onSaveMarkerTemplate);
    if (!templates.isEmpty()) {
        menu->addSeparator();
        for (const auto& t : templates) {
            menu->addAction(tr("Apply: %1").arg(t.name), this,
                [this, t]() { onApplyMarkerTemplate(t); });
        }
        menu->addSeparator();
        auto* del = menu->addMenu(tr("Delete template"));
        for (const auto& t : templates) {
            del->addAction(t.name, this,
                [this, n = t.name]() { onDeleteMarkerTemplate(n); });
        }
    }
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------
void AtlasWorkspace::onPreviewZoomChanged(double value) {
    auto* preview = m_spriteEditorPanel->previewCanvas();
    if (!preview) return;
    auto* spin = m_spriteEditorPanel->previewZoomSpin();
    if (spin && !spin->signalsBlocked()) {
        preview->setZoomManual(true);
    }
    preview->setZoom(value / 100.0);
}

void AtlasWorkspace::onPivotSpinChanged() {
    if (!m_session || !m_session->selectedSprite) return;

    const auto unit = m_settings->coordUnit;
    const QSize activeSize = spriteCoordinateSpaceSize(m_session->selectedSprite);
    int sw = activeSize.width();
    int sh = activeSize.height();
    int ox = 0, oy = 0;
    auto* preview = m_spriteEditorPanel->previewCanvas();
    if (m_settings->showTrimRect && preview) {
        const QRect tr = preview->cachedTrimRect();
        if (tr.isValid()) {
            ox = tr.left();
            oy = tr.top();
            sw = tr.width();
            sh = tr.height();
        }
    }

    auto* xSpin = m_spriteEditorPanel->pivotXSpin();
    auto* ySpin = m_spriteEditorPanel->pivotYSpin();
    const int newX = fromDisplay(xSpin->value(), sw, unit, ox);
    const int newY = fromDisplay(ySpin->value(), sh, unit, oy);

    if (m_session->selectedPointName.isEmpty()) {
        const int oldX = m_session->selectedSprite->pivotX;
        const int oldY = m_session->selectedSprite->pivotY;
        const bool activeChanged = oldX != newX || oldY != newY;
        QVector<SetPivotCommand::CoTarget> coTargets;
        if (m_settings->propagateEditsToChecked) {
            for (const auto& sprite : m_session->selectedSprites) {
                if (sprite && sprite != m_session->selectedSprite) {
                    const QPair<int, int> oldPos{sprite->pivotX, sprite->pivotY};
                    const QSize coSize = spriteCoordinateSpaceSize(sprite);
                    const int csw = (m_settings->showTrimRect && sw > 0) ? sw : coSize.width();
                    const int csh = (m_settings->showTrimRect && sh > 0) ? sh : coSize.height();
                    sprite->pivotX = fromDisplay(xSpin->value(), csw, unit, ox);
                    sprite->pivotY = fromDisplay(ySpin->value(), csh, unit, oy);
                    const QPair<int, int> newPos{sprite->pivotX, sprite->pivotY};
                    if (oldPos != newPos) {
                        coTargets.append({sprite, oldPos, newPos});
                    }
                }
            }
        }
        if (!activeChanged && coTargets.isEmpty()) return;
        storeCoordinateFieldOverride();
        AnimationPreviewService::invalidateBounds();
        m_undoStack->push(new SetPivotCommand(
            m_session->selectedSprite, oldX, oldY, newX, newY,
            /*alreadyApplied=*/false, std::move(coTargets)));
        if (preview && preview->overlay()) preview->overlay()->updateLayout();
        emit spriteDataChanged();
    } else {
        auto& sprite = m_session->selectedSprite;
        const QVector<NamedPoint> oldPoints = sprite->points;
        bool found = false;
        bool activeChanged = false;
        for (auto& p : sprite->points) {
            if (p.name != m_session->selectedPointName) continue;
            if (p.x != newX || p.y != newY) {
                if (p.kind == MarkerKind::Polygon && !p.polygonPoints.isEmpty()) {
                    const QPoint delta(newX - p.polygonPoints[0].x(), newY - p.polygonPoints[0].y());
                    for (auto& pt : p.polygonPoints) pt += delta;
                }
                p.x = newX;
                p.y = newY;
                activeChanged = true;
            }
            found = true;
            break;
        }
        if (!found) return;
        QVector<SetMarkersCommand::CoTarget> coTargets;
        if (m_settings->propagateEditsToChecked) {
            for (const auto& coSprite : m_session->selectedSprites) {
                if (!coSprite || coSprite == sprite) continue;
                const QVector<NamedPoint> oldCoPoints = coSprite->points;
                const QSize coSize = spriteCoordinateSpaceSize(coSprite);
                const int csw = (m_settings->showTrimRect && sw > 0) ? sw : coSize.width();
                const int csh = (m_settings->showTrimRect && sh > 0) ? sh : coSize.height();
                const int cx = fromDisplay(xSpin->value(), csw, unit, ox);
                const int cy = fromDisplay(ySpin->value(), csh, unit, oy);
                for (auto& p : coSprite->points) {
                    if (p.name != m_session->selectedPointName) continue;
                    if (p.kind == MarkerKind::Polygon && !p.polygonPoints.isEmpty()) {
                        const QPoint delta(cx - p.polygonPoints[0].x(), cy - p.polygonPoints[0].y());
                        for (auto& pt : p.polygonPoints) pt += delta;
                    }
                    p.x = cx;
                    p.y = cy;
                    break;
                }
                if (coSprite->points != oldCoPoints) {
                    coTargets.append({coSprite, oldCoPoints, coSprite->points});
                }
            }
        }
        if (!activeChanged && coTargets.isEmpty()) return;
        storeCoordinateFieldOverride();
        m_undoStack->push(new SetMarkersCommand(
            sprite, oldPoints, sprite->points,
            [this]() { refreshMarkerDisplay(); },
            std::move(coTargets)
        ));
        if (preview && preview->overlay()) preview->overlay()->updateLayout();
        emit spriteDataChanged();
    }
}

void AtlasWorkspace::onCanvasPivotChanged(int x, int y) {
    Q_UNUSED(x);
    Q_UNUSED(y);
    clearCoordinateFieldOverride();
    syncCoordinateSpinsFromSelection();
}

void AtlasWorkspace::onHandleComboChanged(int index) {
    clearCoordinateFieldOverride();
    auto* combo = m_spriteEditorPanel->handleCombo();
    auto* preview = m_spriteEditorPanel->previewCanvas();

    if (index <= 0) {
        if (m_session) m_session->selectedPointName.clear();
        if (preview && preview->overlay())
            preview->overlay()->setSelectedMarker(QString());
        if (m_session && m_session->selectedSprite)
            syncCoordinateSpinsFromSelection();
    } else {
        if (m_session && combo) {
            m_session->selectedPointName = combo->itemText(index);
            if (preview && preview->overlay())
                preview->overlay()->setSelectedMarker(m_session->selectedPointName);
            syncCoordinateSpinsFromSelection();
        }
    }

    const QString pointName = (m_session && index > 0 && combo)
        ? combo->itemText(index) : QString();
    emit selectedMarkerChanged(pointName);

    const QString msg = (m_session && !m_session->selectedPointName.isEmpty())
        ? tr("Selected Marker: ") + m_session->selectedPointName
        : tr("Selected: ") + (m_session && m_session->selectedSprite
              ? m_session->selectedSprite->name : tr("none"));
    emit statusMessage(msg);
}

void AtlasWorkspace::onCoordUnitChanged() {
    auto* combo = m_spriteEditorPanel->coordUnitCombo();
    if (!combo || !m_settings) return;

    clearCoordinateFieldOverride();

    const CoordUnit newUnit = combo->currentIndex() == 1
        ? CoordUnit::Percent : CoordUnit::Pixels;
    if (m_settings->coordUnit == newUnit) {
        syncCoordinateSpinsFromSelection();
        return;
    }

    m_settings->coordUnit = newUnit;
    if (m_cliPaths) CliToolsConfig::saveAppSettings(*m_settings, *m_cliPaths);
    syncCoordinateSpinsFromSelection();
}

void AtlasWorkspace::onLayoutZoomChanged(double value) {
    m_layoutZoom = value;
    if (m_canvas) m_canvas->setZoom(value / 100.0);
    emit canvasZoomChanged(value);
}

void AtlasWorkspace::onMarkerSelectedFromCanvas(const QString& name) {
    clearCoordinateFieldOverride();
    if (m_session) m_session->selectedPointName = name;

    auto* combo = m_spriteEditorPanel->handleCombo();
    if (combo) {
        combo->blockSignals(true);
        if (!name.isEmpty()) {
            const int idx = combo->findText(name);
            if (idx != -1) combo->setCurrentIndex(idx);
        } else {
            combo->setCurrentIndex(0);
        }
        combo->blockSignals(false);
    }

    if (!name.isEmpty()) {
        emit statusMessage(tr("Selected Marker: ") + name);
        syncCoordinateSpinsFromSelection();
    } else {
        const QString spriteName = (m_session && m_session->selectedSprite)
            ? m_session->selectedSprite->name : tr("none");
        emit statusMessage(tr("Selected: ") + spriteName);
        if (m_session && m_session->selectedSprite)
            syncCoordinateSpinsFromSelection();
    }

    emit selectedMarkerChanged(name);
}

void AtlasWorkspace::onMarkerChangedFromCanvas() {
    auto* preview = m_spriteEditorPanel->previewCanvas();
    if (preview && preview->overlay()) preview->overlay()->update();
    if (!m_session || !m_session->selectedSprite) return;
    clearCoordinateFieldOverride();
    syncCoordinateSpinsFromSelection();
}

void AtlasWorkspace::onSpriteNameEditingFinished() {
    if (!m_session || !m_session->selectedSprite) return;
    auto* edit = m_spriteEditorPanel->spriteNameEdit();
    if (!edit) return;

    const QString newName = edit->text().trimmed();
    const QString oldName = m_session->selectedSprite->name;

    if (newName.isEmpty()) {
        edit->blockSignals(true);
        edit->setText(oldName);
        edit->blockSignals(false);
        return;
    }
    if (newName == oldName) return;

    const QStringList aliases = m_session->selectedSprite->aliases;
    m_session->selectedSprite->name = newName;
    emit statusMessage(tr("Selected: ") + newName);

    SpritePtr sprite = m_session->selectedSprite;
    m_undoStack->push(new SetSpriteNamesCommand(
        sprite,
        oldName, aliases,
        newName, aliases,
        [this, sprite, edit]() {
            if (m_session && m_session->selectedSprite == sprite) {
                edit->blockSignals(true);
                edit->setText(sprite->name);
                edit->blockSignals(false);
                emit statusMessage(tr("Selected: ") + sprite->name);
            }
        }
    ));
}

void AtlasWorkspace::filterSpriteTree(const QString& text) {
    if (m_navigatorPanel) m_navigatorPanel->applyFilter(text);
}

void AtlasWorkspace::onSpriteTreeContextMenu(const QPoint& pos) {
    if (!m_session) return;

    auto* tree = m_navigatorPanel->tree();
    QTreeWidgetItem* clickedItem = tree->itemAt(pos);

    const QStringList clickedPaths = clickedItem
        ? SpriteTreeUtils::collectDescendantPaths(clickedItem) : QStringList();
    const QStringList checkedPaths = SpriteTreeUtils::collectCheckedPaths(tree);

    const bool clickedIsLeaf  = clickedItem && clickedItem->childCount() == 0
                                && clickedItem->data(0, Qt::UserRole).isValid();
    const bool clickedIsGroup = clickedItem && clickedItem->childCount() > 0;
    const bool clickedIsSourceNode = clickedItem
                                     && clickedItem->data(0, Qt::UserRole + 1).isValid()
                                     && !clickedItem->data(0, Qt::UserRole).isValid();
    const bool hasChecked = !checkedPaths.isEmpty();
    const bool hasTimeline = m_session->selectedTimelineIndex >= 0
                             && m_session->selectedTimelineIndex < m_session->activeAtlas().timelines.size();

    const int clickedItemType = clickedItem
        ? clickedItem->data(0, Qt::UserRole + 2).toInt() : 0;

    // Special item types: hidden-folder (1), excluded item (2), excluded-section header (3)
    if (clickedItemType > 0) {
        QMenu menu(this);
        if (clickedItemType == 1) {
            menu.addAction(tr("Unhide \"%1\"").arg(clickedItem->text(0)));
        } else if (clickedItemType == 2) {
            const QString relPath = clickedItem->data(0, Qt::UserRole + 4).toString();
            menu.addAction(tr("Re-include \"%1\"").arg(QFileInfo(relPath).fileName()));
        } else if (clickedItemType == 3) {
            menu.addAction(tr("Re-include all"));
        }
        menu.exec(tree->viewport()->mapToGlobal(pos));
        return;
    }

    QMenu menu(this);
    bool hadItems = false;
    auto addSep = [&]() { if (hadItems) { menu.addSeparator(); hadItems = false; } };

    // Section 1: exclude / delete
    QAction* deleteFrameAction    = nullptr;
    QAction* deleteGroupAction    = nullptr;
    QAction* deleteSelectedAction = nullptr;

    if (clickedIsLeaf) {
        deleteFrameAction = menu.addAction(QIcon(":/icons/remove.svg"), tr("Exclude"));
        hadItems = true;
    }
    if (clickedIsGroup && !clickedIsSourceNode) {
        deleteGroupAction = menu.addAction(QIcon(":/icons/remove.svg"), tr("Exclude group"));
        hadItems = true;
    }
    if (hasChecked) {
        deleteSelectedAction = menu.addAction(QIcon(":/icons/remove.svg"), tr("Exclude selected"));
        hadItems = true;
    }

    // Section 2: add frames into group
    QAction* addFramesAction = nullptr;
    if (clickedIsGroup) {
        addSep();
        addFramesAction = menu.addAction(
            QIcon(":/icons/add-ellipse.svg"), tr("Add frames into '%1'...").arg(clickedItem->text(0)));
        hadItems = true;
    }

    // Section 3: timelines
    QAction* createTimelineFromGroupAction    = nullptr;
    QAction* createTimelineFromSelectedAction = nullptr;
    QAction* addToTimelineAction              = nullptr;

    if (clickedIsGroup || hasChecked) {
        addSep();
        if (clickedIsGroup && !clickedIsSourceNode) {
            createTimelineFromGroupAction = menu.addAction(QIcon(":/icons/bot-add.svg"), tr("Create timeline from group"));
            hadItems = true;
        }
        if (hasChecked) {
            createTimelineFromSelectedAction = menu.addAction(QIcon(":/icons/bot-add.svg"), tr("Create timeline from selected frames"));
            hadItems = true;
        }
        if (hasChecked && hasTimeline) {
            addToTimelineAction = menu.addAction(QIcon(":/icons/add-ellipse.svg"), tr("Add selected to current timeline"));
            hadItems = true;
        }
    }

    // Source node actions
    QAction* autoCreateTimelinesAction = nullptr;
    if (clickedIsSourceNode) {
        addSep();
        autoCreateTimelinesAction = menu.addAction(QIcon(":/icons/bot-add.svg"), tr("Auto-create timelines"));
        hadItems = true;
    }

    if (!hadItems) return;

    QAction* chosen = menu.exec(tree->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if      (chosen == deleteFrameAction)                emit deleteFramesRequested(clickedPaths);
    else if (chosen == deleteGroupAction && clickedItem) emit deleteGroupRequested(clickedItem);
    else if (chosen == deleteSelectedAction)             emit deleteFramesRequested(checkedPaths);
    else if (chosen == addFramesAction) {
        // Subfolder hint: group item text relative to source root
        const QString folder = clickedItem ? clickedItem->text(0) : QString();
        emit addFramesToFolderRequested(folder);
    }
    else if (chosen == createTimelineFromGroupAction)    emit createTimelineRequested(clickedPaths, clickedItem);
    else if (chosen == createTimelineFromSelectedAction) emit createTimelineRequested(checkedPaths, nullptr);
    else if (chosen == addToTimelineAction)              emit addToTimelineRequested(checkedPaths);
    else if (chosen == autoCreateTimelinesAction && clickedItem) {
        const int sourceIdx = clickedItem->data(0, Qt::UserRole + 1).toInt();
        emit autoCreateTimelinesForSourceRequested(sourceIdx);
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------
QSize AtlasWorkspace::spriteCoordinateSpaceSize(const SpritePtr& sprite) const {
    if (!sprite) return {};

    const QSize sourceSize = QImageReader(sprite->path).size();
    if (sourceSize.isValid() && sourceSize.width() > 0 && sourceSize.height() > 0) {
        return sourceSize;
    }

    const int contentWidth  = sprite->rotated ? sprite->rect.height() : sprite->rect.width();
    const int contentHeight = sprite->rotated ? sprite->rect.width()  : sprite->rect.height();

    if (sprite->trimmed) {
        const int fullWidth  = sprite->trimRect.x() + contentWidth  + sprite->trimRect.width();
        const int fullHeight = sprite->trimRect.y() + contentHeight + sprite->trimRect.height();
        if (fullWidth > 0 && fullHeight > 0) return QSize(fullWidth, fullHeight);
    }

    if (contentWidth > 0 && contentHeight > 0)
        return QSize(contentWidth, contentHeight);

    return sprite->rect.size();
}

void AtlasWorkspace::clearCoordinateFieldOverride() {
    m_coordinateFieldOverride = {};
}

void AtlasWorkspace::storeCoordinateFieldOverride() {
    auto* xSpin = m_spriteEditorPanel->pivotXSpin();
    auto* ySpin = m_spriteEditorPanel->pivotYSpin();
    if (!m_session || !m_session->selectedSprite || !xSpin || !ySpin) {
        clearCoordinateFieldOverride();
        return;
    }
    m_coordinateFieldOverride.active       = true;
    m_coordinateFieldOverride.sprite       = m_session->selectedSprite.get();
    m_coordinateFieldOverride.markerName   = m_session->selectedPointName;
    m_coordinateFieldOverride.unit         = m_settings->coordUnit;
    m_coordinateFieldOverride.showTrimRect = m_settings->showTrimRect;
    m_coordinateFieldOverride.x            = xSpin->value();
    m_coordinateFieldOverride.y            = ySpin->value();
}

bool AtlasWorkspace::coordinateFieldOverrideApplies() const {
    return m_coordinateFieldOverride.active
        && m_session
        && m_session->selectedSprite
        && m_coordinateFieldOverride.sprite       == m_session->selectedSprite.get()
        && m_coordinateFieldOverride.markerName   == m_session->selectedPointName
        && m_coordinateFieldOverride.unit         == m_settings->coordUnit
        && m_coordinateFieldOverride.showTrimRect == m_settings->showTrimRect;
}

void AtlasWorkspace::syncPivotSpinsFromSprite() {
    syncCoordinateSpinsFromSelection();
    auto* preview = m_spriteEditorPanel->previewCanvas();
    if (preview && preview->overlay())
        preview->overlay()->updateLayout();
}

void AtlasWorkspace::refreshSpriteEditor() {
    clearCoordinateFieldOverride();
    syncPivotSpinsFromSprite();
}

void AtlasWorkspace::clearCoordinateOverride() {
    clearCoordinateFieldOverride();
}

void AtlasWorkspace::syncCoordinateSpinsFromSelection() {
    auto* coordUnitCombo = m_spriteEditorPanel->coordUnitCombo();
    auto* xSpin          = m_spriteEditorPanel->pivotXSpin();
    auto* ySpin          = m_spriteEditorPanel->pivotYSpin();

    if (!m_session || !m_session->selectedSprite) {
        if (coordUnitCombo) coordUnitCombo->setEnabled(false);
        return;
    }

    const QSize spriteSize  = spriteCoordinateSpaceSize(m_session->selectedSprite);
    int spriteWidth  = spriteSize.width();
    int spriteHeight = spriteSize.height();
    int originX = 0, originY = 0;

    auto* preview = m_spriteEditorPanel->previewCanvas();
    if (m_settings->showTrimRect && preview) {
        const QRect tr = preview->cachedTrimRect();
        if (tr.isValid()) {
            originX      = tr.left();
            originY      = tr.top();
            spriteWidth  = tr.width();
            spriteHeight = tr.height();
        }
    }

    const bool hasDimensions = spriteWidth > 0 && spriteHeight > 0;
    const CoordUnit displayUnit = hasDimensions ? m_settings->coordUnit : CoordUnit::Pixels;

    if (coordUnitCombo) coordUnitCombo->setEnabled(hasDimensions);

    if (xSpin) {
        xSpin->blockSignals(true);
        xSpin->setDecimals(displayUnit == CoordUnit::Percent ? 1 : 0);
        xSpin->blockSignals(false);
    }
    if (ySpin) {
        ySpin->blockSignals(true);
        ySpin->setDecimals(displayUnit == CoordUnit::Percent ? 1 : 0);
        ySpin->blockSignals(false);
    }

    if (coordinateFieldOverrideApplies()) {
        if (xSpin) { xSpin->blockSignals(true); xSpin->setValue(m_coordinateFieldOverride.x); xSpin->blockSignals(false); }
        if (ySpin) { ySpin->blockSignals(true); ySpin->setValue(m_coordinateFieldOverride.y); ySpin->blockSignals(false); }
        return;
    }

    if (m_session->selectedPointName.isEmpty()) {
        setCoordinateSpinValue(xSpin, m_session->selectedSprite->pivotX, spriteWidth,  displayUnit, originX);
        setCoordinateSpinValue(ySpin, m_session->selectedSprite->pivotY, spriteHeight, displayUnit, originY);
        return;
    }

    for (const auto& point : m_session->selectedSprite->points) {
        if (point.name != m_session->selectedPointName) continue;
        setCoordinateSpinValue(xSpin, point.x, spriteWidth,  displayUnit, originX);
        setCoordinateSpinValue(ySpin, point.y, spriteHeight, displayUnit, originY);
        return;
    }

    // Fallback to pivot
    setCoordinateSpinValue(xSpin, m_session->selectedSprite->pivotX, spriteWidth,  displayUnit, originX);
    setCoordinateSpinValue(ySpin, m_session->selectedSprite->pivotY, spriteHeight, displayUnit, originY);
}

// ---------------------------------------------------------------------------
// Marker clipboard / templates (moved from MainWindow.Marker.cpp)
// ---------------------------------------------------------------------------

void AtlasWorkspace::applyMarkersToSelection(const QVector<NamedPoint>& points) {
    if (!m_session || !m_session->selectedSprite) return;
    const QVector<NamedPoint> old = m_session->selectedSprite->points;
    m_session->selectedSprite->points = points;
    QVector<SetMarkersCommand::CoTarget> coTargets;
    auto addCoTarget = [&](const SpritePtr& s) {
        const QVector<NamedPoint> prev = s->points;
        s->points = points;
        coTargets.append({s, prev, s->points});
    };
    if (m_settings && m_settings->propagateEditsToChecked)
        for (const auto& s : m_session->selectedSprites)
            if (s && s != m_session->selectedSprite) addCoTarget(s);
    m_undoStack->push(new SetMarkersCommand(
        m_session->selectedSprite, old, points,
        [this]() { refreshMarkerDisplay(); },
        std::move(coTargets)));
    refreshMarkerDisplay();
}

void AtlasWorkspace::onCopyMarkersRequested() {
    if (!m_session || !m_session->selectedSprite) return;
    m_markerRepo->setMarkerClipboard(m_session->selectedSprite->points);
}

void AtlasWorkspace::onPasteMarkersRequested() {
    if (!m_session || !m_session->selectedSprite || m_markerRepo->markerClipboard().isEmpty()) return;
    applyMarkersToSelection(m_markerRepo->markerClipboard());
}

void AtlasWorkspace::onSaveMarkerTemplate() {
    if (!m_session || !m_session->selectedSprite) return;
    bool ok;
    const QString name = QInputDialog::getText(this, tr("Save Marker Template"),
        tr("Template name:"), QLineEdit::Normal, {}, &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    MarkerTemplate tmpl{name.trimmed(), m_session->selectedSprite->points};
    m_markerRepo->addMarkerTemplate(tmpl);
}

void AtlasWorkspace::onApplyMarkerTemplate(const MarkerTemplate& tmpl) {
    if (!m_session || !m_session->selectedSprite) return;
    applyMarkersToSelection(tmpl.points);
}

void AtlasWorkspace::onDeleteMarkerTemplate(const QString& name) {
    m_markerRepo->removeMarkerTemplate(name);
}

void AtlasWorkspace::onPointsConfigClicked() {
    if (!m_session || !m_session->selectedSprite) return;
    clearCoordinateFieldOverride();

    SuggestedMarkerPosition suggestion;
    auto* previewView = m_spriteEditorPanel->previewCanvas();
    if (previewView && previewView->scene()) {
        QRect viewportRect = previewView->viewport()->rect();
        QRectF visibleRectInScene = previewView->mapToScene(viewportRect).boundingRect();

        QSize imgSize = QImageReader(m_session->selectedSprite->path).size();
        if (!imgSize.isValid())
            imgSize = m_session->selectedSprite->rect.size();
        QRectF imageRect(0, 0, imgSize.width(), imgSize.height());
        QRectF visiblePart = imageRect.intersected(visibleRectInScene);

        double zoom = previewView->transform().m11();
        if (zoom <= 1e-9) zoom = 1.0;
        double targetSceneSize = 80.0 / zoom;

        if (!visiblePart.isEmpty()) {
            suggestion.pos = visiblePart.center().toPoint();
            double maxSafeSize = qMin(visiblePart.width(), visiblePart.height()) * 0.5;
            suggestion.baseSize = qMax(8, qRound(qMin(targetSceneSize,
                maxSafeSize > 16 ? maxSafeSize : targetSceneSize)));
        } else {
            suggestion.pos = imageRect.center().toPoint();
            suggestion.baseSize = qMax(8, qRound(targetSceneSize));
        }
    } else {
        suggestion.pos = QPoint(m_session->selectedSprite->rect.width() / 2,
                                m_session->selectedSprite->rect.height() / 2);
        suggestion.baseSize = 20;
    }

    const QVector<NamedPoint> oldPoints = m_session->selectedSprite->points;

    MarkersDialog dlg(m_session->selectedSprite, suggestion, this);
    connect(&dlg, &MarkersDialog::markersChanged, this, [this]() { refreshMarkerDisplay(); });
    dlg.exec();

    const QVector<NamedPoint> newPoints = m_session->selectedSprite->points;
    if (newPoints != oldPoints) {
        QVector<SetMarkersCommand::CoTarget> coTargets;
        if (m_settings && m_settings->propagateEditsToChecked) {
            for (const auto& sprite : m_session->selectedSprites) {
                if (sprite && sprite != m_session->selectedSprite) {
                    const QVector<NamedPoint> oldCoPoints = sprite->points;
                    sprite->points = newPoints;
                    coTargets.append({sprite, oldCoPoints, sprite->points});
                }
            }
        }
        m_undoStack->push(new SetMarkersCommand(
            m_session->selectedSprite, oldPoints, newPoints,
            [this]() { refreshMarkerDisplay(); },
            std::move(coTargets)));
    }
}
