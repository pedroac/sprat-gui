#include "MainWindow.h"
#include "PackedAtlasView.h"
#include "AtlasesManagementWorkspace.h"
#include "UndoCommands.h"
#include "MainWindowUiState.h"
#include "ResolutionsConfig.h"
#include "ResolutionUtils.h"
#include "AnimationCanvas.h"
#include "CliToolsConfig.h"
#include "AppConstants.h"
#include <QDockWidget>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QScreen>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QFrame>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QFontDatabase>
#include <algorithm>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QActionGroup>
#include <QMessageBox>
#include "NavigatorTreeWidget.h"
#include "SpriteTreeUtils.h"
#include <QUuid>

static double toDisplay(int px, int dim, CoordUnit unit, int origin = 0) {
    const int adjusted = px - origin;
    return (unit == CoordUnit::Percent && dim > 0)
        ? adjusted * 100.0 / dim : double(adjusted);
}

namespace {
void setCoordinateSpinValue(QDoubleSpinBox* spin, int px, int dim, CoordUnit unit, int origin = 0) {
    if (!spin) return;
    spin->blockSignals(true);
    spin->setValue(toDisplay(px, dim, unit, origin));
    spin->blockSignals(false);
}
}

void MainWindow::setupUi() {
    resize(1400, 860);
    setWindowTitle(tr("Sprat GUI %1[*]").arg(SPRAT_GUI_VERSION));
    setupToolbar();

    // Add a 10px gap between the menu bar and the dock widgets
    QToolBar* topSpacer = new QToolBar(this);
    topSpacer->setObjectName("topSpacer");
    topSpacer->setFixedHeight(10);
    topSpacer->setMovable(false);
    topSpacer->setFloatable(false);
    topSpacer->setAllowedAreas(Qt::TopToolBarArea);
    topSpacer->setStyleSheet("background: transparent; border: none;");
    topSpacer->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    addToolBar(Qt::TopToolBarArea, topSpacer);

    // Prepare fonts for dock titles
    QFont boldFont = this->font();
    boldFont.setBold(true);
    QFont normalFont = this->font();

    // Set dock options
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);

    // Central Widget is a Stack
    m_mainStack = new QStackedWidget(this);
    setCentralWidget(m_mainStack);

    // Page 1: Welcome
    m_welcomePage = new QWidget(this);
    QVBoxLayout* welcomeLayout = new QVBoxLayout(m_welcomePage);
    welcomeLayout->addStretch();

    QHBoxLayout* welcomeButtons = new QHBoxLayout();
    welcomeButtons->setAlignment(Qt::AlignCenter);
    welcomeButtons->setSpacing(20);

    m_recentProjectBtn = new QPushButton(tr("Open Recent Project"), m_welcomePage);
    m_recentProjectBtn->setFixedSize(180, 50);
    m_recentProjectBtn->setStyleSheet("QPushButton { font-size: 14px; }");
    connect(m_recentProjectBtn, &QPushButton::clicked, this, &MainWindow::onOpenRecentProjectRequested);
    welcomeButtons->addWidget(m_recentProjectBtn);

    welcomeLayout->addLayout(welcomeButtons);

    m_welcomeLabel = new QLabel(tr("Or: drag and drop folders, files, or URLs"), m_welcomePage);
    m_welcomeLabel->setAlignment(Qt::AlignCenter);
    m_welcomeLabel->setStyleSheet("font-size: 13px; color: #888; margin-top: 12px;");
    welcomeLayout->addWidget(m_welcomeLabel);

    welcomeLayout->addStretch();
    
    m_mainStack->addWidget(m_welcomePage);

    // Page 2: Export Workspace
    m_exportWorkspace = new ExportWorkspace(this);
    m_mainStack->addWidget(m_exportWorkspace);  // page 1
    connect(m_exportWorkspace, &ExportWorkspace::exportRequested,
            this, &MainWindow::onExportWorkspaceRequested);
    connect(m_exportWorkspace, &ExportWorkspace::cancelled,
            this, &MainWindow::leaveExportWorkspace);

    m_packedAtlasView = new PackedAtlasView(this);
    m_packedAtlasView->hide();

    m_exportLayoutCanvas = new LayoutCanvas(this);
    m_exportLayoutCanvas->hide();
    m_exportLayoutCanvas->setSettings(m_settings);
    m_exportLayoutCanvas->setDisplayOnly(true);

    connect(m_exportWorkspace, &ExportWorkspace::previewRefreshRequested,
            this, [this](int sessionAtlasIndex, const QString& profile, const QString& sf) {
                // Atlas-specific setup (composition): update index and canvas placeholder
                if (m_exportCoordinator)
                    m_exportCoordinator->setExportPreviewAtlasIndex(sessionAtlasIndex);
                if (m_session && m_exportLayoutCanvas) {
                    QVector<LayoutModel> models;
                    if (sessionAtlasIndex < 0) {
                        for (const auto& atlas : m_session->atlases)
                            if (!atlas.isExcluded)
                                models.append(atlas.layoutModels);
                    } else if (sessionAtlasIndex < m_session->atlases.size()) {
                        models = m_session->atlases[sessionAtlasIndex].layoutModels;
                    }
                    m_exportLayoutCanvas->setModels(models);
                    m_exportLayoutCanvas->setZoomManual(false);
                    m_exportLayoutCanvas->initialFit();
                    if (m_exportWorkspaceActive)
                        m_exportWorkspace->setViewport(m_exportLayoutCanvas);
                }
                // Shared refresh: cancel in-progress load, invalidate cache, schedule pack
                if (m_exportCoordinator)
                    m_exportCoordinator->refreshPreview(profile, sf);
            });

    // Page 3: Atlases Management Workspace
    m_atlasesManagementWorkspace = new AtlasesManagementWorkspace(this);
    m_atlasesManagementWorkspace->setSession(m_session);
    m_mainStack->addWidget(m_atlasesManagementWorkspace);  // page 2
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::addAtlasRequested,
            this, [this]() {
                if (!m_session) return;
                AtlasEntry newAtlas;
                newAtlas.id   = QUuid::createUuid().toString(QUuid::WithoutBraces);
                newAtlas.name = tr("Atlas %1").arg(m_session->atlases.size());
                m_session->atlases.append(newAtlas);
                emit m_session->atlasesChanged();
                m_atlasesManagementWorkspace->setAtlases(
                    m_session->atlases, m_session->activeAtlasIndex);
            });
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::atlasRenamed,
            this, [this](int index, const QString& newName) {
                if (!m_session || index < 0 || index >= m_session->atlases.size()) return;
                m_session->atlases[index].name = newName;
                m_session->atlases[index].outputSubdir =
                    newName.toLower().replace(QLatin1Char(' '), QLatin1Char('_'));
                emit m_session->atlasesChanged();
                m_atlasesManagementWorkspace->setAtlases(
                    m_session->atlases, m_session->activeAtlasIndex);
            });
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::removeAtlasRequested,
            this, [this](int index) {
                if (!m_session || index < 0 || index >= m_session->atlases.size()) return;
                if (m_session->atlases[index].isNeutral) return;
                // Move sprites to neutral atlas
                const QStringList orphanPaths = m_session->atlases[index].spritePaths;
                const int neutralIdx = m_session->neutralAtlasIndex();
                for (const QString& p : orphanPaths) {
                    if (!m_session->atlases[neutralIdx].spritePaths.contains(p))
                        m_session->atlases[neutralIdx].spritePaths.append(p);
                }
                m_session->atlases.remove(index);
                m_session->activeAtlasIndex = qBound(0, m_session->activeAtlasIndex,
                                                      m_session->atlases.size() - 1);
                emit m_session->atlasesChanged();
                m_atlasesManagementWorkspace->setAtlases(
                    m_session->atlases, m_session->activeAtlasIndex);
            });
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::moveSpritesRequested,
            this, [this](const QStringList& paths, int sourceAtlasIndex, int targetAtlasIndex) {
                if (!m_session) return;
                if (sourceAtlasIndex < 0 || sourceAtlasIndex >= m_session->atlases.size()) return;
                if (targetAtlasIndex < 0 || targetAtlasIndex >= m_session->atlases.size()) return;
                const int excIdx          = m_session->excludedAtlasIndex();
                const bool toExcluded     = (targetAtlasIndex == excIdx);
                const bool fromExcluded   = (sourceAtlasIndex == excIdx);
                moveAtlasSprites(paths, sourceAtlasIndex, targetAtlasIndex);
                emit m_session->atlasesChanged();
                m_atlasesManagementWorkspace->refreshSpriteList(m_session->atlases);
                if (m_atlasesManagementWorkspace->viewMode()
                        == AtlasesManagementWorkspace::ViewMode::Layout) {
                    if (m_canvas)
                        m_canvas->setModels(m_session->atlases[sourceAtlasIndex].layoutModels);
                    scheduleLayoutRebuild(true);
                }
                if (toExcluded || fromExcluded)
                    refreshSpriteTree();
            });
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::createAtlasFromGroupRequested,
            this, [this](const QString& groupName, const QStringList& paths) {
                if (!m_session || paths.isEmpty()) return;
                const int srcIdx = m_atlasesManagementWorkspace->selectedAtlasIndex();
                if (srcIdx < 0 || srcIdx >= m_session->atlases.size()) return;
                AtlasEntry newAtlas;
                newAtlas.id           = QUuid::createUuid().toString(QUuid::WithoutBraces);
                newAtlas.name         = groupName;
                newAtlas.outputSubdir = groupName.toLower().replace(QLatin1Char(' '), QLatin1Char('_'));
                m_session->atlases.append(newAtlas);
                moveAtlasSprites(paths, srcIdx, m_session->atlases.size() - 1);
                emit m_session->atlasesChanged();
                m_atlasesManagementWorkspace->setAtlases(m_session->atlases, m_session->activeAtlasIndex);
            });
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::autoCreateAtlasesRequested,
            this, [this](const QVector<QPair<QString, QStringList>>& groups) {
                if (!m_session || groups.isEmpty()) return;
                const int srcIdx = m_atlasesManagementWorkspace->selectedAtlasIndex();
                if (srcIdx < 0 || srcIdx >= m_session->atlases.size()) return;
                for (const auto& [groupName, paths] : groups) {
                    AtlasEntry newAtlas;
                    newAtlas.id           = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    newAtlas.name         = groupName;
                    newAtlas.outputSubdir = groupName.toLower().replace(QLatin1Char(' '), QLatin1Char('_'));
                    m_session->atlases.append(newAtlas);
                    moveAtlasSprites(paths, srcIdx, m_session->atlases.size() - 1);
                }
                emit m_session->atlasesChanged();
                m_atlasesManagementWorkspace->setAtlases(m_session->atlases, m_session->activeAtlasIndex);
            });
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::atlasSelected,
            this, [this](int index) {
                if (!m_session || index < 0 || index >= m_session->atlases.size()) return;
                m_session->activeAtlasIndex = index;
                if (m_atlasesManagementWorkspace->viewMode()
                        == AtlasesManagementWorkspace::ViewMode::Layout)
                    scheduleLayoutRebuild(true);
            });
    connect(m_session, &ProjectSession::atlasesChanged,
            this, [this]() {
        updateNavigatorAtlasCombo();
        m_session->rebuildSpriteIndex();
    });

    // --- Create Docks ---
    const int groupMargin = 4;
    const int groupTopPadding = 12;
    const int groupBottomMargin = 0;

    // 1. Layout Canvas panel
    QWidget* canvasContent = new QWidget(this);
    canvasContent->setStyleSheet("font-weight: normal;");
    QVBoxLayout* canvasLayout = new QVBoxLayout(canvasContent);
    canvasLayout->setContentsMargins(groupMargin, groupTopPadding, groupMargin, groupBottomMargin);

    // Hidden profile combo – drives layout logic, never shown directly
    m_profileCombo = new QComboBox(this);
    m_profileCombo->setVisible(false);
    m_profileCombo->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_profileCombo->setToolTip(tr("Layout profile"));
    m_profileCombo->setAccessibleName(tr("Layout profile"));
    connect(m_profileCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onProfileChanged);
    // Keep profileCombo in sync with workspace selection
    connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (!m_profileCombo) return;
        const QString name = m_profileCombo->currentData().toString();
        if (m_atlasesManagementWorkspace) m_atlasesManagementWorkspace->setSelectedProfile(name);
    });
    applyConfiguredProfiles(configuredProfiles(), QString());

    // Resolution combo
    m_sourceResolutionCombo = new QComboBox(this);
    m_sourceResolutionCombo->setToolTip(tr("Target source resolution for layout"));
    m_sourceResolutionCombo->setAccessibleName(tr("Source resolution"));
    m_sourceResolutionCombo->addItems(ResolutionsConfig::loadResolutionOptions());
    if (m_sourceResolutionCombo->count() == 0) {
        m_sourceResolutionCombo->addItem("1024x768");
    }

    // Set default resolution based on actual screen size
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QSize screenSize = screen->size();
        int bestIndex = 0;
        int minDistance = std::numeric_limits<int>::max();
        for (int i = 0; i < m_sourceResolutionCombo->count(); ++i) {
            int w, h;
            if (parseResolutionText(m_sourceResolutionCombo->itemText(i), w, h)) {
                int distance = qAbs(w - screenSize.width()) + qAbs(h - screenSize.height());
                if (distance < minDistance) {
                    minDistance = distance;
                    bestIndex = i;
                }
            }
        }
        m_sourceResolutionCombo->setCurrentIndex(bestIndex);
        m_currentResolution = m_sourceResolutionCombo->currentText();
    }

    if (!m_sourceResolutionDebounceTimer) {
        m_sourceResolutionDebounceTimer = new QTimer(this);
        m_sourceResolutionDebounceTimer->setSingleShot(true);
        connect(m_sourceResolutionDebounceTimer, &QTimer::timeout, this, [this]() { scheduleLayoutRebuild(); });
    }
    auto scheduleSourceResolutionLayoutRun = [this](int) {
        const QString requestedRes = m_sourceResolutionCombo->currentText();
        if (requestedRes == m_currentResolution) return;

        QString oldRes = m_currentResolution;
        m_currentResolution = requestedRes;

        m_undoStack->push(new SetSourceResolutionCommand(
            m_sourceResolutionCombo,
            oldRes,
            requestedRes,
            [this]() {
                m_currentResolution = m_sourceResolutionCombo->currentText();
                scheduleLayoutRebuild();
            }
        ));

        if (!m_sourceResolutionDebounceTimer) {
            scheduleLayoutRebuild();
            return;
        }
        m_sourceResolutionDebounceTimer->start(AppConstants::kSourceResDebounceMs);
    };
    connect(m_sourceResolutionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, scheduleSourceResolutionLayoutRun);
    m_sourceResolutionCombo->hide();
    m_sourceResolutionCombo->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    // m_layoutZoom (double) tracks the current layout canvas zoom percentage.

    // AtlasesManagementWorkspace — resolution and profile wiring
    m_atlasesManagementWorkspace->setResolutionOptions(
        ResolutionsConfig::loadResolutionOptions(), m_currentResolution);
    // Keep resolution combos in sync (m_sourceResolutionCombo ↔ AtlasesManagement)
    connect(m_sourceResolutionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() {
        if (m_atlasesManagementWorkspace)
            m_atlasesManagementWorkspace->setCurrentResolution(m_sourceResolutionCombo->currentText());
    });
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::resolutionChanged,
            this, [this](const QString& res) {
        const int idx = m_sourceResolutionCombo->findText(res);
        if (idx >= 0) m_sourceResolutionCombo->setCurrentIndex(idx);
    });
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::selectedProfileChanged,
            this, [this](const QString& name) {
        if (!m_profileCombo) return;
        const int idx = m_profileCombo->findData(name);
        if (idx >= 0 && idx != m_profileCombo->currentIndex())
            m_profileCombo->setCurrentIndex(idx);
    });
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::manageProfilesRequested,
            this, &MainWindow::onManageProfiles);
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::zoomChanged,
            this, &MainWindow::onLayoutZoomChanged);
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::viewModeChanged,
            this, [this](AtlasesManagementWorkspace::ViewMode mode) {
        if (mode == AtlasesManagementWorkspace::ViewMode::Layout) {
            m_atlasesManagementWorkspace->setCanvasWidget(m_canvas);
            m_atlasesManagementWorkspace->setZoom(m_layoutZoom);
            if (!m_session) return;
            // Rebuild so the canvas reflects the currently selected atlas (it may have
            // changed while Navigation mode was active without triggering a layout run).
            scheduleLayoutRebuild(true);
        } else {
            // Clear dim filter before returning the canvas to the Sprites workspace.
            if (m_canvas) m_canvas->setDimFilter(QString());
            m_atlasesManagementWorkspace->clearCanvasWidget();
            // Re-parent the canvas back to its original dock container so it
            // is not left as a floating top-level window blocking the menu bar.
            if (m_canvas && m_atlasViewStack && m_atlasViewStack->widget(0)) {
                QWidget* cc = m_atlasViewStack->widget(0);
                m_canvas->setParent(cc);
                if (auto* l = cc->layout()) l->addWidget(m_canvas);
                m_canvas->show();
            }
        }
    });
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::layoutFilterChanged,
            this, [this](const QString& query) {
        if (m_canvas) m_canvas->setDimFilter(query);
    });

    m_canvas = new LayoutCanvas(this);
    canvasLayout->addWidget(m_canvas);

    m_atlasDimsLabel = new QLabel(canvasContent);
    m_atlasDimsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_atlasDimsLabel->setContentsMargins(8, 2, 8, 2);
    m_atlasDimsLabel->setVisible(false);
    canvasLayout->addWidget(m_atlasDimsLabel);

    connect(m_canvas, &LayoutCanvas::spriteSelected, this, &MainWindow::onSpriteSelected);
    connect(m_canvas, &LayoutCanvas::selectionChanged, this, [this](const QList<SpritePtr>& selection) {
        m_session->selectedSprites = selection;
    });
    connect(m_canvas, &LayoutCanvas::zoomChanged, this, [this](double zoom) {
        m_layoutZoom = zoom * 100.0;
        if (m_atlasesManagementWorkspace)
            m_atlasesManagementWorkspace->setZoom(zoom * 100.0);
    });
    connect(m_canvas, &LayoutCanvas::requestTimelineGeneration, this, &MainWindow::onGenerateTimelinesFromFrames);
    connect(m_canvas, &LayoutCanvas::externalPathDropped, this, &MainWindow::onLayoutCanvasPathDropped);
    connect(m_canvas, &LayoutCanvas::addFramesRequested, this, &MainWindow::onCanvasAddFramesRequested);
    connect(m_canvas, &LayoutCanvas::removeFramesRequested, this, &MainWindow::onCanvasRemoveFramesRequested);
    connect(m_canvas, &LayoutCanvas::removeSmallFramesRequested, m_canvas, &LayoutCanvas::removeFramesSmallerThan);
    connect(m_canvas, &LayoutCanvas::splitSpriteRequested, this, &MainWindow::onSplitSpriteRequested);
    connect(m_canvas, &LayoutCanvas::userInteractionStarted, this, &MainWindow::pauseLayoutRebuild);
    connect(m_canvas, &LayoutCanvas::userInteractionEnded, this, &MainWindow::resumeLayoutRebuild);
    connect(m_canvas, &LayoutCanvas::splitModeChanged, this, [this](bool enabled) {
        m_statusLabel->setText(enabled
            ? tr("Split mode — click a sprite edge to split it. Press S or right-click to exit.")
            : tr("Idle"));
    });
    m_canvas->viewport()->installEventFilter(this);

    // Navigator panel (tree view of sprites) — now managed by NavigatorPanel
    QWidget* navigatorContent = new QWidget(this);
    navigatorContent->setStyleSheet("font-weight: normal;");
    QVBoxLayout* navigatorLayout = new QVBoxLayout(navigatorContent);
    navigatorLayout->setContentsMargins(groupMargin, groupTopPadding, groupMargin, groupBottomMargin);

    m_navigatorPanel = new NavigatorPanel(navigatorContent);
    navigatorLayout->addWidget(m_navigatorPanel);

    // Wire up NavigatorPanel's internal widgets back to MainWindow's existing pointers
    // so all other code (context menus, filterSpriteTree, etc.) keeps working unchanged.
    m_spriteTree            = m_navigatorPanel->tree();
    m_spriteFilterEdit      = m_navigatorPanel->filterEdit();
    m_spriteFilterResultLabel = m_navigatorPanel->filterResultLabel();
    m_showHiddenToggleBtn   = m_navigatorPanel->showHiddenCheckBox();
    m_navigatorAtlasRow     = m_navigatorPanel->atlasRow();
    m_navigatorAtlasCombo   = m_navigatorPanel->atlasCombo();

    // Atlas combo changes: update session + refresh everything
    connect(m_navigatorPanel, &NavigatorPanel::atlasIndexChanged,
            this, [this](int atlasIndex) {
                if (!m_session) return;
                if (atlasIndex >= 0 && atlasIndex < m_session->atlases.size()) {
                    m_session->activeAtlasIndex = atlasIndex;
                    refreshSpriteTree();
                    refreshTimelineList();
                    refreshAnimationTest();
                    // Ensure the selected atlas is packed so the sprite tree is populated.
                    scheduleLayoutRebuild(true);
                } else {
                    // "All" selected: refresh tree only, active atlas unchanged.
                    refreshSpriteTree();
                }
            });

    // Show-hidden checkbox
    connect(m_navigatorPanel, &NavigatorPanel::showHiddenChanged, this, [this](bool checked) {
        m_showHiddenItems = checked;
        refreshSpriteTree();
    });

    // Context menu + exclude key: delegate to existing MainWindow slots
    connect(m_spriteTree, &QWidget::customContextMenuRequested,
            this, &MainWindow::onSpriteTreeContextMenu);
    connect(m_navigatorPanel, &NavigatorPanel::excludeKeyPressed,
            this, &MainWindow::onNavigatorExcludeKey);

    // Checkbox toggling: only propagate to children/parents (no canvas side-effects)
    connect(m_spriteTree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int /*column*/) {
        m_spriteTree->blockSignals(true);

        // If a group node was checked/unchecked, propagate to all descendants
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

        // Walk up and update all ancestor group states
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

        m_spriteTree->blockSignals(false);

        // Rebuild selectedSprites from all checked leaf items so that
        // "Apply to Selected Frames" reflects the navigator checkbox state.
        if (m_session) {
            QList<SpritePtr> checked;
            std::function<void(QTreeWidgetItem*)> collectChecked = [&](QTreeWidgetItem* node) {
                if (node->childCount() == 0) {
                    if (node->checkState(0) == Qt::Checked) {
                        auto sprite = node->data(0, Qt::UserRole).value<SpritePtr>();
                        if (sprite) checked.append(sprite);
                    }
                } else {
                    for (int i = 0; i < node->childCount(); ++i)
                        collectChecked(node->child(i));
                }
            };
            for (int i = 0; i < m_spriteTree->topLevelItemCount(); ++i)
                collectChecked(m_spriteTree->topLevelItem(i));
            m_session->selectedSprites = checked;
            updateOnionSkinDisplay();

            // Update multi-selection label in the sprite editor panel
            if (m_multiSelectionLabel) {
                const int n = checked.size();
                if (n > 1 && m_settings.propagateEditsToChecked) {
                    m_multiSelectionLabel->setText(
                        tr("%1 sprites selected — pivot and marker changes apply to all").arg(n));
                    m_multiSelectionLabel->setVisible(true);
                } else {
                    m_multiSelectionLabel->setVisible(false);
                }
            }
        }
    });

    // Selecting (highlighting) a sprite row makes it the active/editable sprite
    connect(m_spriteTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current, QTreeWidgetItem* /*previous*/) {
        if (!current) return;
        QVariant v = current->data(0, Qt::UserRole);
        if (!v.isValid()) return;
        auto sprite = v.value<SpritePtr>();
        if (sprite) onSpriteSelected(sprite);
    });

    // Atlas view stack: page 0 = Layout, page 1 = Navigator, page 2 = Empty state
    m_atlasViewStack = new QStackedWidget(this);
    m_atlasViewStack->addWidget(canvasContent);
    m_atlasViewStack->addWidget(navigatorContent);
    auto* emptyAtlasLabel = new QLabel(tr("Drag and drop folder, files or URLs"), m_atlasViewStack);
    emptyAtlasLabel->setAlignment(Qt::AlignCenter);
    emptyAtlasLabel->setStyleSheet("font-size: 14px; color: #888;");
    m_atlasViewStack->addWidget(emptyAtlasLabel);
    m_atlasViewStack->setCurrentIndex(1);

    // 2. Animation Timelines panel — owns all timeline business logic (Phase 7)
    m_timelineEditorPanel = new TimelineEditorPanel(m_session, m_undoStack, this);

    // Connect TimelineEditorPanel signals to MainWindow animation state
    connect(m_timelineEditorPanel, &TimelineEditorPanel::animPlaybackIntervalChanged, this,
        [this](int fps) {
            if (m_animPlaying) {
#ifndef Q_OS_WASM
                m_animTimer->setInterval(1000 / qMax(1, fps));
#endif
                m_animElapsed.restart();
            }
        });
    connect(m_timelineEditorPanel, &TimelineEditorPanel::animFrameReset, this,
        [this]() {
            m_animFrameIndex = 0;
            fitAnimationToViewport();
            refreshAnimationTest();
        });
    connect(m_timelineEditorPanel, &TimelineEditorPanel::animFrameIndexSelected, this,
        [this](int index) {
            if (!m_animPlaying) {
                m_animFrameIndex = index;
                refreshAnimationTest();
            }
        });
    connect(m_timelineEditorPanel, &TimelineEditorPanel::animZoomResetAndFitRequested, this,
        [this]() {
            if (m_animCanvas) m_animCanvas->setZoomManual(false);
            fitAnimationToViewport();
            refreshAnimationTest();
        });
    connect(m_timelineEditorPanel, &TimelineEditorPanel::animationDataChanged,
        this, &MainWindow::refreshAnimationTest);
    connect(m_timelineEditorPanel, &TimelineEditorPanel::statusMessage, this,
        [this](const QString& text) { if (m_statusLabel) m_statusLabel->setText(text); });
    connect(m_timelineEditorPanel, &TimelineEditorPanel::spritesToTimelineRequested,
        this, &MainWindow::onSpritesDroppedToTimeline);

    // The panel widget itself is never placed in any layout — its sub-widgets are
    // distributed directly into the animation dock. Hide it so it doesn't sit as
    // an invisible 100×30 overlay on top of the menu bar.
    m_timelineEditorPanel->hide();

    // 3. Selected Frame Editor panel — owned by SpriteEditorPanel
    m_spriteEditorPanel = new SpriteEditorPanel(this);
    m_editorContent = m_spriteEditorPanel;

    // Wire MainWindow's raw-pointer members to the panel's child widgets
    m_multiSelectionLabel    = m_spriteEditorPanel->multiSelectionLabel();
    m_spriteNameEdit         = m_spriteEditorPanel->spriteNameEdit();
    m_editAliasesBtn         = m_spriteEditorPanel->editAliasesBtn();
    m_previewZoomSpin        = m_spriteEditorPanel->previewZoomSpin();
    m_handleCombo            = m_spriteEditorPanel->handleCombo();
    m_pivotXSpin             = m_spriteEditorPanel->pivotXSpin();
    m_pivotYSpin             = m_spriteEditorPanel->pivotYSpin();
    m_coordUnitCombo         = m_spriteEditorPanel->coordUnitCombo();
    m_configPointsBtn        = m_spriteEditorPanel->configPointsBtn();
    m_previewView            = m_spriteEditorPanel->previewCanvas();
    m_spriteNameFooterLabel  = m_spriteEditorPanel->spriteNameFooterLabel();
    m_spriteDimsLabel        = m_spriteEditorPanel->spriteDimsLabel();

    // Apply initial coordinate unit from settings
    m_coordUnitCombo->setCurrentIndex(
        m_settings.coordUnit == CoordUnit::Percent ? 1 : 0);

    // Connect panel widget signals to MainWindow slots
    connect(m_spriteNameEdit, &QLineEdit::editingFinished,
            this, &MainWindow::onSpriteNameEditingFinished);
    connect(m_editAliasesBtn, &QPushButton::clicked, this, &MainWindow::onEditAliases);
    connect(m_handleCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onHandleComboChanged);
    connect(m_pivotXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ onPivotSpinChanged(); });
    connect(m_pivotYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ onPivotSpinChanged(); });
    connect(m_coordUnitCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onCoordUnitChanged);
    connect(m_configPointsBtn, &QPushButton::clicked, this, &MainWindow::onPointsConfigClicked);
    connect(m_previewView, &PreviewCanvas::pivotChanged, this, &MainWindow::onCanvasPivotChanged);
    connect(m_previewView->overlay(), &EditorOverlayItem::markerSelected,
            this, &MainWindow::onMarkerSelectedFromCanvas);
    connect(m_previewView->overlay(), &EditorOverlayItem::markerChanged,
            this, &MainWindow::onMarkerChangedFromCanvas);
    connect(m_previewView, &PreviewCanvas::zoomChanged, this, [this](double zoom) {
        m_previewZoomSpin->blockSignals(true);
        m_previewZoomSpin->setValue(zoom * 100.0);
        m_previewZoomSpin->blockSignals(false);
    });
    connect(m_previewZoomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MainWindow::onPreviewZoomChanged);

    {
        auto* btn = m_spriteEditorPanel->showTrimRectBtn();
        btn->setChecked(m_settings.showTrimRect);
        connect(btn, &QPushButton::toggled, this, [this](bool checked) {
            m_settings.showTrimRect = checked;
            CliToolsConfig::saveAppSettings(m_settings, m_cliPaths);
            m_previewView->setSettings(m_settings);
            clearCoordinateFieldOverride();
            syncCoordinateSpinsFromSelection();
        });
    }

    // 4. Animation Preview panel — owned by AnimationPreviewPanel
    m_animPreviewPanel = new AnimationPreviewPanel(this);

    // Wire MainWindow's raw-pointer members to the panel's child widgets
    m_animPrevBtn      = m_animPreviewPanel->prevButton();
    m_animPlayPauseBtn = m_animPreviewPanel->playPauseButton();
    m_animNextBtn      = m_animPreviewPanel->nextButton();
    m_animOverlayBtn   = m_animPreviewPanel->overlayButton();
    m_animZoomSpin     = m_animPreviewPanel->zoomSpin();
    m_animStatusLabel  = m_animPreviewPanel->statusLabel();
    m_animCanvas       = m_animPreviewPanel->animCanvas();

    // Connect animation panel signals to MainWindow slots
    connect(m_animPrevBtn,      &QPushButton::clicked, this, &MainWindow::onAnimPrevClicked);
    connect(m_animPlayPauseBtn, &QPushButton::clicked, this, &MainWindow::onAnimPlayPauseClicked);
    connect(m_animNextBtn,      &QPushButton::clicked, this, &MainWindow::onAnimNextClicked);
    connect(m_animZoomSpin,     QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MainWindow::onAnimZoomChanged);
    connect(m_animCanvas->overlay(), &EditorOverlayItem::pivotChanged,
            this, &MainWindow::onCanvasPivotChanged);
    connect(m_animCanvas->overlay(), &EditorOverlayItem::markerSelected,
            this, &MainWindow::onMarkerSelectedFromCanvas);
    connect(m_animCanvas->overlay(), &EditorOverlayItem::markerChanged,
            this, &MainWindow::onMarkerChangedFromCanvas);
    connect(m_animOverlayBtn, &QToolButton::toggled, this, [this](bool checked) {
        m_animCanvas->setOverlayVisible(checked);
        if (checked)
            m_animCanvas->setOverlayEditable(!m_animPlaying);
    });

    // 5. CLI Log panel
    QWidget* cliLogContent = new QWidget(this);
    cliLogContent->setStyleSheet("font-weight: normal;");
    QVBoxLayout* cliLogLayout = new QVBoxLayout(cliLogContent);
    cliLogLayout->setContentsMargins(groupMargin, groupTopPadding, groupMargin, groupBottomMargin);
    m_cliLog = new QPlainTextEdit(this);
    m_cliLog->setReadOnly(true);
    m_cliLog->setMaximumBlockCount(AppConstants::kCliLogMaxBlocks);
    m_cliLog->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    cliLogLayout->addWidget(m_cliLog);
    QHBoxLayout* cliLogBtnLayout = new QHBoxLayout();
    cliLogBtnLayout->setContentsMargins(4, 2, 4, 4);
    cliLogBtnLayout->addStretch();
    QPushButton* clearLogBtn = new QPushButton(QApplication::style()->standardIcon(QStyle::SP_LineEditClearButton), tr("Clear"), cliLogContent);
    clearLogBtn->setToolTip(tr("Clear log output"));
    connect(clearLogBtn, &QPushButton::clicked, m_cliLog, &QPlainTextEdit::clear);
    cliLogBtnLayout->addWidget(clearLogBtn);
    cliLogLayout->addLayout(cliLogBtnLayout);
    // --- Assemble group docks ---
    // Each group is a single QDockWidget with a QSplitter holding its panels.
    // Groups occupy separate rows so they never share a dock row.

    // Atlas group: Canvas | Editor
    m_atlasDock = new QDockWidget(tr("Sprites"), this);
    m_atlasDock->setObjectName("atlasDock");
    m_atlasDock->setFont(boldFont);
    m_atlasDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_atlasDock->setTitleBarWidget(new QWidget(this));
    m_atlasSplitter = new QSplitter(Qt::Horizontal, m_atlasDock);
    m_atlasSplitter->addWidget(m_atlasViewStack);
    m_atlasSplitter->addWidget(m_editorContent);
    m_atlasSplitter->setStretchFactor(0, 0);
    m_atlasSplitter->setStretchFactor(1, 1);
    m_atlasSplitter->setSizes({270, 1030});  // sprites tree : frame editor  ~1:4
    m_atlasDock->setWidget(m_atlasSplitter);

    // Animation dock (right column in Frame Animation workspace):
    //   Top  = timeline list
    //   Bottom = selected-timeline editor  +  animation preview
    m_animationDock = new QDockWidget(tr("Animation"), this);
    m_animationDock->setObjectName("animationDock");
    m_animationDock->setFont(boldFont);
    m_animationDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_animationDock->setTitleBarWidget(new QWidget(this));
    {
        auto* animBottomPanel = new QWidget(m_animationDock);
        auto* animBottomLayout = new QVBoxLayout(animBottomPanel);
        animBottomLayout->setContentsMargins(0, 0, 0, 0);
        animBottomLayout->setSpacing(0);
        animBottomLayout->addWidget(m_timelineEditorPanel->timelineEditorContainer());
        animBottomLayout->addWidget(m_animPreviewPanel, 1);

        auto* animSplitter = new QSplitter(Qt::Vertical, m_animationDock);
        animSplitter->addWidget(m_timelineEditorPanel->listAreaWidget());
        animSplitter->addWidget(animBottomPanel);
        animSplitter->setStretchFactor(0, 2);
        animSplitter->setStretchFactor(1, 3);
        m_animationDock->setWidget(animSplitter);
    }

    // Debug group: CLI diagnostics + log
    m_debugDock = new QDockWidget(tr("Debug"), this);
    m_debugDock->setObjectName("debugDock");
    m_debugDock->setFont(boldFont);

    m_cliInfoText = new QPlainTextEdit(this);
    m_cliInfoText->setReadOnly(true);
    m_cliInfoText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_cliInfoText->setStyleSheet("font-weight: normal;");
    m_cliInfoText->setPlaceholderText(tr("CLI diagnostics not yet available."));

    m_debugTabs = new QTabWidget(this);
    m_debugTabs->addTab(cliLogContent, tr("Log"));
    m_debugTabs->addTab(m_cliInfoText, tr("Diagnostics"));
    m_debugDock->setWidget(m_debugTabs);
    connect(m_debugDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible && m_cliLog && m_cliLog->document()->isEmpty()) {
            m_debugTabs->setCurrentIndex(1); // Diagnostics
        }
    });

    // Atlas dock fills the top area; animation dock sits to its right but starts hidden.
    // Debug dock lives below in its own row.
    addDockWidget(Qt::TopDockWidgetArea, m_atlasDock);
    addDockWidget(Qt::TopDockWidgetArea, m_animationDock);
    splitDockWidget(m_atlasDock, m_animationDock, Qt::Horizontal);
    m_animationDock->hide();
    addDockWidget(Qt::BottomDockWidgetArea, m_debugDock);
    resizeDocks({m_atlasDock}, {500}, Qt::Vertical);

    // View menu
    m_viewMenu = menuBar()->addMenu(tr("&Workspace"));

    auto* workspaceGroup = new QActionGroup(this);
    workspaceGroup->setExclusive(true);

    m_exportationWorkspaceAction = m_viewMenu->addAction(
        QApplication::style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Exportation"));
    m_exportationWorkspaceAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_E));
    m_exportationWorkspaceAction->setCheckable(true);
    workspaceGroup->addAction(m_exportationWorkspaceAction);
    connect(m_exportationWorkspaceAction, &QAction::triggered, this, &MainWindow::onExportAsClicked);

    m_viewMenu->addSeparator();

    m_atlasesManagementWorkspaceAction = m_viewMenu->addAction(tr("Atlases"));
    m_atlasesManagementWorkspaceAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_M));
    m_atlasesManagementWorkspaceAction->setCheckable(true);
    workspaceGroup->addAction(m_atlasesManagementWorkspaceAction);
    connect(m_atlasesManagementWorkspaceAction, &QAction::triggered,
            this, &MainWindow::showAtlasesManagementWorkspace);

    m_atlasWorkspaceAction = m_viewMenu->addAction(tr("Sprites"));
    m_atlasWorkspaceAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_A));
    m_atlasWorkspaceAction->setCheckable(true);
    m_atlasWorkspaceAction->setChecked(true);
    workspaceGroup->addAction(m_atlasWorkspaceAction);
    connect(m_atlasWorkspaceAction, &QAction::triggered, this, &MainWindow::switchToAtlasWorkspace);

    m_frameAnimWorkspaceAction = m_viewMenu->addAction(tr("Frame Animation"));
    m_frameAnimWorkspaceAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F));
    m_frameAnimWorkspaceAction->setCheckable(true);
    workspaceGroup->addAction(m_frameAnimWorkspaceAction);
    connect(m_frameAnimWorkspaceAction, &QAction::triggered, this, &MainWindow::switchToFrameAnimWorkspace);

    m_viewMenu->addSeparator();

    auto* debugShortcut = new QAction(this);
    debugShortcut->setShortcut(QKeySequence(Qt::Key_F12));
    addAction(debugShortcut);
    connect(debugShortcut, &QAction::triggered, this, [this]() {
        if (m_debugDock) m_debugDock->setVisible(!m_debugDock->isVisible());
    });

    // Help menu (added last so it sits on the right)
    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction* quickStartAction = helpMenu->addAction(tr("Quick Start..."));
    quickStartAction->setToolTip(tr("Learn the basic workflow"));
    connect(quickStartAction, &QAction::triggered, this, &MainWindow::onQuickStart);

    QAction* hotkeysAction = helpMenu->addAction(tr("Hotkeys..."));
    hotkeysAction->setToolTip(tr("View keyboard shortcuts"));
    connect(hotkeysAction, &QAction::triggered, this, &MainWindow::onShowHotkeys);

    helpMenu->addSeparator();
    QAction* aboutAction = helpMenu->addAction(tr("About..."));
    aboutAction->setToolTip(tr("About Sprat"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAboutClicked);

    // Initially hide docks for welcome page
    m_atlasDock->hide();
    m_animationDock->hide();
    m_debugDock->hide();

    setupStatusBarUi();

    updateUiState();
    applySettings();

    setupZoomShortcuts();
}


