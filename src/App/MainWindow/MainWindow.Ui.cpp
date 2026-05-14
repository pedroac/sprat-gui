#include "MainWindow.h"
#include "MainWindowUiState.h"
#include "ResolutionsConfig.h"
#include "ResolutionUtils.h"
#include "AnimationCanvas.h"
#include "CliToolsConfig.h"
#include <QDockWidget>

#include <QAction>
#include <QApplication>
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
#include <QPlainTextEdit>

void MainWindow::setupUi() {
    resize(1400, 860);
    setWindowTitle(tr("Sprat GUI %1").arg(SPRAT_GUI_VERSION));
    setupToolbar();

    // Add a 10px gap between the menu bar and the dock widgets
    QToolBar* topSpacer = new QToolBar(this);
    topSpacer->setObjectName("topSpacer");
    topSpacer->setFixedHeight(10);
    topSpacer->setMovable(false);
    topSpacer->setFloatable(false);
    topSpacer->setAllowedAreas(Qt::TopToolBarArea);
    topSpacer->setStyleSheet("background: transparent; border: none;");
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
    m_welcomeLabel = new QLabel(tr("Drag and drop a folder, image file, archive (zip/tar), or URL"), m_welcomePage);
    m_welcomeLabel->setAlignment(Qt::AlignCenter);
    welcomeLayout->addWidget(m_welcomeLabel);
    m_mainStack->addWidget(m_welcomePage);

    // --- Create Docks ---
    const int groupMargin = 4;
    const int groupTopPadding = 12;
    const int groupBottomMargin = 0;

    // 1. Layout Canvas Dock
    m_canvasDock = new QDockWidget(tr("Layout Canvas"), this);
    m_canvasDock->setObjectName("canvasDock");
    m_canvasDock->setFont(boldFont);
    QWidget* canvasContent = new QWidget(m_canvasDock);
    canvasContent->setStyleSheet("font-weight: normal;");
    QVBoxLayout* canvasLayout = new QVBoxLayout(canvasContent);
    canvasLayout->setContentsMargins(groupMargin, groupTopPadding, groupMargin, groupBottomMargin);

    // Controls
    QWidget* canvasControlsWidget = new QWidget(canvasContent);
    canvasControlsWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    QHBoxLayout* canvasControls = new QHBoxLayout(canvasControlsWidget);
    canvasControls->setContentsMargins(0, 0, 0, 0);
    canvasControls->addWidget(new QLabel(tr("Profile:")));
    m_profileSelectorStack = new QStackedWidget(this);
    m_profileSelectorStack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    QWidget* profileSelectPage = new QWidget(m_profileSelectorStack);
    QHBoxLayout* profileSelectLayout = new QHBoxLayout(profileSelectPage);
    profileSelectLayout->setContentsMargins(0, 0, 0, 0);
    profileSelectLayout->setSpacing(4);
    m_profileCombo = new QComboBox(profileSelectPage);
    m_profileCombo->setToolTip(tr("Select the output layout profile"));
    m_profileCombo->setAccessibleName(tr("Layout profile"));
    connect(m_profileCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onProfileChanged);
    profileSelectLayout->addWidget(m_profileCombo);
    QIcon profileManageIcon = QIcon::fromTheme("document-properties");
    m_manageProfilesBtn = new QPushButton(profileManageIcon, profileManageIcon.isNull() ? tr("Manage") : "", profileSelectPage);
    m_manageProfilesBtn->setToolTip(tr("Manage Profiles"));
    m_manageProfilesBtn->setAccessibleName(tr("Manage profiles"));
    m_manageProfilesBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(m_manageProfilesBtn, &QPushButton::clicked, this, &MainWindow::onManageProfiles);
    profileSelectLayout->addWidget(m_manageProfilesBtn);
    m_profileSelectorStack->addWidget(profileSelectPage);

    QWidget* addProfilesPage = new QWidget(m_profileSelectorStack);
    QHBoxLayout* addProfilesLayout = new QHBoxLayout(addProfilesPage);
    addProfilesLayout->setContentsMargins(0, 0, 0, 0);
    addProfilesLayout->setSpacing(0);
    m_addProfilesBtn = new QPushButton(tr("Add Profiles"), addProfilesPage);
    m_addProfilesBtn->setIcon(QIcon::fromTheme("document-new"));
    connect(m_addProfilesBtn, &QPushButton::clicked, this, &MainWindow::onManageProfiles);
    addProfilesLayout->addWidget(m_addProfilesBtn);
    m_profileSelectorStack->addWidget(addProfilesPage);

    canvasControls->addWidget(m_profileSelectorStack);
    applyConfiguredProfiles(configuredProfiles(), QString());

    canvasControls->addSpacing(8);
    canvasControls->addWidget(new QLabel(tr("Source resolution:")));
    m_sourceResolutionCombo = new QComboBox(canvasControlsWidget);
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
                // Manhatan distance for simple closeness
                int distance = qAbs(w - screenSize.width()) + qAbs(h - screenSize.height());
                if (distance < minDistance) {
                    minDistance = distance;
                    bestIndex = i;
                }
            }
        }
        m_sourceResolutionCombo->setCurrentIndex(bestIndex);
    }

    canvasControls->addWidget(m_sourceResolutionCombo);
    if (!m_sourceResolutionDebounceTimer) {
        m_sourceResolutionDebounceTimer = new QTimer(this);
        m_sourceResolutionDebounceTimer->setSingleShot(true);
        connect(m_sourceResolutionDebounceTimer, &QTimer::timeout, this, [this]() { onRunLayout(); });
    }
    auto scheduleSourceResolutionLayoutRun = [this](int) {
        if (!m_sourceResolutionDebounceTimer) {
            onRunLayout();
            return;
        }
        m_sourceResolutionDebounceTimer->start(350);
    };
    connect(m_sourceResolutionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, scheduleSourceResolutionLayoutRun);

    canvasControls->addStretch();
    canvasControls->addWidget(new QLabel(tr("Zoom:")));
    m_layoutZoomSpin = new QDoubleSpinBox(this);
    m_layoutZoomSpin->setRange(10.0, 800.0);
    m_layoutZoomSpin->setValue(100.0);
    m_layoutZoomSpin->setSuffix("%");
    m_layoutZoomSpin->setSingleStep(10.0);
    connect(m_layoutZoomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onLayoutZoomChanged);
    canvasControls->addWidget(m_layoutZoomSpin);

    canvasLayout->addWidget(canvasControlsWidget, 0, Qt::AlignTop);

    m_canvas = new LayoutCanvas(this);
    canvasLayout->addWidget(m_canvas);
    connect(m_canvas, &LayoutCanvas::spriteSelected, this, &MainWindow::onSpriteSelected);
    connect(m_canvas, &LayoutCanvas::selectionChanged, this, [this](const QList<SpritePtr>& selection) {
        m_session->selectedSprites = selection;
    });
    connect(m_canvas, &LayoutCanvas::zoomChanged, this, [this](double zoom) {
        m_layoutZoomSpin->blockSignals(true);
        m_layoutZoomSpin->setValue(zoom * 100.0);
        m_layoutZoomSpin->blockSignals(false);
    });
    connect(m_canvas, &LayoutCanvas::requestTimelineGeneration, this, &MainWindow::onGenerateTimelinesFromFrames);
    connect(m_canvas, &LayoutCanvas::externalPathDropped, this, &MainWindow::onLayoutCanvasPathDropped);
    connect(m_canvas, &LayoutCanvas::addFramesRequested, this, &MainWindow::onAddFramesRequested);
    connect(m_canvas, &LayoutCanvas::removeFramesRequested, this, &MainWindow::onRemoveFramesRequested);
    connect(m_canvas, &LayoutCanvas::splitSpriteRequested, this, &MainWindow::onSplitSpriteRequested);

    m_canvasDock->setWidget(canvasContent);
    addDockWidget(Qt::LeftDockWidgetArea, m_canvasDock);

    // 2. Timelines Dock
    m_timelineDock = new QDockWidget(tr("Animation Timelines"), this);
    m_timelineDock->setObjectName("timelineDock");
    m_timelineDock->setFont(boldFont);
    QWidget* timelineContent = new QWidget(m_timelineDock);
    timelineContent->setStyleSheet("font-weight: normal;");
    QVBoxLayout* timelineLayout = new QVBoxLayout(timelineContent);
    timelineLayout->setContentsMargins(groupMargin, groupTopPadding, groupMargin, groupBottomMargin);

    QHBoxLayout* timelineAddLayout = new QHBoxLayout();
    timelineAddLayout->addWidget(new QLabel(tr("Name:")));
    m_timelineCreateEdit = new QLineEdit(this);
    m_timelineCreateEdit->setPlaceholderText(tr("Timeline name (optional)"));
    timelineAddLayout->addWidget(m_timelineCreateEdit);
    QPushButton* addTimelineBtn = new QPushButton(QIcon::fromTheme("list-add"), tr("Add"), this);
    connect(addTimelineBtn, &QPushButton::clicked, this, &MainWindow::onTimelineAddClicked);
    connect(m_timelineCreateEdit, &QLineEdit::returnPressed, this, &MainWindow::onTimelineAddClicked);
    timelineAddLayout->addWidget(addTimelineBtn);
    timelineLayout->addLayout(timelineAddLayout);

    m_timelineList = new QListWidget(this);
    m_timelineList->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_timelineList->setIconSize(QSize(32, 32));
    connect(m_timelineList, &QListWidget::itemSelectionChanged, this, &MainWindow::onTimelineSelectionChanged);
    timelineLayout->addWidget(m_timelineList, 1); // Give it a stretch factor
    m_timelineList->setVisible(false);

    // Add a gap between the list and the editor
    timelineLayout->addSpacing(8);

    m_timelineEditorContainer = new QWidget(this);
    m_timelineEditorContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    QVBoxLayout* editorContainerLayout = new QVBoxLayout(m_timelineEditorContainer);
    editorContainerLayout->setContentsMargins(0, 0, 0, 0);
    timelineLayout->addWidget(m_timelineEditorContainer, 0); // No stretch for the editor area
    m_timelineEditorContainer->setVisible(false);

    m_selectedTimelineGroup = new QGroupBox(tr("Selected Timeline"), m_timelineEditorContainer);
    QVBoxLayout* groupLayout = new QVBoxLayout(m_selectedTimelineGroup);
    editorContainerLayout->addWidget(m_selectedTimelineGroup);

    QHBoxLayout* timelineNameLayout = new QHBoxLayout();
    timelineNameLayout->addWidget(new QLabel(tr("Name:")));
    m_timelineNameEdit = new QLineEdit(this);
    m_timelineNameEdit->setEnabled(false);
    connect(m_timelineNameEdit, &QLineEdit::editingFinished, this, &MainWindow::onTimelineNameChanged);
    timelineNameLayout->addWidget(m_timelineNameEdit);
    timelineNameLayout->addWidget(new QLabel(tr("FPS:")));
    m_timelineFpsSpin = new QSpinBox(this);
    m_timelineFpsSpin->setRange(1, 60);
    m_timelineFpsSpin->setValue(8);
    m_timelineFpsSpin->setEnabled(false);
    m_timelineFpsSpin->setToolTip(tr("Frames per second for animation playback"));
    m_timelineFpsSpin->setAccessibleName(tr("Timeline FPS"));
    connect(m_timelineFpsSpin, &QSpinBox::valueChanged, this, &MainWindow::onTimelineFpsChanged);
    timelineNameLayout->addWidget(m_timelineFpsSpin);
    QPushButton* removeTimelineBtn = new QPushButton(QIcon::fromTheme("list-remove"), tr("Remove"), this);
    connect(removeTimelineBtn, &QPushButton::clicked, this, &MainWindow::onTimelineRemoveClicked);
    timelineNameLayout->addWidget(removeTimelineBtn);
    groupLayout->addLayout(timelineNameLayout);

    m_timelineDropArea = new QWidget(this); // No longer a group box, replaced by Selected Timeline group
    QVBoxLayout* dropAreaLayout = new QVBoxLayout(m_timelineDropArea);
    dropAreaLayout->setContentsMargins(0, 4, 0, 0);
    m_timelineDragHintLabel = new QLabel(tr("Drag frames from layout canvas here"), m_timelineDropArea);
    dropAreaLayout->addWidget(m_timelineDragHintLabel);
    m_timelineFramesList = new TimelineListWidget(m_timelineDropArea);
    m_timelineFramesList->setViewMode(QListWidget::IconMode);
    m_timelineFramesList->setFlow(QListWidget::LeftToRight);
    m_timelineFramesList->setWrapping(false);
    m_timelineFramesList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_timelineFramesList->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_timelineFramesList->setResizeMode(QListWidget::Adjust);
    m_timelineFramesList->setIconSize(QSize(48, 48));
    m_timelineFramesList->setFixedHeight(80); // Reduced height for the tape
    connect(m_timelineFramesList, &TimelineListWidget::frameDropped, this, &MainWindow::onFrameDropped);
    connect(m_timelineFramesList, &TimelineListWidget::frameMoved, this, &MainWindow::onFrameMoved);
    connect(m_timelineFramesList, &TimelineListWidget::removeSelectedRequested, this, &MainWindow::onFrameRemoveRequested);
    connect(m_timelineFramesList, &TimelineListWidget::duplicateFrameRequested, this, &MainWindow::onFrameDuplicateRequested);
    connect(m_timelineFramesList, &QListWidget::itemSelectionChanged, this, &MainWindow::onTimelineFrameSelectionChanged);
    dropAreaLayout->addWidget(m_timelineFramesList);
    groupLayout->addWidget(m_timelineDropArea);
    timelineLayout->addStretch(0);

    m_timelineDock->setWidget(timelineContent);
    addDockWidget(Qt::LeftDockWidgetArea, m_timelineDock);

    // 3. Selected Frame Editor Dock
    m_editorDock = new QDockWidget(tr("Selected Frame Editor"), this);
    m_editorDock->setObjectName("editorDock");
    m_editorDock->setFont(boldFont);
    QWidget* editorContent = new QWidget(m_editorDock);
    editorContent->setStyleSheet("font-weight: normal;");
    QVBoxLayout* editorLayoutBox = new QVBoxLayout(editorContent);
    editorLayoutBox->setContentsMargins(groupMargin, groupTopPadding, groupMargin, groupBottomMargin);

    QHBoxLayout* nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel(tr("Name:")));
    m_spriteNameEdit = new QLineEdit(this);
    m_spriteNameEdit->setEnabled(false);
    nameRow->addWidget(m_spriteNameEdit);

    nameRow->addWidget(new QLabel(tr("Zoom:")));
    m_previewZoomSpin = new QDoubleSpinBox(this);
    m_previewZoomSpin->setRange(10.0, 1600.0);
    m_previewZoomSpin->setValue(200.0);
    m_previewZoomSpin->setSuffix("%");
    m_previewZoomSpin->setSingleStep(10.0);
    nameRow->addWidget(m_previewZoomSpin);

    editorLayoutBox->addLayout(nameRow);

    QHBoxLayout* pivotRow = new QHBoxLayout();
    pivotRow->addWidget(new QLabel(tr("Handle:")));
    m_handleCombo = new QComboBox(this);
    m_handleCombo->addItem(tr("pivot"));
    m_handleCombo->setToolTip(tr("Select pivot or a named marker to edit"));
    m_handleCombo->setAccessibleName(tr("Handle selector"));
    pivotRow->addWidget(m_handleCombo);
    connect(m_handleCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onHandleComboChanged);
    pivotRow->addWidget(new QLabel(tr("X:")));
    m_pivotXSpin = new QSpinBox(this);
    m_pivotXSpin->setEnabled(false);
    m_pivotXSpin->setRange(0, 9999);
    m_pivotXSpin->setToolTip(tr("Pivot X: horizontal origin for sprite rotation"));
    m_pivotXSpin->setAccessibleName(tr("Pivot X"));
    connect(m_pivotXSpin, &QSpinBox::editingFinished, this, &MainWindow::onPivotSpinChanged);
    pivotRow->addWidget(m_pivotXSpin);
    pivotRow->addWidget(new QLabel(tr("Y:")));
    m_pivotYSpin = new QSpinBox(this);
    m_pivotYSpin->setEnabled(false);
    m_pivotYSpin->setRange(0, 9999);
    m_pivotYSpin->setToolTip(tr("Pivot Y: vertical origin for sprite rotation"));
    m_pivotYSpin->setAccessibleName(tr("Pivot Y"));
    connect(m_pivotYSpin, &QSpinBox::editingFinished, this, &MainWindow::onPivotSpinChanged);
    pivotRow->addWidget(m_pivotYSpin);
    QIcon manageIcon = QIcon::fromTheme("document-properties");
    m_configPointsBtn = new QPushButton(manageIcon, manageIcon.isNull() ? tr("Manage") : "", this);
    m_configPointsBtn->setToolTip(tr("Manage Markers"));
    m_configPointsBtn->setAccessibleName(tr("Configure markers"));
    connect(m_configPointsBtn, &QPushButton::clicked, this, &MainWindow::onPointsConfigClicked);
    pivotRow->addWidget(m_configPointsBtn);
    m_configPointsBtn->setEnabled(false);
    pivotRow->addStretch();
    editorLayoutBox->addLayout(pivotRow);

    m_previewView = new PreviewCanvas(this);
    connect(m_previewView, &PreviewCanvas::pivotChanged, this, &MainWindow::onCanvasPivotChanged);
    connect(m_previewView->overlay(), &EditorOverlayItem::markerSelected, this, &MainWindow::onMarkerSelectedFromCanvas);
    connect(m_previewView->overlay(), &EditorOverlayItem::markerChanged, this, &MainWindow::onMarkerChangedFromCanvas);
    connect(m_previewView->overlay(), &EditorOverlayItem::applyPivotToSelectedFramesRequested, this, &MainWindow::onApplyPivotToSelectedTimelineFrames);
    connect(m_previewView->overlay(), &EditorOverlayItem::applyMarkerToSelectedFramesRequested, this, &MainWindow::onApplyMarkerToSelectedTimelineFrames);
    connect(m_previewView, &PreviewCanvas::applyPivotToSelectedFramesRequested, this, &MainWindow::onApplyPivotToSelectedTimelineFrames);
    connect(m_previewView, &PreviewCanvas::applyMarkerToSelectedFramesRequested, this, &MainWindow::onApplyMarkerToSelectedTimelineFrames);
    connect(m_previewView, &PreviewCanvas::zoomChanged, this, [this](double zoom) {
        m_previewZoomSpin->blockSignals(true);
        m_previewZoomSpin->setValue(zoom * 100.0);
        m_previewZoomSpin->blockSignals(false);
    });
    connect(m_previewZoomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onPreviewZoomChanged);
    editorLayoutBox->addWidget(m_previewView);

    m_editorDock->setWidget(editorContent);
    addDockWidget(Qt::RightDockWidgetArea, m_editorDock);

    // 4. Animation Test Dock
    m_animDock = new QDockWidget(tr("Animation Test"), this);
    m_animDock->setObjectName("animDock");
    m_animDock->setFont(boldFont);
    QWidget* animContent = new QWidget(m_animDock);
    animContent->setStyleSheet("font-weight: normal;");
    QVBoxLayout* animLayout = new QVBoxLayout(animContent);
    animLayout->setContentsMargins(groupMargin, groupTopPadding, groupMargin, groupBottomMargin);

    QHBoxLayout* animControls = new QHBoxLayout();
    m_animPrevBtn = new QPushButton(tr("◀◀"));
    m_animPrevBtn->setIcon(QIcon::fromTheme("media-skip-backward"));
    m_animPrevBtn->setToolTip(tr("Step to previous frame"));
    m_animPrevBtn->setAccessibleName(tr("Previous frame"));
    connect(m_animPrevBtn, &QPushButton::clicked, this, &MainWindow::onAnimPrevClicked);
    animControls->addWidget(m_animPrevBtn);
    m_animPlayPauseBtn = new QPushButton(tr("▶"));
    m_animPlayPauseBtn->setIcon(QIcon::fromTheme("media-playback-start"));
    m_animPlayPauseBtn->setToolTip(tr("Play or pause animation"));
    m_animPlayPauseBtn->setAccessibleName(tr("Play or pause"));
    connect(m_animPlayPauseBtn, &QPushButton::clicked, this, &MainWindow::onAnimPlayPauseClicked);
    animControls->addWidget(m_animPlayPauseBtn);
    m_animNextBtn = new QPushButton(tr("▶▶"));
    m_animNextBtn->setIcon(QIcon::fromTheme("media-skip-forward"));
    m_animNextBtn->setToolTip(tr("Step to next frame"));
    m_animNextBtn->setAccessibleName(tr("Next frame"));
    connect(m_animNextBtn, &QPushButton::clicked, this, &MainWindow::onAnimNextClicked);
    animControls->addWidget(m_animNextBtn);
    animControls->addStretch();
    animControls->addWidget(new QLabel(tr("Zoom:")));
    m_animZoomSpin = new QDoubleSpinBox(this);
    m_animZoomSpin->setRange(10.0, 1600.0);
    m_animZoomSpin->setValue(200.0);
    m_animZoomSpin->setSuffix("%");
    m_animZoomSpin->setSingleStep(10.0);
    m_animZoomSpin->setToolTip(tr("Zoom level for animation preview"));
    m_animZoomSpin->setAccessibleName(tr("Animation zoom"));
    connect(m_animZoomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onAnimZoomChanged);
    animControls->addWidget(m_animZoomSpin);
    animLayout->addLayout(animControls);

    m_animStatusLabel = new QLabel(tr("Create/select a timeline and drag frames into it."), this);
    m_animStatusLabel->setStyleSheet("color: #808080;");
    animLayout->addWidget(m_animStatusLabel);

    m_animCanvas = new AnimationCanvas(this);
    connect(m_animCanvas, &AnimationCanvas::zoomChanged, this, [this](double zoom) {
        m_animZoomSpin->blockSignals(true);
        m_animZoomSpin->setValue(zoom * 100.0);
        m_animZoomSpin->blockSignals(false);
    });
    animLayout->addWidget(m_animCanvas);

    m_animDock->setWidget(animContent);
    addDockWidget(Qt::RightDockWidgetArea, m_animDock);

    // CLI Log dock
    m_cliLogDock = new QDockWidget(tr("CLI Log"), this);
    m_cliLogDock->setObjectName("cliLogDock");
    m_cliLog = new QPlainTextEdit(this);
    m_cliLog->setReadOnly(true);
    m_cliLog->setMaximumBlockCount(5000);
    m_cliLog->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_cliLogDock->setWidget(m_cliLog);

    // Initial arrangement:
    // Top row: Canvas | Editor
    // Bottom row: Timelines | Anim Test

    // First, clear any default placement and set them explicitly
    addDockWidget(Qt::TopDockWidgetArea, m_canvasDock);
    addDockWidget(Qt::TopDockWidgetArea, m_editorDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_timelineDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_animDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_cliLogDock);

    splitDockWidget(m_canvasDock, m_editorDock, Qt::Horizontal);
    splitDockWidget(m_timelineDock, m_animDock, Qt::Horizontal);
    tabifyDockWidget(m_timelineDock, m_cliLogDock);
    m_timelineDock->raise(); // ensure timeline tab is active by default

    // Ensure the top row is taller than the bottom row
    resizeDocks({m_canvasDock, m_editorDock}, {600, 600}, Qt::Vertical);
    resizeDocks({m_timelineDock, m_animDock}, {260, 260}, Qt::Vertical);

    // Add view menu
    m_viewMenu = menuBar()->addMenu(tr("&View"));
    m_viewMenu->addAction(m_canvasDock->toggleViewAction());
    m_viewMenu->addAction(m_timelineDock->toggleViewAction());
    m_viewMenu->addAction(m_editorDock->toggleViewAction());
    m_viewMenu->addAction(m_animDock->toggleViewAction());
    m_viewMenu->addAction(m_cliLogDock->toggleViewAction());

    // Initially hide docks for welcome page
    m_canvasDock->hide();
    m_timelineDock->hide();
    m_editorDock->hide();
    m_animDock->hide();
    m_cliLogDock->hide();

    setupStatusBarUi();

    updateUiState();
    applySettings();

    setupZoomShortcuts();
}


