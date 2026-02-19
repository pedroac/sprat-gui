#include "MainWindow.h"
#include "MainWindowUiState.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>
#include <QVBoxLayout>

void MainWindow::setupUi() {
    resize(1400, 860);
    setWindowTitle(tr("Sprat GUI - C++"));
    setupToolbar();

    // Central Widget is a Stack
    m_mainStack = new QStackedWidget(this);
    setCentralWidget(m_mainStack);

    m_debugDock = new QDockWidget(tr("Debug"), this);
    m_debugDock->setObjectName("debugDock");
    m_debugDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    m_debugLogEdit = new QTextEdit(m_debugDock);
    m_debugLogEdit->setReadOnly(true);
    m_debugDock->setWidget(m_debugLogEdit);
    addDockWidget(Qt::BottomDockWidgetArea, m_debugDock);
    m_debugDock->hide();
    if (m_showDebugAction) {
        connect(m_debugDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
            if (!m_showDebugAction) {
                return;
            }
            m_showDebugAction->blockSignals(true);
            m_showDebugAction->setChecked(visible);
            m_showDebugAction->blockSignals(false);
        });
    }

    // Page 1: Welcome
    m_welcomePage = new QWidget(this);
    QVBoxLayout* welcomeLayout = new QVBoxLayout(m_welcomePage);
    m_welcomeLabel = new QLabel(tr("Drag and Drop folder with image files"), m_welcomePage);
    m_welcomeLabel->setAlignment(Qt::AlignCenter);
    welcomeLayout->addWidget(m_welcomeLabel);
    m_mainStack->addWidget(m_welcomePage);

    // Page 2: Editor
    m_editorPage = new QWidget(this);
    QHBoxLayout* editorLayout = new QHBoxLayout(m_editorPage);
    editorLayout->setContentsMargins(10, 10, 10, 10);

    QSplitter* mainSplitter = new QSplitter(Qt::Horizontal, m_editorPage);
    editorLayout->addWidget(mainSplitter);

    // --- Left Panel ---
    m_leftSplitter = new QSplitter(Qt::Vertical, mainSplitter);

    // 1. Layout Canvas Frame
    QGroupBox* canvasGroup = new QGroupBox(tr("Layout Canvas"), m_leftSplitter);
    QVBoxLayout* canvasLayout = new QVBoxLayout(canvasGroup);
    canvasLayout->setContentsMargins(10, 10, 10, 10);

    // Controls
    QWidget* canvasControlsWidget = new QWidget(canvasGroup);
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
    connect(m_profileCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onProfileChanged);
    profileSelectLayout->addWidget(m_profileCombo);
    QIcon profileManageIcon = QIcon::fromTheme("preferences-system");
    m_manageProfilesBtn = new QPushButton(profileManageIcon, profileManageIcon.isNull() ? tr("Manage") : "", profileSelectPage);
    m_manageProfilesBtn->setToolTip(tr("Manage Profiles"));
    m_manageProfilesBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(m_manageProfilesBtn, &QPushButton::clicked, this, &MainWindow::onManageProfiles);
    profileSelectLayout->addWidget(m_manageProfilesBtn);
    m_profileSelectorStack->addWidget(profileSelectPage);

    QWidget* addProfilesPage = new QWidget(m_profileSelectorStack);
    QHBoxLayout* addProfilesLayout = new QHBoxLayout(addProfilesPage);
    addProfilesLayout->setContentsMargins(0, 0, 0, 0);
    addProfilesLayout->setSpacing(0);
    m_addProfilesBtn = new QPushButton(tr("Add Profiles"), addProfilesPage);
    connect(m_addProfilesBtn, &QPushButton::clicked, this, &MainWindow::onManageProfiles);
    addProfilesLayout->addWidget(m_addProfilesBtn);
    m_profileSelectorStack->addWidget(addProfilesPage);

    canvasControls->addWidget(m_profileSelectorStack);
    applyConfiguredProfiles(configuredProfiles(), QString());

    canvasControls->addStretch();
    canvasControls->addWidget(new QLabel(tr("Zoom:")));
    m_layoutZoomSpin = new QDoubleSpinBox(this);
    m_layoutZoomSpin->setRange(0.1, 8.0);
    m_layoutZoomSpin->setValue(1.0);
    m_layoutZoomSpin->setSingleStep(0.1);
    connect(m_layoutZoomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onLayoutZoomChanged);
    canvasControls->addWidget(m_layoutZoomSpin);

    canvasLayout->addWidget(canvasControlsWidget, 0, Qt::AlignTop);

    m_canvas = new LayoutCanvas(this);
    canvasLayout->addWidget(m_canvas);
    connect(m_canvas, &LayoutCanvas::spriteSelected, this, &MainWindow::onSpriteSelected);
    connect(m_canvas, &LayoutCanvas::selectionChanged, this, [this](const QList<SpritePtr>& selection) {
        m_selectedSprites = selection;
    });
    connect(m_canvas, &LayoutCanvas::zoomChanged, this, [this](double zoom) {
        m_layoutZoomSpin->blockSignals(true);
        m_layoutZoomSpin->setValue(zoom);
        m_layoutZoomSpin->blockSignals(false);
    });
    connect(m_canvas, &LayoutCanvas::requestTimelineGeneration, this, &MainWindow::onGenerateTimelinesFromFrames);
    connect(m_canvas, &LayoutCanvas::externalPathDropped, this, &MainWindow::onLayoutCanvasPathDropped);
    connect(m_canvas, &LayoutCanvas::addFramesRequested, this, &MainWindow::onAddFramesRequested);
    connect(m_canvas, &LayoutCanvas::removeFramesRequested, this, &MainWindow::onRemoveFramesRequested);

    m_leftSplitter->addWidget(canvasGroup);

    // 2. Timelines Frame
    QGroupBox* timelineGroup = new QGroupBox(tr("Animation Timelines"), m_leftSplitter);
    QVBoxLayout* timelineLayout = new QVBoxLayout(timelineGroup);
    timelineLayout->setContentsMargins(10, 10, 10, 10);

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
    m_timelineList->setFixedHeight(100);
    connect(m_timelineList, &QListWidget::itemSelectionChanged, this, &MainWindow::onTimelineSelectionChanged);
    timelineLayout->addWidget(m_timelineList);
    m_timelineList->setVisible(false);

    m_timelineEditorContainer = new QWidget(this);
    m_timelineEditorContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    QVBoxLayout* editorContainerLayout = new QVBoxLayout(m_timelineEditorContainer);
    editorContainerLayout->setContentsMargins(0, 0, 0, 0);
    timelineLayout->addWidget(m_timelineEditorContainer, 1);
    m_timelineEditorContainer->setVisible(false);

    QHBoxLayout* timelineNameLayout = new QHBoxLayout();
    timelineNameLayout->addWidget(new QLabel(tr("Name:")));
    m_timelineNameEdit = new QLineEdit(this);
    m_timelineNameEdit->setEnabled(false);
    connect(m_timelineNameEdit, &QLineEdit::editingFinished, this, &MainWindow::onTimelineNameChanged);
    timelineNameLayout->addWidget(m_timelineNameEdit);
    QPushButton* removeTimelineBtn = new QPushButton(QIcon::fromTheme("list-remove"), tr("Remove"), this);
    connect(removeTimelineBtn, &QPushButton::clicked, this, &MainWindow::onTimelineRemoveClicked);
    timelineNameLayout->addWidget(removeTimelineBtn);
    editorContainerLayout->addLayout(timelineNameLayout);

    m_timelineDropArea = new QGroupBox(this);
    QVBoxLayout* dropAreaLayout = new QVBoxLayout(m_timelineDropArea);
    m_timelineDragHintLabel = new QLabel(tr("Drag frames from layout canvas here"), m_timelineDropArea);
    dropAreaLayout->addWidget(m_timelineDragHintLabel);
    m_timelineFramesList = new TimelineListWidget(m_timelineDropArea);
    m_timelineFramesList->setViewMode(QListWidget::IconMode);
    m_timelineFramesList->setFlow(QListWidget::LeftToRight);
    m_timelineFramesList->setWrapping(true);
    m_timelineFramesList->setResizeMode(QListWidget::Adjust);
    m_timelineFramesList->setIconSize(QSize(64, 64));
    connect(m_timelineFramesList, &TimelineListWidget::frameDropped, this, &MainWindow::onFrameDropped);
    connect(m_timelineFramesList, &TimelineListWidget::frameMoved, this, &MainWindow::onFrameMoved);
    connect(m_timelineFramesList, &TimelineListWidget::removeSelectedRequested, this, &MainWindow::onFrameRemoveRequested);
    connect(m_timelineFramesList, &TimelineListWidget::duplicateFrameRequested, this, &MainWindow::onFrameDuplicateRequested);
    dropAreaLayout->addWidget(m_timelineFramesList);
    editorContainerLayout->addWidget(m_timelineDropArea);
    timelineLayout->addStretch(0);

    m_leftSplitter->addWidget(timelineGroup);

    mainSplitter->addWidget(m_leftSplitter);

    // --- Right Panel ---
    m_rightSplitter = new QSplitter(Qt::Vertical, mainSplitter);

    // 3. Selected Frame Editor
    QGroupBox* editorGroup = new QGroupBox(tr("Selected Frame Editor"), m_rightSplitter);
    QVBoxLayout* editorLayoutBox = new QVBoxLayout(editorGroup);

    QHBoxLayout* nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel(tr("Name:")));
    m_spriteNameEdit = new QLineEdit(this);
    m_spriteNameEdit->setEnabled(false);
    nameRow->addWidget(m_spriteNameEdit);

    nameRow->addWidget(new QLabel(tr("Zoom:")));
    m_previewZoomSpin = new QDoubleSpinBox(this);
    m_previewZoomSpin->setRange(0.1, 16.0);
    m_previewZoomSpin->setValue(2.0);
    m_previewZoomSpin->setSingleStep(0.1);
    nameRow->addWidget(m_previewZoomSpin);

    editorLayoutBox->addLayout(nameRow);

    QHBoxLayout* pivotRow = new QHBoxLayout();
    pivotRow->addWidget(new QLabel(tr("Handle:")));
    m_handleCombo = new QComboBox(this);
    m_handleCombo->addItem(tr("pivot"));
    pivotRow->addWidget(m_handleCombo);
    connect(m_handleCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onHandleComboChanged);
    pivotRow->addWidget(new QLabel(tr("X:")));
    m_pivotXSpin = new QSpinBox(this);
    m_pivotXSpin->setEnabled(false);
    m_pivotXSpin->setRange(0, 9999);
    connect(m_pivotXSpin, &QSpinBox::editingFinished, this, &MainWindow::onPivotSpinChanged);
    pivotRow->addWidget(m_pivotXSpin);
    pivotRow->addWidget(new QLabel(tr("Y:")));
    m_pivotYSpin = new QSpinBox(this);
    m_pivotYSpin->setEnabled(false);
    m_pivotYSpin->setRange(0, 9999);
    connect(m_pivotYSpin, &QSpinBox::editingFinished, this, &MainWindow::onPivotSpinChanged);
    pivotRow->addWidget(m_pivotYSpin);
    QIcon manageIcon = QIcon::fromTheme("preferences-system");
    m_configPointsBtn = new QPushButton(manageIcon, manageIcon.isNull() ? tr("Manage") : "", this);
    m_configPointsBtn->setToolTip(tr("Manage Markers"));
    connect(m_configPointsBtn, &QPushButton::clicked, this, &MainWindow::onPointsConfigClicked);
    pivotRow->addWidget(m_configPointsBtn);
    m_configPointsBtn->setEnabled(false);
    pivotRow->addStretch();
    editorLayoutBox->addLayout(pivotRow);

    m_previewView = new PreviewCanvas(this);
    connect(m_previewView, &PreviewCanvas::pivotChanged, this, &MainWindow::onCanvasPivotChanged);
    connect(m_previewView->overlay(), &EditorOverlayItem::markerSelected, this, &MainWindow::onMarkerSelectedFromCanvas);
    connect(m_previewView->overlay(), &EditorOverlayItem::markerChanged, this, &MainWindow::onMarkerChangedFromCanvas);
    connect(m_previewView, &PreviewCanvas::zoomChanged, this, [this](double zoom) {
        m_previewZoomSpin->blockSignals(true);
        m_previewZoomSpin->setValue(zoom);
        m_previewZoomSpin->blockSignals(false);
    });
    connect(m_previewZoomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onPreviewZoomChanged);
    editorLayoutBox->addWidget(m_previewView);

    m_rightSplitter->addWidget(editorGroup);

    // 4. Animation Test
    QGroupBox* animGroup = new QGroupBox(tr("Animation Test"), m_rightSplitter);
    QVBoxLayout* animLayout = new QVBoxLayout(animGroup);
    animLayout->setContentsMargins(10, 10, 10, 10);

    QHBoxLayout* animControls = new QHBoxLayout();
    m_animPrevBtn = new QPushButton("<");
    connect(m_animPrevBtn, &QPushButton::clicked, this, &MainWindow::onAnimPrevClicked);
    animControls->addWidget(m_animPrevBtn);
    m_animPlayPauseBtn = new QPushButton(tr("Play"));
    connect(m_animPlayPauseBtn, &QPushButton::clicked, this, &MainWindow::onAnimPlayPauseClicked);
    animControls->addWidget(m_animPlayPauseBtn);
    m_animNextBtn = new QPushButton(">");
    connect(m_animNextBtn, &QPushButton::clicked, this, &MainWindow::onAnimNextClicked);
    animControls->addWidget(m_animNextBtn);
    animControls->addWidget(new QLabel(tr("FPS:")));
    m_fpsSpin = new QSpinBox(this);
    m_fpsSpin->setRange(1, 60);
    m_fpsSpin->setValue(8);
    connect(m_fpsSpin, &QSpinBox::valueChanged, this, &MainWindow::onAnimFpsChanged);
    animControls->addWidget(m_fpsSpin);
    animControls->addStretch();
    animControls->addWidget(new QLabel(tr("Zoom:")));
    m_animZoomSpin = new QDoubleSpinBox(this);
    m_animZoomSpin->setRange(0.1, 16.0);
    m_animZoomSpin->setValue(2.0);
    connect(m_animZoomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { refreshAnimationTest(); });
    animControls->addWidget(m_animZoomSpin);
    animLayout->addLayout(animControls);

    m_animStatusLabel = new QLabel(tr("Create/select a timeline and drag frames into it."), this);
    m_animStatusLabel->setStyleSheet("color: #808080;");
    animLayout->addWidget(m_animStatusLabel);

    m_animPreviewLabel = new QLabel(this);
    m_animPreviewLabel->setAlignment(Qt::AlignCenter);
    m_animPreviewLabel->setMinimumSize(280, 180);
    m_animPreviewLabel->setStyleSheet("border: 1px solid #565656; background: #5a5a5a;");
    m_animPreviewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_animPreviewLabel->installEventFilter(this);
    animLayout->addWidget(m_animPreviewLabel);

    m_rightSplitter->addWidget(animGroup);

    mainSplitter->addWidget(m_rightSplitter);
    mainSplitter->setStretchFactor(0, 3);
    mainSplitter->setStretchFactor(1, 1);

    // Sync splitters
    m_leftSplitter->setStretchFactor(0, 1);
    m_leftSplitter->setStretchFactor(1, 0);
    m_rightSplitter->setStretchFactor(0, 1);
    m_rightSplitter->setStretchFactor(1, 0);

    QList<int> initialSizes;
    initialSizes << 560 << 300;
    m_leftSplitter->setSizes(initialSizes);
    m_rightSplitter->setSizes(initialSizes);

    connect(m_leftSplitter, &QSplitter::splitterMoved, this, [this](int, int) {
        if (m_syncingSplitters) {
            return;
        }
        m_syncingSplitters = true;
        m_rightSplitter->setSizes(m_leftSplitter->sizes());
        m_syncingSplitters = false;
    });
    connect(m_rightSplitter, &QSplitter::splitterMoved, this, [this](int, int) {
        if (m_syncingSplitters) {
            return;
        }
        m_syncingSplitters = true;
        m_leftSplitter->setSizes(m_rightSplitter->sizes());
        m_syncingSplitters = false;
    });

    m_mainStack->addWidget(m_editorPage);

    setupStatusBarUi();

    updateUiState();
    applySettings();

    setupZoomShortcuts();
}