void MainWindow::setupToolbar() {
    QMenuBar* mainMenuBar = menuBar();
    QMenu* fileMenu = mainMenuBar->addMenu(tr("File"));

    auto* style = QApplication::style();

    m_recentProjectsMenu = fileMenu->addMenu(tr("Open Recent"));

    fileMenu->addSeparator();

    m_loadProjectAction = fileMenu->addAction(
        style->standardIcon(QStyle::SP_DialogOpenButton), tr("Load..."));
    m_loadProjectAction->setToolTip(
        tr("Load a project.spart.json file or a project folder containing that file"));
    connect(m_loadProjectAction, &QAction::triggered, this, &MainWindow::onLoadProject);

    m_loadAction = fileMenu->addAction(
        style->standardIcon(QStyle::SP_DirOpenIcon), tr("Add Source Folder..."));
    m_loadAction->setToolTip(tr("Add a source folder of sprite images to the current project"));
    connect(m_loadAction, &QAction::triggered, this, &MainWindow::onLoadFolder);

    m_addSourceFileAction = fileMenu->addAction(
        style->standardIcon(QStyle::SP_FileIcon), tr("Add Source File..."));
    m_addSourceFileAction->setToolTip(
        tr("Add a source image or archive file such as PNG, GIF, ZIP, or TAR"));
    connect(m_addSourceFileAction, &QAction::triggered, this, &MainWindow::onAddSourceFile);

    m_addSourceUrlAction = fileMenu->addAction(
        style->standardIcon(QStyle::SP_CommandLink), tr("Add Source URL..."));
    m_addSourceUrlAction->setToolTip(tr("Download and add an image or archive from a URL"));
    connect(m_addSourceUrlAction, &QAction::triggered, this, &MainWindow::onLoadFromUrl);

    fileMenu->addSeparator();
    m_saveAction = fileMenu->addAction(
        style->standardIcon(QStyle::SP_DialogSaveButton), tr("Save"));
    m_saveAction->setShortcut(QKeySequence::Save);      // Ctrl+S
    m_saveAction->setEnabled(false);
    m_saveAction->setToolTip(tr("Save project state to the current folder"));
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::onSaveClicked);