void MainWindow::setupToolbar() {
    QMenuBar* mainMenuBar = menuBar();
    QMenu* fileMenu = mainMenuBar->addMenu(tr("File"));

    m_loadAction = fileMenu->addAction(tr("Load Images Folder"));
    m_loadAction->setToolTip(tr("Load a folder of sprite images"));
    connect(m_loadAction, &QAction::triggered, this, &MainWindow::onLoadFolder);

    QAction* loadProjectAction = fileMenu->addAction(tr("Load..."));
    loadProjectAction->setToolTip(tr("Load a project or archive file"));
    connect(loadProjectAction, &QAction::triggered, this, &MainWindow::onLoadProject);

    QAction* loadUrlAction = fileMenu->addAction(tr("Load URL..."));
    loadUrlAction->setToolTip(tr("Download and load an image, project, or archive from a URL"));
    connect(loadUrlAction, &QAction::triggered, this, &MainWindow::onLoadFromUrl);

    fileMenu->addSeparator();
    m_saveAction = fileMenu->addAction(tr("Save..."));
    m_saveAction->setEnabled(false);
    m_saveAction->setToolTip(tr("Save the current project (Ctrl+S)"));
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::onSaveClicked);

    fileMenu->addSeparator();
    m_openSourceFolderAction = fileMenu->addAction(tr("Open Sprites Folder"));
    m_openSourceFolderAction->setToolTip(tr("Open the sprites source folder in the file manager"));
    m_openSourceFolderAction->setEnabled(false);
    connect(m_openSourceFolderAction, &QAction::triggered,
            this, &MainWindow::onOpenSourceFolderClicked);

    fileMenu->addSeparator();
    m_recentProjectsMenu = fileMenu->addMenu(tr("Recent"));

    fileMenu->addSeparator();
    QAction* quitAction = fileMenu->addAction(tr("Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &MainWindow::close);

    QMenu* settingsMenu = mainMenuBar->addMenu(tr("Settings"));
    QAction* stylesAction = settingsMenu->addAction(tr("Styles..."));
    stylesAction->setToolTip(tr("Open style settings"));
    connect(stylesAction, &QAction::triggered, this, &MainWindow::onSettingsStylesClicked);

    QAction* spritesheetAction = settingsMenu->addAction(tr("Spritesheet..."));
    spritesheetAction->setToolTip(tr("Open spritesheet settings"));
    connect(spritesheetAction, &QAction::triggered, this, &MainWindow::onSettingsSpritesheetClicked);

    QAction* cliToolsAction = settingsMenu->addAction(tr("CLI Tools..."));
    cliToolsAction->setToolTip(tr("Open CLI tools settings"));
    connect(cliToolsAction, &QAction::triggered, this, &MainWindow::onSettingsCliToolsClicked);

    QAction* manageProfilesAction = settingsMenu->addAction(tr("Manage Profiles..."));
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
    statusBar()->addPermanentWidget(m_statusLabel);
}

void MainWindow::setupZoomShortcuts() {
    auto performZoom = [this](bool zoomIn) {
        if (!m_canvas) {
            return;
        }
        QWidget* fw = QApplication::focusWidget();
        if (!fw) {
            return;
        }

        QDoubleSpinBox* targetSpin = nullptr;
        if (m_canvas && (fw == m_canvas || m_canvas->isAncestorOf(fw))) {
            targetSpin = m_layoutZoomSpin;
        } else if (m_previewView && (fw == m_previewView || m_previewView->isAncestorOf(fw))) {
            targetSpin = m_previewZoomSpin;
        } else if (m_animCanvas && (fw == m_animCanvas || m_animCanvas->isAncestorOf(fw))) {
            targetSpin = m_animZoomSpin;
        }

        if (!targetSpin) {
            return;
        }
        double zoom = targetSpin->value();
        const double scaleFactor = 1.25;
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
            else              { m_canvas->setZoomManual(true);  m_layoutZoomSpin->setValue(100.0); }
        } else if (m_previewView && (fw == m_previewView || m_previewView->isAncestorOf(fw))) {
            if (fitToContent) { m_previewView->setZoomManual(false); m_previewView->initialFit(); }
            else              { m_previewView->setZoomManual(true);  m_previewZoomSpin->setValue(100.0); }
        } else if (m_animCanvas && (fw == m_animCanvas || m_animCanvas->isAncestorOf(fw))) {
            if (fitToContent) { m_animCanvas->setZoomManual(false); m_animCanvas->initialFit(); }
            else              { m_animCanvas->setZoomManual(true);  m_animZoomSpin->setValue(100.0); }
        }
    };

    QShortcut* zoom100 = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_1), this);
    connect(zoom100, &QShortcut::activated, this, [applyZoomPreset]() { applyZoomPreset(false); });

    QShortcut* zoomFit = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this);
    connect(zoomFit, &QShortcut::activated, this, [applyZoomPreset]() { applyZoomPreset(true); });
}