void MainWindow::setupToolbar() {
    QToolBar* toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(true);

    m_loadAction = toolbar->addAction(tr("Load Images Folder"));
    connect(m_loadAction, &QAction::triggered, this, &MainWindow::onLoadFolder);

    toolbar->addSeparator();
    QAction* loadProjectAction = toolbar->addAction(tr("Load..."));
    connect(loadProjectAction, &QAction::triggered, this, &MainWindow::onLoadProject);
    m_saveAction = toolbar->addAction(tr("Save..."));
    m_saveAction->setEnabled(false);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::onSaveClicked);

    QAction* settingsAction = toolbar->addAction(tr("Settings"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onSettingsClicked);

    m_showDebugAction = toolbar->addAction(tr("Debug"));
    m_showDebugAction->setCheckable(true);
    connect(m_showDebugAction, &QAction::toggled, this, [this](bool checked) {
        if (m_debugDock) {
            m_debugDock->setVisible(checked);
        }
    });
    toolbar->addSeparator();
    m_folderLabel = new QLabel(tr("Folder: none"), this);
    m_folderLabel->setStyleSheet("padding-left: 10px;");
    toolbar->addWidget(m_folderLabel);
}

void MainWindow::setupStatusBarUi() {
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
        } else if (m_animPreviewLabel && m_animPreviewLabel->parentWidget() && m_animPreviewLabel->parentWidget()->isAncestorOf(fw)) {
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
}

void MainWindow::updateUiState() {
    const bool enabled = m_cliReady && !m_isLoading;
    MainWindowUiState::apply(
        m_cliReady,
        m_isLoading,
        !m_layoutModel.sprites.isEmpty(),
        m_loadAction,
        m_profileCombo,
        m_saveAction);
    if (m_manageProfilesBtn) {
        m_manageProfilesBtn->setEnabled(enabled);
    }
    if (m_addProfilesBtn) {
        m_addProfilesBtn->setEnabled(enabled);
    }
}

void MainWindow::updateMainContentView() {
    bool hasLayout = m_canvas->scene() && !m_canvas->scene()->items().isEmpty();
    m_mainStack->setCurrentWidget(hasLayout ? m_editorPage : m_welcomePage);
}