#ifndef Q_OS_WASM
    m_saveAsAction = fileMenu->addAction(
        style->standardIcon(QStyle::SP_DialogSaveButton), tr("Save As..."));
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);  // Ctrl+Shift+S
    m_saveAsAction->setEnabled(false);
    m_saveAsAction->setToolTip(tr("Save project state to a new folder"));
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::onSaveAsClicked);
#endif

    m_exportAction = fileMenu->addAction(
        style->standardIcon(QStyle::SP_DialogSaveButton), tr("Export"));
    m_exportAction->setEnabled(false);
    m_exportAction->setToolTip(tr("Re-run the export pipeline to the last-used destination"));
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::onExportClicked);

    fileMenu->addSeparator();
    QAction* quitAction = fileMenu->addAction(
        style->standardIcon(QStyle::SP_DialogCloseButton), tr("Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &MainWindow::close);

    QMenu* editMenu = mainMenuBar->addMenu(tr("&Edit"));
    QAction* undoAction = editMenu->addAction(
        style->standardIcon(QStyle::SP_ArrowBack), tr("&Undo"));
    undoAction->setIcon(style->standardIcon(QStyle::SP_ArrowBack));
    undoAction->setShortcut(QKeySequence::Undo);
    undoAction->setEnabled(m_undoStack->canUndo());
    connect(undoAction, &QAction::triggered, this, [this]() {
        clearCoordinateFieldOverride();
        m_undoStack->undo();
    });
    connect(m_undoStack, &QUndoStack::canUndoChanged, undoAction, &QAction::setEnabled);

    QAction* redoAction = editMenu->addAction(
        style->standardIcon(QStyle::SP_ArrowForward), tr("&Redo"));
    redoAction->setIcon(style->standardIcon(QStyle::SP_ArrowForward));
    redoAction->setShortcut(QKeySequence::Redo);
    redoAction->setEnabled(m_undoStack->canRedo());
    connect(redoAction, &QAction::triggered, this, [this]() {
        clearCoordinateFieldOverride();
        m_undoStack->redo();
    });
    connect(m_undoStack, &QUndoStack::canRedoChanged, redoAction, &QAction::setEnabled);

    QMenu* settingsMenu = mainMenuBar->addMenu(tr("Settings"));
    QAction* spritesheetAction = settingsMenu->addAction(
        style->standardIcon(QStyle::SP_FileDialogListView), tr("Atlas Sprites..."));
    spritesheetAction->setToolTip(tr("Open atlas sprites settings"));
    connect(spritesheetAction, &QAction::triggered, this, &MainWindow::onSettingsSpritesheetClicked);

    QAction* spritesNavigatorAction = settingsMenu->addAction(
        style->standardIcon(QStyle::SP_FileDialogContentsView), tr("Sprites Navigator..."));
    spritesNavigatorAction->setToolTip(tr("Open sprites navigator settings"));
    connect(spritesNavigatorAction, &QAction::triggered, this, &MainWindow::onSettingsSpritesNavigatorClicked);

    QAction* framesEditorAction = settingsMenu->addAction(
        style->standardIcon(QStyle::SP_FileDialogDetailedView), tr("Frames Editor..."));
    framesEditorAction->setToolTip(tr("Open frames editor settings"));
    connect(framesEditorAction, &QAction::triggered, this, &MainWindow::onSettingsFramesEditorClicked);

    QAction* atlasLayoutAction = settingsMenu->addAction(
        style->standardIcon(QStyle::SP_DesktopIcon), tr("Atlas Layout..."));
    atlasLayoutAction->setToolTip(tr("Open atlas layout settings"));
    connect(atlasLayoutAction, &QAction::triggered, this, &MainWindow::onSettingsAtlasLayoutClicked);

    QAction* exportationAction = settingsMenu->addAction(
        style->standardIcon(QStyle::SP_DialogSaveButton), tr("Exportation..."));
    exportationAction->setToolTip(tr("Open exportation settings"));
    connect(exportationAction, &QAction::triggered, this, &MainWindow::onSettingsExportationClicked);

#ifndef Q_OS_WASM
    QAction* cliToolsAction = settingsMenu->addAction(
        style->standardIcon(QStyle::SP_ComputerIcon), tr("CLI Tools..."));
    cliToolsAction->setToolTip(tr("Open CLI tools settings"));
    connect(cliToolsAction, &QAction::triggered, this, &MainWindow::onSettingsCliToolsClicked);
#endif

    QAction* manageProfilesAction = settingsMenu->addAction(
        style->standardIcon(QStyle::SP_FileDialogDetailedView), tr("Manage Profiles..."));
    manageProfilesAction->setToolTip(tr("Create and edit layout profiles"));
    connect(manageProfilesAction, &QAction::triggered, this, &MainWindow::onManageProfiles);
}

void MainWindow::setupStatusBarUi() {
    m_statusProgressBar = new QProgressBar(this);
    m_statusProgressBar->setRange(0, 0);        // indeterminate (busy) mode
    m_statusProgressBar->setFixedWidth(120);
    m_statusProgressBar->setMaximumHeight(14);
    m_statusProgressBar->setVisible(false);     // hidden by default
    m_statusProgressBar->setAccessibleName(tr("Operation progress"));
    statusBar()->addPermanentWidget(m_statusProgressBar);

    m_folderLabel = new QLabel(tr("Folder: none"), this);
    m_folderLabel->setContentsMargins(0, 0, 12, 0);
    statusBar()->addPermanentWidget(m_folderLabel);

    m_statusLabel = new QLabel(tr("Idle"), this);
    m_statusLabel->setContentsMargins(0, 0, 12, 0);
    m_statusLabel->setTextFormat(Qt::RichText);
    m_statusLabel->setOpenExternalLinks(false);
    connect(m_statusLabel, &QLabel::linkActivated, this, [this](const QString& link) {
        Q_UNUSED(link)
    });
    statusBar()->addPermanentWidget(m_statusLabel);
}

void MainWindow::setupZoomShortcuts() {
    auto performZoom = [this](bool zoomIn) {
        QWidget* fw = QApplication::focusWidget();
        if (!fw) {
            return;
        }

        const double scaleFactor = 1.25;
        if (m_canvas && (fw == m_canvas || m_canvas->isAncestorOf(fw))) {
            double zoom = qBound(10.0, zoomIn ? m_layoutZoom * scaleFactor : m_layoutZoom / scaleFactor, 800.0);
            m_layoutZoom = zoom;
            onLayoutZoomChanged(zoom);
            return;
        }
        QDoubleSpinBox* targetSpin = nullptr;
        if (m_previewView && (fw == m_previewView || m_previewView->isAncestorOf(fw))) {
            targetSpin = m_previewZoomSpin;
        } else if (m_animCanvas && (fw == m_animCanvas || m_animCanvas->isAncestorOf(fw))) {
            targetSpin = m_animZoomSpin;
        } else if (m_exportWorkspace && m_exportWorkspaceActive && (fw == m_exportWorkspace || m_exportWorkspace->isAncestorOf(fw))) {
            targetSpin = m_exportWorkspace->zoomSpin();
        }

        if (!targetSpin) {
            return;
        }
        double zoom = targetSpin->value();
        zoom = zoomIn ? zoom * scaleFactor : zoom / scaleFactor;
        targetSpin->setValue(zoom);
    };

    QShortcut* zoomIn = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus), this);
    connect(zoomIn, &QShortcut::activated, this, [performZoom]() { performZoom(true); });
    QShortcut* zoomInEq = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), this);
    connect(zoomInEq, &QShortcut::activated, this, [performZoom]() { performZoom(true); });
    QShortcut* zoomOut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), this);
    connect(zoomOut, &QShortcut::activated, this, [performZoom]() { performZoom(false); });

    // Helper lambda to apply 100% or fit to the focused canvas
    auto applyZoomPreset = [this](bool fitToContent) {
        QWidget* fw = QApplication::focusWidget();
        if (!fw) return;
        if (m_canvas && (fw == m_canvas || m_canvas->isAncestorOf(fw))) {
            if (fitToContent) { m_canvas->setZoomManual(false); m_canvas->initialFit(); }
            else              { m_layoutZoom = 100.0; onLayoutZoomChanged(100.0); }
        } else if (m_previewView && (fw == m_previewView || m_previewView->isAncestorOf(fw))) {
            if (fitToContent) { m_previewView->setZoomManual(false); m_previewView->initialFit(); }
            else              { m_previewView->setZoomManual(true);  m_previewZoomSpin->setValue(100.0); }
        } else if (m_animCanvas && (fw == m_animCanvas || m_animCanvas->isAncestorOf(fw))) {
            if (fitToContent) { m_animCanvas->setZoomManual(false); m_animCanvas->initialFit(); }
            else              { m_animCanvas->setZoomManual(true);  m_animZoomSpin->setValue(100.0); }
        } else if (m_exportWorkspace && m_exportWorkspaceActive && (fw == m_exportWorkspace || m_exportWorkspace->isAncestorOf(fw))) {
            if (fitToContent) {
                if (m_packedAtlasView) {
                    m_packedAtlasView->setZoomManual(false);
                    m_packedAtlasView->initialFit();
                }
            } else {
                if (m_packedAtlasView) m_packedAtlasView->setZoomManual(true);
                m_exportWorkspace->zoomSpin()->setValue(100.0);
            }
        }
    };

    QShortcut* zoom100 = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_1), this);
    connect(zoom100, &QShortcut::activated, this, [applyZoomPreset]() { applyZoomPreset(false); });

    QShortcut* zoomFit = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this);
    connect(zoomFit, &QShortcut::activated, this, [applyZoomPreset]() { applyZoomPreset(true); });
}