void MainWindow::setupKeyboardShortcuts() {
    // Ctrl+S → Save
    QShortcut* saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, &MainWindow::onSaveClicked);

    // Ctrl+Z → Undo
    QShortcut* undoShortcut = new QShortcut(QKeySequence::Undo, this);
    connect(undoShortcut, &QShortcut::activated, this, &MainWindow::onUndo);

    // Ctrl+Y (or Ctrl+Shift+Z on Mac) → Redo
    QShortcut* redoShortcut = new QShortcut(QKeySequence::Redo, this);
    connect(redoShortcut, &QShortcut::activated, this, &MainWindow::onRedo);

    // Ctrl+V → Import image/file/URL from clipboard
    QShortcut* pasteShortcut = new QShortcut(QKeySequence::Paste, this);
    connect(pasteShortcut, &QShortcut::activated, this, &MainWindow::onPasteImport);
}

void MainWindow::updateUiState() {
    const bool enabled = m_cliReady && !m_isLoading;
    const bool hasModels = m_session && !m_session->layoutModels.isEmpty() && !m_session->layoutModels.first().sprites.isEmpty();
    
    MainWindowUiState::apply(
        m_cliReady,
        m_isLoading,
        hasModels,
        m_loadAction,
        m_profileCombo,
        m_saveAction);

    if (m_manageProfilesBtn) {
        m_manageProfilesBtn->setEnabled(enabled);
    }
    if (m_addProfilesBtn) {
        m_addProfilesBtn->setEnabled(enabled);
    }
    if (m_sourceResolutionCombo) {
        m_sourceResolutionCombo->setEnabled(enabled);
    }

    // Toggle editor page and docks
    if (hasModels) {
        m_mainStack->hide();
        if (m_canvasDock && m_canvasDock->isHidden()) m_canvasDock->show();
        if (m_timelineDock && m_timelineDock->isHidden()) m_timelineDock->show();
        if (m_editorDock && m_editorDock->isHidden()) m_editorDock->show();
        if (m_animDock && m_animDock->isHidden()) m_animDock->show();
    } else {
        m_mainStack->setCurrentIndex(0); // Welcome page
        m_mainStack->show();
        if (m_canvasDock) m_canvasDock->hide();
        if (m_timelineDock) m_timelineDock->hide();
        if (m_editorDock) m_editorDock->hide();
        if (m_animDock) m_animDock->hide();
    }
}

void MainWindow::updateMainContentView() {
    const bool hasLayout = m_session && !m_session->layoutModels.isEmpty() && !m_session->layoutModels.first().sprites.isEmpty();
    m_mainStack->setVisible(!hasLayout);
    if (!hasLayout) {
        m_mainStack->setCurrentIndex(0);
    }
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
    while (m_recentProjects.size() > 5)
        m_recentProjects.removeLast();
    CliToolsConfig::saveRecentProjects(m_recentProjects);
    updateRecentProjectsMenu();
}

void MainWindow::syncPivotSpinsFromSprite() {
    if (!m_session->selectedSprite) return;
    m_pivotXSpin->blockSignals(true);
    m_pivotYSpin->blockSignals(true);
    m_pivotXSpin->setValue(m_session->selectedSprite->pivotX);
    m_pivotYSpin->setValue(m_session->selectedSprite->pivotY);
    m_pivotXSpin->blockSignals(false);
    m_pivotYSpin->blockSignals(false);
    if (m_previewView && m_previewView->overlay())
        m_previewView->overlay()->updateLayout();
}
