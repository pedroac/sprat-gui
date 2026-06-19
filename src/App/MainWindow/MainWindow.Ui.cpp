#include "MainWindow.h"
#include "AnimationPreviewPanel.h"
#include "TimelineEditorPanel.h"
#include "ExportWorkspace.h"
#include "FrameAnimationWorkspace.h"
#include "LayoutCanvas.h"
#include "SpriteEditorPanel.h"
#include "PreviewCanvas.h"
#include "NavigatorPanel.h"
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
#include <QFileDialog>
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
            this, [this]() { switchWorkspace(m_atlasWorkspace); });

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
                    if (m_currentWorkspace == m_exportWorkspace)
                        m_exportWorkspace->setViewport(m_exportLayoutCanvas);
                }
                // Shared refresh: cancel in-progress load, invalidate cache, schedule pack
                if (m_exportCoordinator)
                    m_exportCoordinator->refreshPreview(profile, sf);
            });

    connect(m_exportWorkspace, &ExportWorkspace::atlasExportConfigChanged,
            this, [this](int sessionIdx, AtlasExportConfig cfg) {
                if (!m_session) return;
                if (sessionIdx >= 0 && sessionIdx < m_session->atlases.size())
                    m_session->atlases[sessionIdx].exportConfig = cfg;
            });

    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::atlasExportProfilesChanged,
            this, [this](int atlasIndex, QStringList profiles) {
                if (!m_session || atlasIndex < 0 || atlasIndex >= m_session->atlases.size()) return;
                m_session->atlases[atlasIndex].exportConfig.profiles = profiles;
            });

    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::profilesGlobalChanged,
            this, [this](bool global) {
                m_lastSaveConfig.profilesGlobal = global;
            });

    connect(m_exportWorkspace, &ExportWorkspace::savePresetRequested,
            this, [this](ExportPreset preset) {
                auto it = std::find_if(m_exportPresets.begin(), m_exportPresets.end(),
                    [&](const ExportPreset& p){ return p.name == preset.name; });
                if (it != m_exportPresets.end()) *it = preset;
                else m_exportPresets.append(preset);
                m_exportWorkspace->setPresets(m_exportPresets);
            });

    connect(m_exportWorkspace, &ExportWorkspace::deletePresetRequested,
            this, [this](const QString& name) {
                m_exportPresets.erase(
                    std::remove_if(m_exportPresets.begin(), m_exportPresets.end(),
                        [&](const ExportPreset& p){ return p.name == name; }),
                    m_exportPresets.end());
                m_exportWorkspace->setPresets(m_exportPresets);
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
                    if (m_atlasWorkspace->canvas())
                        m_atlasWorkspace->canvas()->setModels(m_session->atlases[sourceAtlasIndex].layoutModels);
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

    // 1. Atlas workspace — owns canvas, navigator, sprite editor, profile/resolution combos
    m_atlasWorkspace = new AtlasWorkspace(m_session, m_undoStack, &m_settings, &m_cliPaths, this);

    // Populate profile combo now that AtlasWorkspace is created
    applyConfiguredProfiles(configuredProfiles(), QString());
    // Keep profile combo in sync with AtlasesManagementWorkspace
    connect(m_atlasWorkspace->profileCombo(), QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        auto* combo = m_atlasWorkspace->profileCombo();
        if (!combo) return;
        const QString name = combo->currentData().toString();
        if (m_atlasesManagementWorkspace) m_atlasesManagementWorkspace->setSelectedProfile(name);
    });

    // Populate resolution combo (AtlasWorkspace owns the widget; we set options + screen default)
    {
        QStringList resOptions = ResolutionsConfig::loadResolutionOptions();
        if (resOptions.isEmpty()) resOptions << "1024x768";
        if (QScreen* screen = QGuiApplication::primaryScreen()) {
            const QSize screenSize = screen->size();
            int bestIndex = 0, minDist = std::numeric_limits<int>::max();
            for (int i = 0; i < resOptions.size(); ++i) {
                int w, h;
                if (parseResolutionText(resOptions[i], w, h)) {
                    const int d = qAbs(w - screenSize.width()) + qAbs(h - screenSize.height());
                    if (d < minDist) { minDist = d; bestIndex = i; }
                }
            }
            m_currentResolution = resOptions[bestIndex];
            m_atlasWorkspace->setResolutionOptions(resOptions, m_currentResolution);
        } else {
            m_atlasWorkspace->setResolutionOptions(resOptions, resOptions.first());
            m_currentResolution = resOptions.first();
        }
    }

    // Connect AtlasWorkspace resolution change → push undo command + debounce rebuild
    connect(m_atlasWorkspace, &AtlasWorkspace::resolutionChangeRequested,
            this, [this](const QString& requestedRes) {
        if (requestedRes == m_currentResolution) return;
        const QString oldRes = m_currentResolution;
        m_currentResolution = requestedRes;
        m_undoStack->push(new SetSourceResolutionCommand(
            m_atlasWorkspace->sourceResolutionCombo(), oldRes, requestedRes,
            [this]() {
                m_currentResolution = m_atlasWorkspace->sourceResolutionCombo()->currentText();
                scheduleLayoutRebuild();
            }
        ));
        m_atlasWorkspace->sourceResolutionDebounceTimer()->start(AppConstants::kSourceResDebounceMs);
    });

    // Debounce timer fires → schedule layout rebuild
    connect(m_atlasWorkspace->sourceResolutionDebounceTimer(), &QTimer::timeout,
            this, [this]() { scheduleLayoutRebuild(); });

    // m_layoutZoom (double) tracks the current layout canvas zoom percentage.

    // AtlasesManagementWorkspace — resolution and profile wiring
    m_atlasesManagementWorkspace->setResolutionOptions(
        ResolutionsConfig::loadResolutionOptions(), m_currentResolution);
    // Keep resolution combos in sync (sourceResolutionCombo ↔ AtlasesManagement)
    connect(m_atlasWorkspace->sourceResolutionCombo(), QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() {
        if (m_atlasesManagementWorkspace)
            m_atlasesManagementWorkspace->setCurrentResolution(m_atlasWorkspace->sourceResolutionCombo()->currentText());
    });
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::resolutionChanged,
            this, [this](const QString& res) {
        auto* combo = m_atlasWorkspace->sourceResolutionCombo();
        const int idx = combo->findText(res);
        if (idx >= 0) combo->setCurrentIndex(idx);
    });
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::selectedProfileChanged,
            this, [this](const QString& name) {
        auto* combo = m_atlasWorkspace->profileCombo();
        if (!combo) return;
        const int idx = combo->findData(name);
        if (idx >= 0 && idx != combo->currentIndex())
            combo->setCurrentIndex(idx);
    });
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::manageProfilesRequested,
            this, &MainWindow::onManageProfiles);
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::zoomChanged,
            this, &MainWindow::onLayoutZoomChanged);
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::viewModeChanged,
            this, [this](AtlasesManagementWorkspace::ViewMode mode) {
        if (mode == AtlasesManagementWorkspace::ViewMode::Layout) {
            m_atlasesManagementWorkspace->setCanvasWidget(m_atlasWorkspace->canvas());
            m_atlasesManagementWorkspace->setZoom(m_layoutZoom);
            if (!m_session) return;
            // Rebuild so the canvas reflects the currently selected atlas (it may have
            // changed while Navigation mode was active without triggering a layout run).
            scheduleLayoutRebuild(true);
        } else {
            // Clear dim filter before returning the canvas to the Sprites workspace.
            auto* canvas = m_atlasWorkspace->canvas();
            if (canvas) canvas->setDimFilter(QString());
            m_atlasesManagementWorkspace->clearCanvasWidget();
            // Re-parent the canvas back to its original dock container so it
            // is not left as a floating top-level window blocking the menu bar.
            auto* atlasViewStack = m_atlasWorkspace->atlasViewStack();
            if (canvas && atlasViewStack && atlasViewStack->widget(0)) {
                QWidget* cc = atlasViewStack->widget(0);
                canvas->setParent(cc);
                if (auto* l = cc->layout()) l->addWidget(canvas);
                canvas->show();
            }
        }
    });
    connect(m_atlasesManagementWorkspace, &AtlasesManagementWorkspace::layoutFilterChanged,
            this, [this](const QString& query) {
        if (m_atlasWorkspace->canvas()) m_atlasWorkspace->canvas()->setDimFilter(query);
    });

    // Install event filter on canvas viewport (handled by MainWindow::eventFilter for zoom/scroll)
    m_atlasWorkspace->canvas()->viewport()->installEventFilter(this);

    // Connect AtlasWorkspace signals that need MainWindow context
    connect(m_atlasWorkspace, &AtlasWorkspace::spriteSelected,
            this, &MainWindow::onSpriteSelected);
    connect(m_atlasWorkspace, &AtlasWorkspace::canvasSelectionChanged,
            this, [this](const QList<SpritePtr>&) { updateOnionSkinDisplay(); });
    connect(m_atlasWorkspace, &AtlasWorkspace::canvasZoomChanged,
            this, [this](double pct) {
        m_layoutZoom = pct;
        if (m_atlasesManagementWorkspace)
            m_atlasesManagementWorkspace->setZoom(pct);
    });
    connect(m_atlasWorkspace->canvas(), &LayoutCanvas::externalPathDropped,
            this, &MainWindow::onLayoutCanvasPathDropped);
    connect(m_atlasWorkspace->canvas(), &LayoutCanvas::addFramesRequested,
            this, &MainWindow::onCanvasAddFramesRequested);
    connect(m_atlasWorkspace->canvas(), &LayoutCanvas::removeFramesRequested,
            this, &MainWindow::onCanvasRemoveFramesRequested);
    connect(m_atlasWorkspace->canvas(), &LayoutCanvas::splitSpriteRequested,
            this, &MainWindow::onSplitSpriteRequested);
    connect(m_atlasWorkspace, &AtlasWorkspace::layoutRebuildNeeded,
            this, [this](bool immediate) { scheduleLayoutRebuild(immediate); });
    connect(m_atlasWorkspace->canvas(), &LayoutCanvas::userInteractionStarted,
            this, &MainWindow::pauseLayoutRebuild);
    connect(m_atlasWorkspace->canvas(), &LayoutCanvas::userInteractionEnded,
            this, &MainWindow::resumeLayoutRebuild);
    connect(m_atlasWorkspace->canvas(), &LayoutCanvas::requestTimelineGeneration,
            this, &MainWindow::onGenerateTimelinesFromFrames);
    connect(m_atlasWorkspace, &AtlasWorkspace::statusMessage,
            this, [this](const QString& text) { if (m_statusLabel) m_statusLabel->setText(text); });
    connect(m_atlasWorkspace, &AtlasWorkspace::spriteDataChanged,
            this, &MainWindow::updateOnionSkinDisplay);
    connect(m_atlasWorkspace, &AtlasWorkspace::profileChangeRequested,
            this, [this](const QString&) { onProfileChanged(); });
    connect(m_atlasWorkspace->addProfilesBtn(), &QPushButton::clicked,
            this, &MainWindow::onManageProfiles);
    connect(m_atlasWorkspace->navigatorPanel(), &NavigatorPanel::atlasIndexChanged,
            this, [this](int atlasIndex) {
        if (!m_session) return;
        if (atlasIndex >= 0 && atlasIndex < m_session->atlases.size()) {
            m_session->activeAtlasIndex = atlasIndex;
            refreshSpriteTree();
            refreshTimelineList();
            refreshAnimationTest();
            scheduleLayoutRebuild(true);
        } else {
            refreshSpriteTree();
        }
    });
    connect(m_atlasWorkspace, &AtlasWorkspace::showHiddenToggled,
            this, [this](bool) { refreshSpriteTree(); });
    connect(m_atlasWorkspace, &AtlasWorkspace::deleteFramesRequested,
            this, &MainWindow::onNavigatorDeleteFrames);
    connect(m_atlasWorkspace->navigatorPanel(), &NavigatorPanel::excludeKeyPressed,
            this, &MainWindow::onNavigatorExcludeKey);
    connect(m_atlasWorkspace, &AtlasWorkspace::addSmartFolderRequested,
            this, &MainWindow::onNavigatorAddSmartFolder);
    connect(m_atlasWorkspace, &AtlasWorkspace::addFramesToFolderRequested,
            this, &MainWindow::onNavigatorAddFrames);
    connect(m_atlasWorkspace, &AtlasWorkspace::addToTimelineRequested,
            this, &MainWindow::onNavigatorAddToTimeline);
    connect(m_atlasWorkspace, &AtlasWorkspace::createTimelineRequested,
            this, &MainWindow::onNavigatorCreateTimeline);
    connect(m_atlasWorkspace, &AtlasWorkspace::createGroupRequested,
            this, &MainWindow::onNavigatorCreateGroup);
    connect(m_atlasWorkspace, &AtlasWorkspace::deleteGroupRequested,
            this, &MainWindow::onNavigatorDeleteGroup);
    connect(m_atlasWorkspace, &AtlasWorkspace::autoCreateTimelinesForSourceRequested,
            this, &MainWindow::onNavigatorAutoCreateTimelinesForSource);
    connect(m_atlasWorkspace->navigatorPanel(), &NavigatorPanel::addSourceFolderRequested,
            this, &MainWindow::onLoadFolder);
    connect(m_atlasWorkspace->navigatorPanel(), &NavigatorPanel::addSourceImageRequested,
            this, &MainWindow::onAddSourceFile);
    connect(m_atlasWorkspace->navigatorPanel(), &NavigatorPanel::addSourceArchiveRequested, this, [this]() {
        const QString filter = tr("Archives (*.zip *.tar *.tar.gz *.tar.bz2 *.tar.xz)");
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Add Archive"), m_session ? m_session->currentFolder : QString(), filter);
        if (!path.isEmpty()) {
            const DropAction action = confirmDropAction(path);
            if (action != DropAction::Cancel) loadProject(path, action);
        }
    });
    connect(m_atlasWorkspace->navigatorPanel(), &NavigatorPanel::addSourceUrlRequested,
            this, &MainWindow::onLoadFromUrl);
    connect(m_atlasWorkspace, &AtlasWorkspace::editAliasesRequested,
            this, [this](SpritePtr) { onEditAliases(); });
    connect(m_atlasWorkspace, &AtlasWorkspace::onionSkinToggled,
            this, &MainWindow::updateOnionSkinDisplay);
    connect(m_atlasWorkspace, &AtlasWorkspace::spriteDroppedToTimeline,
            this, &MainWindow::onSpritesDroppedToTimeline);

    // 2. Animation Timelines panel
    auto* timelineEditorPanel = new TimelineEditorPanel(m_session, m_undoStack, this);

    // TimelineEditorPanel → animation playback connections are wired in FrameAnimationWorkspace ctor.
    // Non-animation signals stay here:
    connect(timelineEditorPanel, &TimelineEditorPanel::statusMessage, this,
        [this](const QString& text) { if (m_statusLabel) m_statusLabel->setText(text); });
    connect(timelineEditorPanel, &TimelineEditorPanel::spritesToTimelineRequested,
        this, &MainWindow::onSpritesDroppedToTimeline);

    // The panel widget itself is never placed in any layout — its sub-widgets are
    // distributed directly into the animation dock. Hide it so it doesn't sit as
    // an invisible 100×30 overlay on top of the menu bar.
    timelineEditorPanel->hide();

    // 3. Animation Preview panel
    auto* animPreviewPanel = new AnimationPreviewPanel(this);

    // Playback buttons / zoom / overlay toggle are wired in FrameAnimationWorkspace ctor.
    connect(animPreviewPanel->animCanvas()->overlay(), &EditorOverlayItem::pivotChanged,
            m_atlasWorkspace, &AtlasWorkspace::onCanvasPivotChanged);
    connect(animPreviewPanel->animCanvas()->overlay(), &EditorOverlayItem::markerSelected,
            m_atlasWorkspace, &AtlasWorkspace::onMarkerSelectedFromCanvas);
    connect(animPreviewPanel->animCanvas()->overlay(), &EditorOverlayItem::markerChanged,
            m_atlasWorkspace, &AtlasWorkspace::onMarkerChangedFromCanvas);
    // The animation overlay is locked: only the handle shown in the combo can be dragged.
    animPreviewPanel->animCanvas()->overlay()->setLockToActiveHandle(true);
    // 5. Frame Animation workspace — drives playback, export, and timeline state.
    // Overlay/onion-skin button connections and prev/next/play/zoom are wired internally.
    m_frameAnimWorkspace = new FrameAnimationWorkspace(
        m_atlasWorkspace->navigatorPanel(), animPreviewPanel,
        timelineEditorPanel, m_session, &m_settings, this);
    m_frameAnimWorkspace->hide();

    connect(m_frameAnimWorkspace, &FrameAnimationWorkspace::spriteSelectionRequested,
            this, &MainWindow::onSpriteSelected);
    connect(m_frameAnimWorkspace, &FrameAnimationWorkspace::markerSelectionChanged,
            this, [this](const QString& name) {
        auto* pv = m_atlasWorkspace ? m_atlasWorkspace->spriteEditorPanel()->previewCanvas() : nullptr;
        if (pv && pv->overlay()) pv->overlay()->setSelectedMarker(name);
        auto* hc = m_atlasWorkspace ? m_atlasWorkspace->spriteEditorPanel()->handleCombo() : nullptr;
        if (hc) {
            hc->blockSignals(true);
            const int idx = name.isEmpty() ? 0 : hc->findText(name);
            hc->setCurrentIndex(idx != -1 ? idx : 0);
            hc->blockSignals(false);
        }
    });
    connect(m_atlasWorkspace, &AtlasWorkspace::selectedMarkerChanged,
            m_frameAnimWorkspace, &FrameAnimationWorkspace::onMarkerSelectedFromCanvas);
    connect(m_frameAnimWorkspace, &FrameAnimationWorkspace::statusMessage,
            this, [this](const QString& t) { if (m_statusLabel) m_statusLabel->setText(t); });
    connect(m_frameAnimWorkspace, &FrameAnimationWorkspace::loadingStateChanged,
            this, &MainWindow::setLoading);

    // 6. CLI Log panel
    QWidget* cliLogContent = new QWidget(this);
    cliLogContent->setStyleSheet("font-weight: normal;");
    QVBoxLayout* cliLogLayout = new QVBoxLayout(cliLogContent);
    cliLogLayout->setContentsMargins(4, 12, 4, 0);
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
    m_atlasDock->setWidget(m_atlasWorkspace);

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
        animBottomLayout->addWidget(m_frameAnimWorkspace->timelinePanel()->timelineEditorContainer());
        animBottomLayout->addWidget(m_frameAnimWorkspace->animPanel(), 1);

        auto* animSplitter = new QSplitter(Qt::Vertical, m_animationDock);
        animSplitter->addWidget(m_frameAnimWorkspace->timelinePanel()->listAreaWidget());
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
            this, [this]() { switchWorkspace(m_atlasesManagementWorkspace); });

    m_atlasWorkspaceAction = m_viewMenu->addAction(tr("Sprites"));
    m_atlasWorkspaceAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_A));
    m_atlasWorkspaceAction->setCheckable(true);
    m_atlasWorkspaceAction->setChecked(true);
    workspaceGroup->addAction(m_atlasWorkspaceAction);
    connect(m_atlasWorkspaceAction, &QAction::triggered, this, [this]() { switchWorkspace(m_atlasWorkspace); });

    m_frameAnimWorkspaceAction = m_viewMenu->addAction(tr("Frame Animation"));
    m_frameAnimWorkspaceAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F));
    m_frameAnimWorkspaceAction->setCheckable(true);
    workspaceGroup->addAction(m_frameAnimWorkspaceAction);
    connect(m_frameAnimWorkspaceAction, &QAction::triggered, this, [this]() { switchWorkspace(m_frameAnimWorkspace); });

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

    // Initial workspace — Atlas workspace is always the starting point
    m_currentWorkspace = m_atlasWorkspace;

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
        if (m_atlasWorkspace) m_atlasWorkspace->clearCoordinateOverride();
        m_undoStack->undo();
    });
    connect(m_undoStack, &QUndoStack::canUndoChanged, undoAction, &QAction::setEnabled);

    QAction* redoAction = editMenu->addAction(
        style->standardIcon(QStyle::SP_ArrowForward), tr("&Redo"));
    redoAction->setIcon(style->standardIcon(QStyle::SP_ArrowForward));
    redoAction->setShortcut(QKeySequence::Redo);
    redoAction->setEnabled(m_undoStack->canRedo());
    connect(redoAction, &QAction::triggered, this, [this]() {
        if (m_atlasWorkspace) m_atlasWorkspace->clearCoordinateOverride();
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
    // Separator line + vertical padding so the bar doesn't sit flush at the window edge
    statusBar()->setStyleSheet(QStringLiteral(
        "QStatusBar { border-top: 1px solid palette(mid); padding: 2px 0px; }"));

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
        auto* canvas = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
        auto* previewView = m_atlasWorkspace ? m_atlasWorkspace->spriteEditorPanel()->previewCanvas() : nullptr;
        auto* ap = m_frameAnimWorkspace ? m_frameAnimWorkspace->animPanel() : nullptr;
        auto* animCanvas = ap ? ap->animCanvas() : nullptr;
        if (canvas && (fw == canvas || canvas->isAncestorOf(fw))) {
            double zoom = qBound(10.0, zoomIn ? m_layoutZoom * scaleFactor : m_layoutZoom / scaleFactor, 800.0);
            m_layoutZoom = zoom;
            onLayoutZoomChanged(zoom);
            return;
        }
        QDoubleSpinBox* targetSpin = nullptr;
        if (previewView && (fw == previewView || previewView->isAncestorOf(fw))) {
            targetSpin = m_atlasWorkspace->spriteEditorPanel()->previewZoomSpin();
        } else if (animCanvas && (fw == animCanvas || animCanvas->isAncestorOf(fw))) {
            targetSpin = ap->zoomSpin();
        } else if (m_exportWorkspace && m_currentWorkspace == m_exportWorkspace && (fw == m_exportWorkspace || m_exportWorkspace->isAncestorOf(fw))) {
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
        auto* canvas = m_atlasWorkspace ? m_atlasWorkspace->canvas() : nullptr;
        auto* previewView = m_atlasWorkspace ? m_atlasWorkspace->spriteEditorPanel()->previewCanvas() : nullptr;
        auto* ap2 = m_frameAnimWorkspace ? m_frameAnimWorkspace->animPanel() : nullptr;
        auto* animCanvas = ap2 ? ap2->animCanvas() : nullptr;
        if (canvas && (fw == canvas || canvas->isAncestorOf(fw))) {
            if (fitToContent) { canvas->setZoomManual(false); canvas->initialFit(); }
            else              { m_layoutZoom = 100.0; onLayoutZoomChanged(100.0); }
        } else if (previewView && (fw == previewView || previewView->isAncestorOf(fw))) {
            if (fitToContent) { previewView->setZoomManual(false); previewView->initialFit(); }
            else              { previewView->setZoomManual(true);  m_atlasWorkspace->spriteEditorPanel()->previewZoomSpin()->setValue(100.0); }
        } else if (animCanvas && (fw == animCanvas || animCanvas->isAncestorOf(fw))) {
            if (fitToContent) { animCanvas->setZoomManual(false); animCanvas->initialFit(); }
            else              { animCanvas->setZoomManual(true);  ap2->zoomSpin()->setValue(100.0); }
        } else if (m_exportWorkspace && m_currentWorkspace == m_exportWorkspace && (fw == m_exportWorkspace || m_exportWorkspace->isAncestorOf(fw))) {
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
        m_atlasWorkspace ? m_atlasWorkspace->profileCombo() : nullptr,
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

    if (m_atlasWorkspace && m_atlasWorkspace->addProfilesBtn()) {
        m_atlasWorkspace->addProfilesBtn()->setEnabled(enabled);
    }
    if (m_atlasWorkspace && m_atlasWorkspace->sourceResolutionCombo()) {
        m_atlasWorkspace->sourceResolutionCombo()->setEnabled(enabled);
    }

    // View menu items: enabled once a project is open or sprites are loaded
    const bool hasProject = m_projectController && !m_projectController->projectFilePath().isEmpty();
    const bool viewEnabled = hasProject || hasModels;
    if (m_atlasWorkspaceAction)         m_atlasWorkspaceAction->setEnabled(viewEnabled);
    if (m_frameAnimWorkspaceAction)     m_frameAnimWorkspaceAction->setEnabled(viewEnabled);
    if (m_exportationWorkspaceAction)   m_exportationWorkspaceAction->setEnabled(viewEnabled);

    // Toggle docks and welcome page based on project / sprite state.
    // Skip this block while any full-screen workspace is active.
    if (m_currentWorkspace == m_exportWorkspace || m_currentWorkspace == m_atlasesManagementWorkspace) return;

    if (hasModels) {
        m_mainStack->hide();
        if (m_atlasDock && m_atlasDock->isHidden()) m_atlasDock->show();
        const bool wantsAnim = (m_currentWorkspace == m_frameAnimWorkspace);
        if (m_animationDock) m_animationDock->setVisible(wantsAnim);
        if (m_atlasWorkspace && m_atlasWorkspace->atlasViewStack() && m_atlasWorkspace->atlasViewStack()->currentIndex() == 2)
            m_atlasWorkspace->atlasViewStack()->setCurrentIndex(1); // Navigation
    } else if (hasProject) {
        m_mainStack->hide();
        if (m_atlasDock && m_atlasDock->isHidden()) m_atlasDock->show();
        const bool wantsAnim = (m_currentWorkspace == m_frameAnimWorkspace);
        if (m_animationDock) m_animationDock->setVisible(wantsAnim);
        if (m_atlasWorkspace && m_atlasWorkspace->atlasViewStack()) m_atlasWorkspace->atlasViewStack()->setCurrentIndex(2); // Empty-state placeholder
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

    if (m_currentWorkspace != m_exportWorkspace && m_currentWorkspace != m_atlasesManagementWorkspace) {
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
    auto* navigatorPanel = m_atlasWorkspace ? m_atlasWorkspace->navigatorPanel() : nullptr;
    auto* spriteTree = navigatorPanel ? navigatorPanel->tree() : nullptr;
    if (!spriteTree) return;

    // Keep excluded atlas in sync with sources' excludedFiles before rebuilding.
    syncExcludedAtlas();

    // Refresh the atlases workspace sprite list if it is visible.
    if (m_atlasesManagementWorkspace && m_currentWorkspace == m_atlasesManagementWorkspace)
        m_atlasesManagementWorkspace->refreshSpriteList(m_session->atlases);

    // Delegate the actual tree build to NavigatorPanel when it's available.
    if (navigatorPanel && m_session) {
        int atlasFilter = -1;
        auto* atlasCombo = navigatorPanel->atlasCombo();
        if (m_currentWorkspace == m_frameAnimWorkspace && atlasCombo)
            atlasFilter = atlasCombo->currentData().toInt();
        navigatorPanel->refresh(m_session, m_atlasWorkspace->showHiddenItems(), atlasFilter);
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
    const bool hadItems = spriteTree->invisibleRootItem()->childCount() > 0;
    QSet<QString> collapsedKeys;
    QSet<QString> checkedPaths;
    int scrollPos = 0;
    if (hadItems) {
        QTreeWidgetItemIterator sit(spriteTree);
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
        scrollPos = spriteTree->verticalScrollBar()->value();
    }
    // ────────────────────────────────────────────────────────────────────────

    // Clear the filter when refreshing the tree
    auto* spriteFilterEdit = navigatorPanel ? navigatorPanel->filterEdit() : nullptr;
    auto* spriteFilterResultLabel = navigatorPanel ? navigatorPanel->filterResultLabel() : nullptr;
    auto* multiSelectionLabel = m_atlasWorkspace ? m_atlasWorkspace->spriteEditorPanel()->multiSelectionLabel() : nullptr;
    if (spriteFilterEdit) {
        spriteFilterEdit->blockSignals(true);
        spriteFilterEdit->clear();
        spriteFilterEdit->blockSignals(false);
    }
    if (spriteFilterResultLabel) spriteFilterResultLabel->setVisible(false);
    if (multiSelectionLabel)     multiSelectionLabel->setVisible(false);
    spriteTree->blockSignals(true);
    spriteTree->clear();

    // In Frame Animation workspace the relevant "is there anything to show" check
    // is whether the selected atlas has any packed sprites — not the global
    // activeFramePaths list which doesn't change when switching atlases.
    const bool nothingToShow = !m_session ||
        (m_currentWorkspace == m_frameAnimWorkspace
            ? m_session->activeAtlas().layoutModels.isEmpty()
            : m_session->activeFramePaths.isEmpty());
    if (nothingToShow) {
        spriteTree->blockSignals(false);
        return;
    }

    const QIcon folderIcon    = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    const QIcon animGroupIcon = QApplication::style()->standardIcon(QStyle::SP_DirLinkIcon);

    auto makeGroupNode = [&](QTreeWidgetItem* parent, const QString& text) {
        auto* node = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(spriteTree);
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
    if (m_currentWorkspace == m_frameAnimWorkspace) {
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
        auto* leaf = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(spriteTree);
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
            int childCount = current ? current->childCount() : spriteTree->topLevelItemCount();
            for (int i = 0; i < childCount; ++i) {
                QTreeWidgetItem* child = current ? current->child(i) : spriteTree->topLevelItem(i);
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
            const QString nodeText = (!m_atlasWorkspace->showHiddenItems() && hiddenCount > 0)
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

            SpriteTreeUtils::buildSubTree(spriteTree, sourceNode, toEntries(perSource[si]),
                folderIcon, animGroupIcon, /*checkable=*/true, makeLeafCb);

            // ── Hidden-folder placeholders (only shown when "Hidden" toggle is ON) ──
            if (m_atlasWorkspace->showHiddenItems() && !src.hiddenFolders.isEmpty()) {
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
            SpriteTreeUtils::buildSubTree(spriteTree, otherNode, toEntries(unassigned),
                folderIcon, animGroupIcon, /*checkable=*/true, makeLeafCb);
        }
    } else {
        // Single source (or no source tracking): flat list from all sprites.
        QVector<QPair<QString, QString>> entries;
        entries.reserve(allSprites.size());
        for (const auto& sprite : allSprites)
            entries.append({sprite->path, sprite->name});
        SpriteTreeUtils::buildSubTree(spriteTree, nullptr, entries,
            folderIcon, animGroupIcon, /*checkable=*/true, makeLeafCb);
    }

    // ── Restore tree state ───────────────────────────────────────────────────
    {
        QTreeWidgetItemIterator rit(spriteTree);
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
        for (int i = 0; i < spriteTree->topLevelItemCount(); ++i)
            updateSpriteTreeFolderCheckState(spriteTree->topLevelItem(i));
    }
    // ────────────────────────────────────────────────────────────────────────

    spriteTree->sortItems(0, Qt::AscendingOrder);
    spriteTree->blockSignals(false);

    if (hadItems)
        spriteTree->verticalScrollBar()->setValue(scrollPos);
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
    auto* navigatorPanel = m_atlasWorkspace ? m_atlasWorkspace->navigatorPanel() : nullptr;
    if (navigatorPanel) {
        navigatorPanel->updateAtlasCombo(m_session->atlases, m_session->activeAtlasIndex);
        return;
    }
    // Fallback: direct combo manipulation (kept for safety)
    auto* navigatorAtlasCombo = navigatorPanel ? navigatorPanel->atlasCombo() : nullptr;
    if (!navigatorAtlasCombo) return;
    navigatorAtlasCombo->blockSignals(true);
    navigatorAtlasCombo->clear();
    int selectComboIdx = 0;
    for (int i = 0; i < m_session->atlases.size(); ++i) {
        const auto& atlas = m_session->atlases[i];
        if (atlas.isExcluded) continue;
        if (atlas.spritePaths.isEmpty()) continue;  // Hide empty atlases
        if (i == m_session->activeAtlasIndex)
            selectComboIdx = navigatorAtlasCombo->count();
        navigatorAtlasCombo->addItem(atlas.name, i);  // Store real atlas index as item data
    }
    navigatorAtlasCombo->setCurrentIndex(selectComboIdx);
    navigatorAtlasCombo->blockSignals(false);
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