void MainWindow::setupKeyboardShortcuts() {
    // Save/undo/redo shortcuts are provided by their QAction bindings.
    // Duplicating them with QShortcut causes handlers to fire twice.

    // Ctrl+V → Import image/file/URL from clipboard
    QShortcut* pasteShortcut = new QShortcut(QKeySequence::Paste, this);
    connect(pasteShortcut, &QShortcut::activated, this, &MainWindow::onPasteImport);
}

void MainWindow::updateUiState() {
    const bool enabled = m_cliReady && !m_isLoading;
    const bool hasModels = m_session && !m_session->activeAtlas().layoutModels.isEmpty() && !m_session->activeAtlas().layoutModels.first().sprites.isEmpty();

    MainWindowUiState::apply(
        m_cliReady,
        m_isLoading,
        hasModels,
        !m_lastSaveConfig.outputPath.isEmpty(),
        m_loadAction,
        m_profileCombo,
        m_saveAction,
        m_exportAction,
        m_saveAsAction);

    const bool sourceActionsEnabled = m_loadAction ? m_loadAction->isEnabled() : false;
    if (m_loadProjectAction) {
        m_loadProjectAction->setEnabled(!m_isLoading);
    }
    if (m_addSourceFileAction) {
        m_addSourceFileAction->setEnabled(sourceActionsEnabled);
    }
    if (m_addSourceUrlAction) {
        m_addSourceUrlAction->setEnabled(sourceActionsEnabled);
    }

    if (m_recentProjectBtn) {
        m_recentProjectBtn->setEnabled(!m_isLoading);
    }

    if (m_addProfilesBtn) {
        m_addProfilesBtn->setEnabled(enabled);
    }
    if (m_sourceResolutionCombo) {
        m_sourceResolutionCombo->setEnabled(enabled);
    }

    // View menu items: enabled once a project is open or sprites are loaded
    const bool hasProject = m_projectController && !m_projectController->projectFilePath().isEmpty();
    const bool viewEnabled = hasProject || hasModels;
    if (m_atlasWorkspaceAction)         m_atlasWorkspaceAction->setEnabled(viewEnabled);
    if (m_frameAnimWorkspaceAction)     m_frameAnimWorkspaceAction->setEnabled(viewEnabled);
    if (m_exportationWorkspaceAction)   m_exportationWorkspaceAction->setEnabled(viewEnabled);

    // Toggle docks and welcome page based on project / sprite state.
    // Skip this block while any full-screen workspace is active.
    if (m_exportWorkspaceActive || m_atlasesManagementWorkspaceActive) return;

    if (hasModels) {
        m_mainStack->hide();
        if (m_atlasDock && m_atlasDock->isHidden()) m_atlasDock->show();
        const bool wantsAnim = (m_activeWorkspace == Workspace::FrameAnimation);
        if (m_animationDock) m_animationDock->setVisible(wantsAnim);
        if (m_atlasViewStack && m_atlasViewStack->currentIndex() == 2)
            m_atlasViewStack->setCurrentIndex(1); // Navigation
    } else if (hasProject) {
        m_mainStack->hide();
        if (m_atlasDock && m_atlasDock->isHidden()) m_atlasDock->show();
        const bool wantsAnim = (m_activeWorkspace == Workspace::FrameAnimation);
        if (m_animationDock) m_animationDock->setVisible(wantsAnim);
        if (m_atlasViewStack) m_atlasViewStack->setCurrentIndex(2); // Empty-state placeholder
    } else {
        m_mainStack->setCurrentIndex(0); // Welcome page
        m_mainStack->show();
        if (m_atlasDock) m_atlasDock->hide();
        if (m_animationDock) m_animationDock->hide();
        if (m_debugDock) m_debugDock->hide();
    }
}

void MainWindow::updateMainContentView() {
    const bool hasLayout = m_session && !m_session->activeAtlas().layoutModels.isEmpty() && !m_session->activeAtlas().layoutModels.first().sprites.isEmpty();
    if (m_atlasDimsLabel && !hasLayout)
        m_atlasDimsLabel->setVisible(false);
    const QString projectFilePathUi = m_projectController ? m_projectController->projectFilePath() : QString();
    const bool hasProject = !projectFilePathUi.isEmpty();

    if (!m_exportWorkspaceActive && !m_atlasesManagementWorkspaceActive) {
        m_mainStack->setVisible(!hasLayout && !hasProject);
        if (!hasLayout && !hasProject) {
            m_mainStack->setCurrentIndex(0);
            setWindowTitle(tr("Sprat GUI %1[*]").arg(SPRAT_GUI_VERSION));
            return;
        }
    }
    if (hasLayout) {
        QString projectName = QFileInfo(projectFilePathUi).dir().dirName();
        if (projectName.isEmpty() || projectName == ".") {
            projectName = tr("Untitled Project");
        }
        setWindowTitle(tr("%1 — %2[*]").arg(projectName).arg(m_session->currentFolder));
    }
    // hasProject && !hasLayout: title was set when the project was created/opened; leave it.
}
void MainWindow::updateRecentProjectsMenu() {
    if (!m_recentProjectsMenu) return;
    m_recentProjectsMenu->clear();
    if (m_recentProjects.isEmpty()) {
        QAction* emptyAction = m_recentProjectsMenu->addAction(tr("(No recent projects)"));
        emptyAction->setEnabled(false);
        return;
    }
    for (const QString& path : m_recentProjects) {
        QAction* action = m_recentProjectsMenu->addAction(QFileInfo(path).fileName());
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, path]() { loadProject(path); });
    }
}

void MainWindow::addToRecentProjects(const QString& path) {
    m_recentProjects.removeAll(path);
    m_recentProjects.prepend(path);
    while (m_recentProjects.size() > AppConstants::kRecentProjectsMax)
        m_recentProjects.removeLast();
    CliToolsConfig::saveRecentProjects(m_recentProjects);
    updateRecentProjectsMenu();
}

void MainWindow::clearCoordinateFieldOverride() {
    m_coordinateFieldOverride = {};
}

void MainWindow::storeCoordinateFieldOverride() {
    if (!m_session || !m_session->selectedSprite || !m_pivotXSpin || !m_pivotYSpin) {
        clearCoordinateFieldOverride();
        return;
    }

    m_coordinateFieldOverride.active = true;
    m_coordinateFieldOverride.sprite = m_session->selectedSprite.get();
    m_coordinateFieldOverride.markerName = m_session->selectedPointName;
    m_coordinateFieldOverride.unit = m_settings.coordUnit;
    m_coordinateFieldOverride.showTrimRect = m_settings.showTrimRect;
    m_coordinateFieldOverride.x = m_pivotXSpin->value();
    m_coordinateFieldOverride.y = m_pivotYSpin->value();
}

bool MainWindow::coordinateFieldOverrideApplies() const {
    return m_coordinateFieldOverride.active
        && m_session
        && m_session->selectedSprite
        && m_coordinateFieldOverride.sprite == m_session->selectedSprite.get()
        && m_coordinateFieldOverride.markerName == m_session->selectedPointName
        && m_coordinateFieldOverride.unit == m_settings.coordUnit
        && m_coordinateFieldOverride.showTrimRect == m_settings.showTrimRect;
}

void MainWindow::syncPivotSpinsFromSprite() {
    syncCoordinateSpinsFromSelection();
    if (m_previewView && m_previewView->overlay())
        m_previewView->overlay()->updateLayout();
    if (m_animCanvas && m_animCanvas->overlay())
        m_animCanvas->overlay()->updateLayout();
}

void MainWindow::syncCoordinateSpinsFromSelection() {
    if (!m_session || !m_session->selectedSprite) {
        if (m_coordUnitCombo) {
            m_coordUnitCombo->setEnabled(false);
        }
        return;
    }

    const QSize spriteSize = spriteCoordinateSpaceSize(m_session->selectedSprite);
    int spriteWidth = spriteSize.width();
    int spriteHeight = spriteSize.height();
    int originX = 0, originY = 0;
    if (m_settings.showTrimRect && m_previewView) {
        const QRect tr = m_previewView->cachedTrimRect();
        if (tr.isValid()) {
            originX     = tr.left();
            originY     = tr.top();
            spriteWidth  = tr.width();
            spriteHeight = tr.height();
        }
    }
    const bool hasDimensions = spriteWidth > 0 && spriteHeight > 0;
    const CoordUnit displayUnit = hasDimensions ? m_settings.coordUnit : CoordUnit::Pixels;

    if (m_coordUnitCombo) {
        m_coordUnitCombo->setEnabled(hasDimensions);
    }
    if (m_pivotXSpin) {
        m_pivotXSpin->blockSignals(true);
        m_pivotXSpin->setDecimals(displayUnit == CoordUnit::Percent ? 1 : 0);
        m_pivotXSpin->blockSignals(false);
    }
    if (m_pivotYSpin) {
        m_pivotYSpin->blockSignals(true);
        m_pivotYSpin->setDecimals(displayUnit == CoordUnit::Percent ? 1 : 0);
        m_pivotYSpin->blockSignals(false);
    }

    if (coordinateFieldOverrideApplies()) {
        m_pivotXSpin->blockSignals(true);
        m_pivotYSpin->blockSignals(true);
        m_pivotXSpin->setValue(m_coordinateFieldOverride.x);
        m_pivotYSpin->setValue(m_coordinateFieldOverride.y);
        m_pivotXSpin->blockSignals(false);
        m_pivotYSpin->blockSignals(false);
        return;
    }

    if (m_session->selectedPointName.isEmpty()) {
        setCoordinateSpinValue(m_pivotXSpin, m_session->selectedSprite->pivotX, spriteWidth, displayUnit, originX);
        setCoordinateSpinValue(m_pivotYSpin, m_session->selectedSprite->pivotY, spriteHeight, displayUnit, originY);
        return;
    }

    for (const auto& point : m_session->selectedSprite->points) {
        if (point.name != m_session->selectedPointName) {
            continue;
        }
        setCoordinateSpinValue(m_pivotXSpin, point.x, spriteWidth, displayUnit, originX);
        setCoordinateSpinValue(m_pivotYSpin, point.y, spriteHeight, displayUnit, originY);
        return;
    }

    setCoordinateSpinValue(m_pivotXSpin, m_session->selectedSprite->pivotX, spriteWidth, displayUnit, originX);
    setCoordinateSpinValue(m_pivotYSpin, m_session->selectedSprite->pivotY, spriteHeight, displayUnit, originY);
}

// Recursively compute tristate check state for every folder node from the
// bottom up.  Must be called with signals blocked so the setCheckState calls
// don't cascade back into onSpriteTreeItemChanged.
static void updateSpriteTreeFolderCheckState(QTreeWidgetItem* item)
{
    for (int i = 0; i < item->childCount(); ++i)
        updateSpriteTreeFolderCheckState(item->child(i));
    if (item->data(0, Qt::UserRole).isValid())      return; // sprite leaf
    if (item->data(0, Qt::UserRole + 2).toInt() > 0) return; // hidden/excluded special item
    int checked = 0, unchecked = 0;
    for (int i = 0; i < item->childCount(); ++i) {
        const Qt::CheckState cs = item->child(i)->checkState(0);
        if (cs == Qt::Checked)        ++checked;
        else if (cs == Qt::Unchecked) ++unchecked;
        else { ++checked; ++unchecked; }
    }
    if (checked == 0)        item->setCheckState(0, Qt::Unchecked);
    else if (unchecked == 0) item->setCheckState(0, Qt::Checked);
    else                     item->setCheckState(0, Qt::PartiallyChecked);
}

void MainWindow::refreshSpriteTree() {
    if (!m_spriteTree) return;

    // Keep excluded atlas in sync with sources' excludedFiles before rebuilding.
    syncExcludedAtlas();

    // Refresh the atlases workspace sprite list if it is visible.
    if (m_atlasesManagementWorkspace && m_atlasesManagementWorkspaceActive)
        m_atlasesManagementWorkspace->refreshSpriteList(m_session->atlases);

    // Delegate the actual tree build to NavigatorPanel when it's available.
    if (m_navigatorPanel && m_session) {
        int atlasFilter = -1;
        if (m_activeWorkspace == Workspace::FrameAnimation && m_navigatorAtlasCombo)
            atlasFilter = m_navigatorAtlasCombo->currentData().toInt();
        m_navigatorPanel->refresh(m_session, m_showHiddenItems, atlasFilter);
        return;
    }

    // Fallback (should not normally be reached once NavigatorPanel is active)

    // ── Save tree state before the rebuild ──────────────────────────────────
    // Key a folder node by joining its full text path with a unit-separator
    // so we can match it again after the tree is reconstructed.
    auto treeItemKey = [](QTreeWidgetItem* node) -> QString {
        QStringList parts;
        while (node) { parts.prepend(node->text(0)); node = node->parent(); }
        return parts.join(QChar(0x1F));
    };
    const bool hadItems = m_spriteTree->invisibleRootItem()->childCount() > 0;
    QSet<QString> collapsedKeys;
    QSet<QString> checkedPaths;
    int scrollPos = 0;
    if (hadItems) {
        QTreeWidgetItemIterator sit(m_spriteTree);
        while (*sit) {
            const QVariant v = (*sit)->data(0, Qt::UserRole);
            if (v.isValid()) {
                if ((*sit)->checkState(0) == Qt::Checked) {
                    const auto sprite = v.value<SpritePtr>();
                    if (sprite) checkedPaths.insert(sprite->path);
                }
            } else {
                if (!(*sit)->isExpanded())
                    collapsedKeys.insert(treeItemKey(*sit));
            }
            ++sit;
        }
        scrollPos = m_spriteTree->verticalScrollBar()->value();
    }
    // ────────────────────────────────────────────────────────────────────────

    // Clear the filter when refreshing the tree
    if (m_spriteFilterEdit) {
        m_spriteFilterEdit->blockSignals(true);
        m_spriteFilterEdit->clear();
        m_spriteFilterEdit->blockSignals(false);
    }
    if (m_spriteFilterResultLabel) m_spriteFilterResultLabel->setVisible(false);
    if (m_multiSelectionLabel)     m_multiSelectionLabel->setVisible(false);
    m_spriteTree->blockSignals(true);
    m_spriteTree->clear();

    // In Frame Animation workspace the relevant "is there anything to show" check
    // is whether the selected atlas has any packed sprites — not the global
    // activeFramePaths list which doesn't change when switching atlases.
    const bool nothingToShow = !m_session ||
        (m_activeWorkspace == Workspace::FrameAnimation
            ? m_session->activeAtlas().layoutModels.isEmpty()
            : m_session->activeFramePaths.isEmpty());
    if (nothingToShow) {
        m_spriteTree->blockSignals(false);
        return;
    }

    const QIcon folderIcon    = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    const QIcon animGroupIcon = QApplication::style()->standardIcon(QStyle::SP_DirLinkIcon);

    auto makeGroupNode = [&](QTreeWidgetItem* parent, const QString& text) {
        auto* node = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_spriteTree);
        node->setText(0, text);
        node->setIcon(0, folderIcon);
        node->setFlags(node->flags() | Qt::ItemIsUserCheckable);
        node->setCheckState(0, Qt::Unchecked);
        return node;
    };

    using SpriteLeaf = QPair<SpritePtr, QString>;

    // Collect sprites for the navigator tree.
    // Frame Animation: show only the selected atlas's sprites.
    // All other workspaces: show every active sprite across all non-excluded atlases.
    QVector<SpritePtr> allSprites;
    if (m_activeWorkspace == Workspace::FrameAnimation) {
        for (const auto& model : m_session->activeAtlas().layoutModels)
            for (const auto& sp : model.sprites)
                allSprites.append(sp);
    } else {
        QSet<QString> seen;
        for (const auto& atlas : m_session->atlases) {
            if (atlas.isExcluded) continue;
            for (const auto& model : atlas.layoutModels) {
                for (const auto& sp : model.sprites) {
                    if (!seen.contains(sp->path)) {
                        seen.insert(sp->path);
                        allSprites.append(sp);
                    }
                }
            }
        }
    }

    QMap<QString, SpritePtr> spriteByPath;
    for (const SpritePtr& sp : allSprites)
        spriteByPath[sp->path] = sp;

    auto makeLeafCb = [&](QTreeWidgetItem* parent, const QString& path, const QString& leafName) {
        auto* leaf = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_spriteTree);
        leaf->setText(0, leafName);
        leaf->setFlags(leaf->flags() | Qt::ItemIsUserCheckable);
        leaf->setCheckState(0, Qt::Unchecked);
        leaf->setData(0, Qt::UserRole, QVariant::fromValue(spriteByPath.value(path)));
        QPixmap pix(path);
        if (!pix.isNull())
            leaf->setIcon(0, QIcon(pix.scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    };

    // findOrCreateFolderPath is needed for hidden-folder placeholder insertion.
    auto findOrCreateFolderPath = [&](QTreeWidgetItem* root, const QStringList& parts) -> QTreeWidgetItem* {
        QTreeWidgetItem* current = root;
        for (const QString& part : parts) {
            QTreeWidgetItem* found = nullptr;
            int childCount = current ? current->childCount() : m_spriteTree->topLevelItemCount();
            for (int i = 0; i < childCount; ++i) {
                QTreeWidgetItem* child = current ? current->child(i) : m_spriteTree->topLevelItem(i);
                if (child->text(0) == part && !child->data(0, Qt::UserRole).isValid()) {
                    found = child;
                    break;
                }
            }
            if (!found) found = makeGroupNode(current, part);
            current = found;
        }
        return current;
    };

    auto toEntries = [](const QVector<SpriteLeaf>& leaves) -> QVector<QPair<QString, QString>> {
        QVector<QPair<QString, QString>> result;
        result.reserve(leaves.size());
        for (const auto& [sp, name] : leaves)
            result.append({sp->path, name});
        return result;
    };

    if (!m_session->sources.isEmpty()) {
        // One or more sources: group sprites under a top-level source node each.
        // Use longest-prefix matching so a more-specific cachedFolderPath wins.
        QVector<QVector<SpriteLeaf>> perSource(m_session->sources.size());
        QVector<SpriteLeaf> unassigned;

        for (const SpritePtr& sprite : allSprites) {
            const QString cleanedPath = QDir::cleanPath(sprite->path);
            int bestLen = -1;
            int bestIdx = -1;
            for (int si = 0; si < m_session->sources.size(); ++si) {
                const QString& cached = m_session->sources[si].cachedFolderPath;
                if (cached.isEmpty()) continue;
                const QString cleaned = QDir::cleanPath(cached);
                if ((cleanedPath.startsWith(cleaned + QLatin1Char('/'))
                        || cleanedPath == cleaned)
                        && cleaned.length() > bestLen) {
                    bestLen = cleaned.length();
                    bestIdx = si;
                }
            }

            if (bestIdx >= 0) {
                // Compute the display name relative to the source's cached folder.
                // This avoids depending on sprite->name, which may contain the full
                // tmp path structure (e.g. tmp/sprat-gui-XXX/source/...) when derived
                // from an extraction directory that differs from sourceFolder.
                const QString cleanedCache = QDir::cleanPath(
                    m_session->sources[bestIdx].cachedFolderPath);
                QString localName;
                if (cleanedPath.startsWith(cleanedCache + QLatin1Char('/'))) {
                    const QString rel = cleanedPath.mid(cleanedCache.length() + 1);
                    // Skip sprites that are currently excluded from this source.
                    if (m_session->sources[bestIdx].excludedFiles.contains(rel)) continue;
                    const QString dir  = QFileInfo(rel).path();
                    const QString base = QFileInfo(rel).baseName();
                    localName = (dir.isEmpty() || dir == QLatin1String("."))
                                ? base : dir + QLatin1Char('/') + base;
                } else {
                    localName = sprite->name; // fallback
                }
                // Strip hidden folder segments (Hide group only).
                // hiddenFolders stores relative paths within this source.
                const QStringList& hiddenFolders = m_session->sources[bestIdx].hiddenFolders;
                if (!hiddenFolders.isEmpty() && localName.contains('/')) {
                    const QSet<QString> hiddenSet(hiddenFolders.begin(), hiddenFolders.end());
                    const QStringList parts = localName.split('/');
                    QStringList resultParts;
                    QString accRelPath;
                    for (int i = 0; i < parts.size() - 1; ++i) {
                        if (!accRelPath.isEmpty()) accRelPath += '/';
                        accRelPath += parts[i];
                        if (!hiddenSet.contains(accRelPath))
                            resultParts.append(parts[i]);
                    }
                    resultParts.append(parts.last());
                    localName = resultParts.join('/');
                }
                perSource[bestIdx].append({sprite, localName});
            } else {
                unassigned.append({sprite, sprite->name});
            }
        }

        for (int si = 0; si < m_session->sources.size(); ++si) {
            if (perSource[si].isEmpty()) continue;
            const auto& src = m_session->sources[si];
            // When hidden toggle is OFF, show a count badge on the source node.
            const int hiddenCount = src.hiddenFolders.size();
            const QString nodeText = (!m_showHiddenItems && hiddenCount > 0)
                ? tr("%1 (%2 hidden)").arg(src.name).arg(hiddenCount)
                : src.name;
            auto* sourceNode = makeGroupNode(nullptr, nodeText);
            QFont f = sourceNode->font(0);
            f.setBold(true);
            sourceNode->setFont(0, f);
            sourceNode->setData(0, Qt::UserRole + 1, si); // source index for context menu

            // Icon and tooltip reflecting the source type
            QStyle::StandardPixmap pixmap;
            QString typeLabel;
            switch (src.type) {
            case SourceType::Folder:
                pixmap = QStyle::SP_DirOpenIcon;
                typeLabel = tr("Folder");
                break;
            case SourceType::SingleImage:
                pixmap = QStyle::SP_FileIcon;
                typeLabel = tr("Image");
                break;
            case SourceType::Archive:
                pixmap = QStyle::SP_DriveFDIcon;
                typeLabel = tr("Archive");
                break;
            case SourceType::Url:
                pixmap = QStyle::SP_CommandLink;
                typeLabel = tr("URL");
                break;
            }
            sourceNode->setIcon(0, QApplication::style()->standardIcon(pixmap));
            sourceNode->setToolTip(0, typeLabel + ": " + src.originalPath);

            SpriteTreeUtils::buildSubTree(m_spriteTree, sourceNode, toEntries(perSource[si]),
                folderIcon, animGroupIcon, /*checkable=*/true, makeLeafCb);

            // ── Hidden-folder placeholders (only shown when "Hidden" toggle is ON) ──
            if (m_showHiddenItems && !src.hiddenFolders.isEmpty()) {
                const QSet<QString> hiddenSet(src.hiddenFolders.begin(), src.hiddenFolders.end());
                const QColor dimColor = QApplication::palette().color(QPalette::Disabled, QPalette::Text);
                for (const QString& relHidden : src.hiddenFolders) {
                    const QString folderName = QFileInfo(relHidden).fileName();
                    const QString parentRel  = QFileInfo(relHidden).path();

                    // Effective parent: strip other hidden segments from parentRel.
                    QString effectiveParentPath;
                    if (parentRel != QLatin1String(".") && !parentRel.isEmpty()) {
                        const QStringList parentParts = parentRel.split('/');
                        QStringList resultParts;
                        QString acc;
                        for (const QString& part : parentParts) {
                            if (!acc.isEmpty()) acc += '/';
                            acc += part;
                            if (!hiddenSet.contains(acc)) resultParts.append(part);
                        }
                        effectiveParentPath = resultParts.join('/');
                    }

                    QTreeWidgetItem* parentNode = sourceNode;
                    if (!effectiveParentPath.isEmpty())
                        parentNode = findOrCreateFolderPath(sourceNode, effectiveParentPath.split('/'));

                    auto* placeholder = new QTreeWidgetItem(parentNode);
                    placeholder->setText(0, folderName);
                    placeholder->setIcon(0, folderIcon);
                    QFont pf = placeholder->font(0);
                    pf.setItalic(true);
                    placeholder->setFont(0, pf);
                    placeholder->setForeground(0, dimColor);
                    placeholder->setToolTip(0, tr("Hidden — right-click to unhide"));
                    placeholder->setData(0, Qt::UserRole + 2, 1);    // type: hidden-placeholder
                    placeholder->setData(0, Qt::UserRole + 3, si);   // source index
                    placeholder->setData(0, Qt::UserRole + 4, relHidden); // relative path
                    placeholder->setFlags(placeholder->flags() & ~Qt::ItemIsUserCheckable);
                }
            }

            // ── Per-source "Excluded" trash node (always shown when non-empty) ──
            if (!src.excludedFiles.isEmpty()) {
                const QColor dimColor = QApplication::palette().color(QPalette::Disabled, QPalette::Text);
                const int N = src.excludedFiles.size();

                auto* trashNode = new QTreeWidgetItem(sourceNode);
                trashNode->setText(0, tr("Excluded (%1)").arg(N));
                trashNode->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_TrashIcon));
                {
                    QFont tf = trashNode->font(0);
                    tf.setItalic(true);
                    trashNode->setFont(0, tf);
                }
                trashNode->setForeground(0, dimColor);
                trashNode->setToolTip(0, tr("Sprites excluded from layout — right-click to re-include"));
                trashNode->setData(0, Qt::UserRole + 2, 3); // type: excluded-section header
                trashNode->setData(0, Qt::UserRole + 3, si); // source index
                trashNode->setFlags(trashNode->flags() & ~Qt::ItemIsUserCheckable);
                trashNode->setExpanded(true);

                // find-or-create dim, non-checkable folder nodes within the trash subtree
                auto findOrCreateExclFolder = [&](auto& self, QTreeWidgetItem* root, const QStringList& parts) -> QTreeWidgetItem* {
                    Q_UNUSED(self);
                    QTreeWidgetItem* current = root;
                    for (const QString& part : parts) {
                        QTreeWidgetItem* found = nullptr;
                        for (int i = 0; i < current->childCount(); ++i) {
                            QTreeWidgetItem* child = current->child(i);
                            if (child->text(0) == part && !child->data(0, Qt::UserRole).isValid()
                                    && child->data(0, Qt::UserRole + 2).toInt() == 0) {
                                found = child;
                                break;
                            }
                        }
                        if (!found) {
                            found = new QTreeWidgetItem(current);
                            found->setText(0, part);
                            found->setIcon(0, folderIcon);
                            found->setForeground(0, dimColor);
                            found->setFlags(found->flags() & ~Qt::ItemIsUserCheckable);
                            found->setExpanded(true);
                        }
                        current = found;
                    }
                    return current;
                };

                for (const QString& relPath : src.excludedFiles) {
                    const QStringList parts = relPath.split('/');
                    const QString baseName  = QFileInfo(relPath).baseName();

                    QTreeWidgetItem* parent = trashNode;
                    if (parts.size() > 1)
                        parent = findOrCreateExclFolder(findOrCreateExclFolder, trashNode, parts.mid(0, parts.size() - 1));

                    auto* exclItem = new QTreeWidgetItem(parent);
                    exclItem->setText(0, baseName);
                    exclItem->setForeground(0, dimColor);
                    {
                        QFont ef = exclItem->font(0);
                        ef.setItalic(true);
                        exclItem->setFont(0, ef);
                    }
                    exclItem->setToolTip(0, tr("Excluded from layout — right-click to re-include"));
                    exclItem->setData(0, Qt::UserRole + 2, 2);       // type: excluded item
                    exclItem->setData(0, Qt::UserRole + 3, si);      // source index
                    exclItem->setData(0, Qt::UserRole + 4, relPath); // relative path
                    exclItem->setFlags(exclItem->flags() & ~Qt::ItemIsUserCheckable);
                }
            }
        }

        if (!unassigned.isEmpty()) {
            auto* otherNode = makeGroupNode(nullptr, tr("Other"));
            SpriteTreeUtils::buildSubTree(m_spriteTree, otherNode, toEntries(unassigned),
                folderIcon, animGroupIcon, /*checkable=*/true, makeLeafCb);
        }
    } else {
        // Single source (or no source tracking): flat list from all sprites.
        QVector<QPair<QString, QString>> entries;
        entries.reserve(allSprites.size());
        for (const auto& sprite : allSprites)
            entries.append({sprite->path, sprite->name});
        SpriteTreeUtils::buildSubTree(m_spriteTree, nullptr, entries,
            folderIcon, animGroupIcon, /*checkable=*/true, makeLeafCb);
    }

    // ── Restore tree state ───────────────────────────────────────────────────
    {
        QTreeWidgetItemIterator rit(m_spriteTree);
        while (*rit) {
            if ((*rit)->data(0, Qt::UserRole + 2).toInt() > 0) {
                // Hidden/excluded special item: skip (no check state to restore)
                ++rit; continue;
            }
            const QVariant v = (*rit)->data(0, Qt::UserRole);
            if (v.isValid()) {
                // Leaf: restore check state
                const auto sprite = v.value<SpritePtr>();
                if (sprite && checkedPaths.contains(sprite->path))
                    (*rit)->setCheckState(0, Qt::Checked);
            } else {
                // Folder: new folders expand by default; explicitly collapsed ones collapse
                (*rit)->setExpanded(!collapsedKeys.contains(treeItemKey(*rit)));
            }
            ++rit;
        }
        // Propagate leaf check states up to folder tristate nodes
        for (int i = 0; i < m_spriteTree->topLevelItemCount(); ++i)
            updateSpriteTreeFolderCheckState(m_spriteTree->topLevelItem(i));
    }
    // ────────────────────────────────────────────────────────────────────────

    m_spriteTree->sortItems(0, Qt::AscendingOrder);
    m_spriteTree->blockSignals(false);

    if (hadItems)
        m_spriteTree->verticalScrollBar()->setValue(scrollPos);
}

void MainWindow::onAboutClicked() {
    QMessageBox::about(this, tr("About Sprat"),
        "<h3>sprat-gui " SPRAT_GUI_VERSION "</h3>"
        "<p>A spritesheet packing and animation authoring tool.</p>"
        "<p>"
          "<b>Author:</b> Pedro Amaral Couto<br>"
          "<b>License:</b> MIT<br>"
          "<b>CLI version:</b> " SPRAT_CLI_VERSION
        "</p>"
        "<p><a href=\"https://pedroac.itch.io/sprat\">https://pedroac.itch.io/sprat</a></p>"
        "<p><b>AI Assistance</b><br>"
          "Claude (Anthropic), Codex (OpenAI), Gemini (Google)</p>"
        "<p><b>Libraries &amp; Tools</b><br>"
          "Qt 6 &mdash; GUI framework<br>"
          "libarchive &mdash; ZIP/tar archive handling<br>"
          "zlib &mdash; ZIP compression<br>"
          "CMake &mdash; build system</p>"
    );
}

void MainWindow::updateNavigatorAtlasCombo() {
    if (!m_session) return;
    if (m_navigatorPanel) {
        m_navigatorPanel->updateAtlasCombo(m_session->atlases, m_session->activeAtlasIndex);
        return;
    }
    // Fallback: direct combo manipulation (kept for safety)
    if (!m_navigatorAtlasCombo) return;
    m_navigatorAtlasCombo->blockSignals(true);
    m_navigatorAtlasCombo->clear();
    int selectComboIdx = 0;
    for (int i = 0; i < m_session->atlases.size(); ++i) {
        const auto& atlas = m_session->atlases[i];
        if (atlas.isExcluded) continue;
        if (atlas.spritePaths.isEmpty()) continue;  // Hide empty atlases
        if (i == m_session->activeAtlasIndex)
            selectComboIdx = m_navigatorAtlasCombo->count();
        m_navigatorAtlasCombo->addItem(atlas.name, i);  // Store real atlas index as item data
    }
    m_navigatorAtlasCombo->setCurrentIndex(selectComboIdx);
    m_navigatorAtlasCombo->blockSignals(false);
}

// ---------------------------------------------------------------------------
// Atlas sprite move helper
// ---------------------------------------------------------------------------

void MainWindow::moveAtlasSprites(const QStringList& paths, int srcIdx, int tgtIdx)
{
    if (!m_session) return;
    if (srcIdx < 0 || srcIdx >= m_session->atlases.size()) return;
    if (tgtIdx < 0 || tgtIdx >= m_session->atlases.size()) return;

    AtlasEntry& src = m_session->atlases[srcIdx];
    AtlasEntry& tgt = m_session->atlases[tgtIdx];

    QSet<QString> movedNorm;
    for (const QString& p : paths) {
        const QString norm = QFileInfo(p).absoluteFilePath();
        movedNorm.insert(norm);

        // Remove from source using normalized comparison so path-format differences
        // (relative vs absolute, extra separators, etc.) never cause duplicates to linger.
        src.spritePaths.erase(
            std::remove_if(src.spritePaths.begin(), src.spritePaths.end(),
                [&norm](const QString& sp) {
                    return QFileInfo(sp).absoluteFilePath() == norm;
                }),
            src.spritePaths.end());

        // Add to target only if not already present.
        const bool inTgt = std::any_of(tgt.spritePaths.begin(), tgt.spritePaths.end(),
            [&norm](const QString& sp) {
                return QFileInfo(sp).absoluteFilePath() == norm;
            });
        if (!inTgt)
            tgt.spritePaths.append(norm);
    }

    // Bidirectional sync: keep excludedFiles in sync with atlas membership.
    const int excIdx = m_session->excludedAtlasIndex();
    if (tgtIdx == excIdx) {
        for (const QString& p : paths) {
            addToExcludedFiles(p);
            m_session->activeFramePaths.removeAll(p);
        }
        syncExcludedAtlas();
    } else if (srcIdx == excIdx) {
        for (const QString& p : paths) {
            removeFromExcludedFiles(p);
            if (!m_session->activeFramePaths.contains(p))
                m_session->activeFramePaths.append(p);
        }
        syncExcludedAtlas();
    }

    // Strip moved sprites from the source layout models so stale entries never linger.
    for (auto& model : src.layoutModels) {
        QVector<SpritePtr> kept;
        kept.reserve(model.sprites.size());
        for (const auto& s : model.sprites) {
            if (!movedNorm.contains(QFileInfo(s->path).absoluteFilePath()))
                kept.append(s);
        }
        model.sprites = std::move(kept);
    }
}
