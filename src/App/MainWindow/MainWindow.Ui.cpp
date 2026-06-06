#include "MainWindow.h"
#include "ElidedLabel.h"
#include "PackedAtlasView.h"
#include "AtlasLayoutWorkspace.h"
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
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QActionGroup>
#include <QMessageBox>
#include "NavigatorTreeWidget.h"
#include "TimelineTreeWidget.h"

static double toDisplay(int px, int dim, CoordUnit unit) {
    return (unit == CoordUnit::Percent && dim > 0)
        ? px * 100.0 / dim : double(px);
}

namespace {
void setCoordinateSpinValue(QDoubleSpinBox* spin, int px, int dim, CoordUnit unit) {
    if (!spin) return;
    spin->blockSignals(true);
    spin->setValue(toDisplay(px, dim, unit));
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

    connect(m_exportWorkspace, &ExportWorkspace::previewSettingsChanged,
            this, &MainWindow::schedulePreviewPack);
    connect(&m_previewPackWatcher, &QFutureWatcher<QByteArray>::finished,
            this, &MainWindow::onPreviewPackFinished);

    // Page 3: Atlas Layout Workspace
    m_atlasLayoutWorkspace = new AtlasLayoutWorkspace(this);
    m_mainStack->addWidget(m_atlasLayoutWorkspace);  // page 2

    // --- Create Docks ---
    const int groupMargin = 4;
    const int groupTopPadding = 12;
    const int groupBottomMargin = 0;

    // 1. Layout Canvas panel
    QWidget* canvasContent = new QWidget(this);
    canvasContent->setStyleSheet("font-weight: normal;");
    QVBoxLayout* canvasLayout = new QVBoxLayout(canvasContent);
    canvasLayout->setContentsMargins(groupMargin, groupTopPadding, groupMargin, groupBottomMargin);

    // makeZoomLabel – used by frame editor and animation panels below
    auto makeZoomLabel = [this]() {
        QPixmap pix(16, 16);
        pix.fill(Qt::transparent);
        QPainter painter(&pix);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().color(QPalette::WindowText), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(6, 6), 4.5, 4.5);
        painter.drawLine(QPointF(9.2, 9.2), QPointF(14, 14));
        painter.end();
        auto* label = new QLabel(this);
        label->setPixmap(pix);
        label->setToolTip(tr("Zoom"));
        return label;
    };

    // Hidden profile combo – drives layout logic, never shown directly
    m_profileCombo = new QComboBox(this);
    m_profileCombo->setVisible(false);
    m_profileCombo->setToolTip(tr("Layout profile"));
    m_profileCombo->setAccessibleName(tr("Layout profile"));
    connect(m_profileCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onProfileChanged);
    // Keep profileCombo in sync with workspace selection
    connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (m_atlasLayoutWorkspace && m_profileCombo)
            m_atlasLayoutWorkspace->setSelectedProfile(m_profileCombo->currentData().toString());
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

    // Zoom spin
    m_layoutZoomSpin = new QDoubleSpinBox(this);
    m_layoutZoomSpin->setRange(10.0, 800.0);
    m_layoutZoomSpin->setValue(100.0);
    m_layoutZoomSpin->setSuffix("%");
    m_layoutZoomSpin->setSingleStep(10.0);
    connect(m_layoutZoomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onLayoutZoomChanged);

    // Move view controls into workspace right panel
    m_atlasLayoutWorkspace->setViewControls(m_sourceResolutionCombo, m_layoutZoomSpin);

    // Workspace → m_profileCombo sync (workspace selection drives layout)
    connect(m_atlasLayoutWorkspace, &AtlasLayoutWorkspace::selectedProfileChanged,
            this, [this](const QString& name) {
                if (!m_profileCombo) return;
                const int idx = m_profileCombo->findData(name);
                if (idx >= 0 && idx != m_profileCombo->currentIndex())
                    m_profileCombo->setCurrentIndex(idx);
            });
    connect(m_atlasLayoutWorkspace, &AtlasLayoutWorkspace::selectedAtlasChanged,
            this, [this](int index) { if (m_canvas) m_canvas->scrollToAtlas(index); });
    connect(m_atlasLayoutWorkspace, &AtlasLayoutWorkspace::manageProfilesRequested,
            this, &MainWindow::onManageProfiles);

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
        m_layoutZoomSpin->blockSignals(true);
        m_layoutZoomSpin->setValue(zoom * 100.0);
        m_layoutZoomSpin->blockSignals(false);
    });
    connect(m_canvas, &LayoutCanvas::requestTimelineGeneration, this, &MainWindow::onGenerateTimelinesFromFrames);
    connect(m_canvas, &LayoutCanvas::externalPathDropped, this, &MainWindow::onLayoutCanvasPathDropped);
    connect(m_canvas, &LayoutCanvas::addFramesRequested, this, &MainWindow::onCanvasAddFramesRequested);
    connect(m_canvas, &LayoutCanvas::removeFramesRequested, this, &MainWindow::onCanvasRemoveFramesRequested);
    connect(m_canvas, &LayoutCanvas::splitSpriteRequested, this, &MainWindow::onSplitSpriteRequested);
    connect(m_canvas, &LayoutCanvas::userInteractionStarted, this, &MainWindow::pauseLayoutRebuild);
    connect(m_canvas, &LayoutCanvas::userInteractionEnded, this, &MainWindow::resumeLayoutRebuild);
    connect(m_canvas, &LayoutCanvas::splitModeChanged, this, [this](bool enabled) {
        m_statusLabel->setText(enabled
            ? tr("Split mode — click a sprite edge to split it. Press S or right-click to exit.")
            : tr("Idle"));
    });
    m_canvas->viewport()->installEventFilter(this);

    // Navigator panel (tree view of sprites)
    QWidget* navigatorContent = new QWidget(this);
    navigatorContent->setStyleSheet("font-weight: normal;");
    QVBoxLayout* navigatorLayout = new QVBoxLayout(navigatorContent);
    navigatorLayout->setContentsMargins(groupMargin, groupTopPadding, groupMargin, groupBottomMargin);

    // Sprite filter search box + result count label
    QHBoxLayout* filterRow = new QHBoxLayout();
    m_spriteFilterEdit = new QLineEdit(navigatorContent);
    m_spriteFilterEdit->setPlaceholderText(tr("Search sprites..."));
    filterRow->addWidget(m_spriteFilterEdit);
    m_spriteFilterResultLabel = new QLabel(navigatorContent);
    m_spriteFilterResultLabel->setStyleSheet("color: #888; font-size: 11px;");
    m_spriteFilterResultLabel->setVisible(false);
    filterRow->addWidget(m_spriteFilterResultLabel);
    m_showHiddenToggleBtn = new QCheckBox(tr("Show hidden"), navigatorContent);
    m_showHiddenToggleBtn->setChecked(false);
    m_showHiddenToggleBtn->setToolTip(tr("Show hidden and excluded items"));
    filterRow->addWidget(m_showHiddenToggleBtn);
    navigatorLayout->addLayout(filterRow);

    m_spriteTree = new NavigatorTreeWidget(navigatorContent);
    m_spriteTree->setHeaderLabel(tr("Sprites"));
    m_spriteTree->setIconSize(QSize(20, 20));
    m_spriteTree->setSortingEnabled(true);
    m_spriteTree->sortByColumn(0, Qt::AscendingOrder);
    m_spriteTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_spriteTree, &QWidget::customContextMenuRequested,
            this, &MainWindow::onSpriteTreeContextMenu);
    connect(m_spriteTree, &NavigatorTreeWidget::excludeRequested,
            this, &MainWindow::onNavigatorExcludeKey);
    navigatorLayout->addWidget(m_spriteTree);

    // Connect filter box to search functionality
    connect(m_spriteFilterEdit, &QLineEdit::textChanged, this, &MainWindow::filterSpriteTree);
    connect(m_showHiddenToggleBtn, &QCheckBox::toggled, this, [this](bool checked) {
        m_showHiddenItems = checked;
        refreshSpriteTree();
    });

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

    // 2. Animation Timelines panel
    QWidget* timelineContent = new QWidget(this);
    timelineContent->setStyleSheet("font-weight: normal;");
    QVBoxLayout* timelineLayout = new QVBoxLayout(timelineContent);
    timelineLayout->setContentsMargins(groupMargin, groupTopPadding, groupMargin, groupBottomMargin);

    QHBoxLayout* timelineAddLayout = new QHBoxLayout();
    timelineAddLayout->addWidget(new QLabel(tr("Name:")));
    m_timelineCreateEdit = new QLineEdit(this);
    m_timelineCreateEdit->setPlaceholderText(tr("Timeline name (optional)"));
    timelineAddLayout->addWidget(m_timelineCreateEdit);
    QPushButton* addTimelineBtn = new QPushButton(
        QApplication::style()->standardIcon(QStyle::SP_FileDialogNewFolder), "", this);
    addTimelineBtn->setToolTip(tr("Add timeline"));
    connect(addTimelineBtn, &QPushButton::clicked, this, &MainWindow::onTimelineAddClicked);
    connect(m_timelineCreateEdit, &QLineEdit::returnPressed, this, &MainWindow::onTimelineAddClicked);
    timelineAddLayout->addWidget(addTimelineBtn);
    timelineLayout->addLayout(timelineAddLayout);

    m_timelineList = new TimelineTreeWidget(this);
    m_timelineList->setHeaderHidden(true);
    m_timelineList->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_timelineList->setIconSize(QSize(32, 32));
    m_timelineList->setDragEnabled(true);
    connect(m_timelineList, &QTreeWidget::itemSelectionChanged, this, &MainWindow::onTimelineSelectionChanged);
    timelineLayout->addWidget(m_timelineList, 1); // Give it a stretch factor
    m_timelineList->setVisible(false);
    m_timelineList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_timelineList, &QWidget::customContextMenuRequested,
            this, &MainWindow::onTimelineContextMenu);
    connect(m_timelineList, &TimelineTreeWidget::deleteKeyPressed,
            this, &MainWindow::onTimelineDeleteKey);
    connect(m_timelineList, &TimelineTreeWidget::dropCompleted,
            this, &MainWindow::onTimelineTreeDropCompleted);
    connect(m_timelineList, &QTreeWidget::itemChanged,
            this, &MainWindow::onTimelineItemChanged);

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
    QPushButton* removeTimelineBtn = new QPushButton(
        QApplication::style()->standardIcon(QStyle::SP_DialogDiscardButton), "", this);
    removeTimelineBtn->setToolTip(tr("Remove timeline"));
    connect(removeTimelineBtn, &QPushButton::clicked, this, &MainWindow::onTimelineRemoveClicked);
    timelineNameLayout->addWidget(removeTimelineBtn);
    groupLayout->addLayout(timelineNameLayout);

    m_timelineAliasLabel = new QLabel(this);
    m_timelineAliasLabel->setVisible(false);
    m_timelineAliasLabel->setToolTip(tr("An alias timeline references another timeline's frames but can have its own flip and transform settings."));
    groupLayout->addWidget(m_timelineAliasLabel);

    QHBoxLayout* flipRow = new QHBoxLayout();
    m_timelineFlipLabel = new QLabel(tr("Flip:"), this);
    m_timelineFlipLabel->setVisible(false);
    flipRow->addWidget(m_timelineFlipLabel);
    m_timelineFlipCombo = new QComboBox(this);
    m_timelineFlipCombo->addItem(tr("None"),       0);
    m_timelineFlipCombo->addItem(tr("Horizontal"), 1);
    m_timelineFlipCombo->addItem(tr("Vertical"),   2);
    m_timelineFlipCombo->addItem(tr("Both"),       3);
    m_timelineFlipCombo->setVisible(false);
    connect(m_timelineFlipCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onTimelineFlipChanged);
    flipRow->addWidget(m_timelineFlipCombo);
    flipRow->addStretch();
    groupLayout->addLayout(flipRow);

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
    m_timelineFramesList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_timelineFramesList->setResizeMode(QListWidget::Adjust);
    m_timelineFramesList->setIconSize(QSize(48, 48));
    m_timelineFramesList->setFixedHeight(96);
    connect(m_timelineFramesList, &TimelineListWidget::frameDropped, this, &MainWindow::onFrameDropped);
    connect(m_timelineFramesList, &TimelineListWidget::frameMoved, this, &MainWindow::onFrameMoved);
    connect(m_timelineFramesList, &TimelineListWidget::removeSelectedRequested, this, &MainWindow::onFrameRemoveRequested);
    connect(m_timelineFramesList, &TimelineListWidget::duplicateFrameRequested, this, &MainWindow::onFrameDuplicateRequested);
    connect(m_timelineFramesList, &QListWidget::itemSelectionChanged, this, &MainWindow::onTimelineFrameSelectionChanged);
    dropAreaLayout->addWidget(m_timelineFramesList);
    groupLayout->addWidget(m_timelineDropArea);
    timelineLayout->addStretch(0);

    // 3. Selected Frame Editor panel
    QWidget* editorContent = new QWidget(this);
    editorContent->setStyleSheet("font-weight: normal;");
    QVBoxLayout* editorLayoutBox = new QVBoxLayout(editorContent);
    editorLayoutBox->setContentsMargins(groupMargin, groupTopPadding, groupMargin, groupBottomMargin);

    // Multi-selection indicator (shown when >1 sprite is checked in Navigator)
    m_multiSelectionLabel = new QLabel(this);
    m_multiSelectionLabel->setStyleSheet("color: #5a9fd4; font-style: italic; padding: 2px 0;");
    m_multiSelectionLabel->setAlignment(Qt::AlignCenter);
    m_multiSelectionLabel->setVisible(false);
    editorLayoutBox->addWidget(m_multiSelectionLabel);

    // Name row: label + editable name field + aliases button
    QHBoxLayout* nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel(tr("Name:")));
    m_spriteNameEdit = new QLineEdit(this);
    m_spriteNameEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_spriteNameEdit->setEnabled(false);
    connect(m_spriteNameEdit, &QLineEdit::editingFinished,
            this, &MainWindow::onSpriteNameEditingFinished);
    nameRow->addWidget(m_spriteNameEdit);
    m_editAliasesBtn = new QPushButton(
        QApplication::style()->standardIcon(QStyle::SP_FileDialogContentsView), "", this);
    m_editAliasesBtn->setToolTip(tr("Edit sprite name aliases (alternative names that share markers and pivots)"));
    m_editAliasesBtn->setEnabled(false);
    connect(m_editAliasesBtn, &QPushButton::clicked, this, &MainWindow::onEditAliases);
    nameRow->addWidget(m_editAliasesBtn);
    nameRow->addWidget(makeZoomLabel());
    m_previewZoomSpin = new QDoubleSpinBox(this);
    m_previewZoomSpin->setRange(10.0, 1600.0);
    m_previewZoomSpin->setValue(200.0);
    m_previewZoomSpin->setSuffix("%");
    m_previewZoomSpin->setSingleStep(10.0);
    nameRow->addWidget(m_previewZoomSpin);
    editorLayoutBox->addLayout(nameRow);

    QHBoxLayout* pivotRow = new QHBoxLayout();
    {
        QPixmap pix(16, 16);
        pix.fill(Qt::transparent);
        QPainter painter(&pix);
        painter.setRenderHint(QPainter::Antialiasing);
        QPen pen(palette().color(QPalette::WindowText), 1.2);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(8, 8), 3.5, 3.5);  // center circle
        painter.drawLine(QPointF(8, 1),  QPointF(8, 4));   // top tick
        painter.drawLine(QPointF(8, 12), QPointF(8, 15));  // bottom tick
        painter.drawLine(QPointF(1, 8),  QPointF(4, 8));   // left tick
        painter.drawLine(QPointF(12, 8), QPointF(15, 8));  // right tick
        painter.end();
        auto* handleLabel = new QLabel(this);
        handleLabel->setPixmap(pix);
        handleLabel->setToolTip(tr("Selected marker: the pivot point, a named point, or a named area."));
        pivotRow->addWidget(handleLabel);
    }
    m_handleCombo = new QComboBox(this);
    m_handleCombo->addItem(tr("pivot"));
    m_handleCombo->setToolTip(tr("Select pivot or a named marker to edit"));
    m_handleCombo->setAccessibleName(tr("Handle selector"));
    pivotRow->addWidget(m_handleCombo);
    connect(m_handleCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onHandleComboChanged);
    pivotRow->addWidget(new QLabel(tr("X:")));
    m_pivotXSpin = new QDoubleSpinBox(this);
    m_pivotXSpin->setEnabled(false);
    m_pivotXSpin->setDecimals(0);
    m_pivotXSpin->setRange(0, 9999);
    m_pivotXSpin->setToolTip(tr("Pivot X: horizontal origin for sprite rotation"));
    m_pivotXSpin->setAccessibleName(tr("Pivot X"));
    connect(m_pivotXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ onPivotSpinChanged(); });
    pivotRow->addWidget(m_pivotXSpin);
    pivotRow->addWidget(new QLabel(tr("Y:")));
    m_pivotYSpin = new QDoubleSpinBox(this);
    m_pivotYSpin->setEnabled(false);
    m_pivotYSpin->setDecimals(0);
    m_pivotYSpin->setRange(0, 9999);
    m_pivotYSpin->setToolTip(tr("Pivot Y: vertical origin for sprite rotation"));
    m_pivotYSpin->setAccessibleName(tr("Pivot Y"));
    connect(m_pivotYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ onPivotSpinChanged(); });
    pivotRow->addWidget(m_pivotYSpin);
    m_coordUnitCombo = new QComboBox(this);
    m_coordUnitCombo->addItem(tr("px"), int(CoordUnit::Pixels));
    m_coordUnitCombo->addItem(tr("%"),  int(CoordUnit::Percent));
    m_coordUnitCombo->setCurrentIndex(
        m_settings.coordUnit == CoordUnit::Percent ? 1 : 0);
    m_coordUnitCombo->setEnabled(false);
    m_coordUnitCombo->setToolTip(tr("Coordinate unit: pixels or percent of sprite dimensions"));
    connect(m_coordUnitCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onCoordUnitChanged);
    pivotRow->addWidget(m_coordUnitCombo);
    m_configPointsBtn = new QPushButton(
        QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView), "", this);
    m_configPointsBtn->setToolTip(tr("Manage Markers: define named points on this sprite, such as hitboxes, spawn positions, or attachment points"));
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
    connect(m_previewView, &PreviewCanvas::zoomChanged, this, [this](double zoom) {
        m_previewZoomSpin->blockSignals(true);
        m_previewZoomSpin->setValue(zoom * 100.0);
        m_previewZoomSpin->blockSignals(false);
    });
    connect(m_previewZoomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onPreviewZoomChanged);
    editorLayoutBox->addWidget(m_previewView);

    {
        auto* spriteFooterRow = new QHBoxLayout();
        spriteFooterRow->setContentsMargins(8, 2, 8, 2);
        m_spriteNameFooterLabel = new ElidedLabel(editorContent);
        m_spriteNameFooterLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_spriteNameFooterLabel->setVisible(false);
        spriteFooterRow->addWidget(m_spriteNameFooterLabel);
        spriteFooterRow->addStretch();
        m_spriteDimsLabel = new QLabel(editorContent);
        m_spriteDimsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_spriteDimsLabel->setVisible(false);
        spriteFooterRow->addWidget(m_spriteDimsLabel);
        editorLayoutBox->addLayout(spriteFooterRow);
    }

    // 4. Animation Test panel
    QWidget* animContent = new QWidget(this);
    animContent->setStyleSheet("font-weight: normal;");
    QVBoxLayout* animLayout = new QVBoxLayout(animContent);
    animLayout->setContentsMargins(groupMargin, groupTopPadding, groupMargin, groupBottomMargin);

    QHBoxLayout* animControls = new QHBoxLayout();
    auto* style_ = QApplication::style();
    m_animPrevBtn = new QPushButton(style_->standardIcon(QStyle::SP_MediaSkipBackward), "");
    m_animPrevBtn->setToolTip(tr("Step to previous frame"));
    m_animPrevBtn->setAccessibleName(tr("Previous frame"));
    connect(m_animPrevBtn, &QPushButton::clicked, this, &MainWindow::onAnimPrevClicked);
    animControls->addWidget(m_animPrevBtn);
    m_animPlayPauseBtn = new QPushButton(style_->standardIcon(QStyle::SP_MediaPlay), "");
    m_animPlayPauseBtn->setToolTip(tr("Play or pause animation"));
    m_animPlayPauseBtn->setAccessibleName(tr("Play or pause"));
    connect(m_animPlayPauseBtn, &QPushButton::clicked, this, &MainWindow::onAnimPlayPauseClicked);
    animControls->addWidget(m_animPlayPauseBtn);
    m_animNextBtn = new QPushButton(style_->standardIcon(QStyle::SP_MediaSkipForward), "");
    m_animNextBtn->setToolTip(tr("Step to next frame"));
    m_animNextBtn->setAccessibleName(tr("Next frame"));
    connect(m_animNextBtn, &QPushButton::clicked, this, &MainWindow::onAnimNextClicked);
    animControls->addWidget(m_animNextBtn);
    animControls->addStretch();
    animControls->addWidget(makeZoomLabel());
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
    m_atlasSplitter->addWidget(editorContent);
    m_atlasSplitter->setStretchFactor(0, 0);
    m_atlasSplitter->setStretchFactor(1, 1);
    m_atlasDock->setWidget(m_atlasSplitter);

    // Animation group: Timelines | Anim Test
    m_animationDock = new QDockWidget(tr("Animation"), this);
    m_animationDock->setObjectName("animationDock");
    m_animationDock->setFont(boldFont);
    m_animationDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_animationDock->setTitleBarWidget(new QWidget(this));
    auto *animSplitter = new QSplitter(Qt::Horizontal, m_animationDock);
    animSplitter->addWidget(timelineContent);
    animSplitter->addWidget(animContent);
    m_animationDock->setWidget(animSplitter);

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

    // Layout: three rows
    addDockWidget(Qt::TopDockWidgetArea, m_atlasDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_animationDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_debugDock);
    splitDockWidget(m_animationDock, m_debugDock, Qt::Vertical);

    // Atlas row taller than Animation row
    resizeDocks({m_atlasDock}, {600}, Qt::Vertical);
    resizeDocks({m_animationDock}, {260}, Qt::Vertical);

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

    m_atlasLayoutWorkspaceAction = m_viewMenu->addAction(tr("Atlas Layout"));
    m_atlasLayoutWorkspaceAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_L));
    m_atlasLayoutWorkspaceAction->setCheckable(true);
    workspaceGroup->addAction(m_atlasLayoutWorkspaceAction);
    connect(m_atlasLayoutWorkspaceAction, &QAction::triggered, this, &MainWindow::showAtlasLayoutWorkspace);

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
    QAction* stylesAction = settingsMenu->addAction(
        style->standardIcon(QStyle::SP_DesktopIcon), tr("Styles..."));
    stylesAction->setToolTip(tr("Open style settings"));
    connect(stylesAction, &QAction::triggered, this, &MainWindow::onSettingsStylesClicked);

    QAction* spritesheetAction = settingsMenu->addAction(
        style->standardIcon(QStyle::SP_FileDialogListView), tr("Atlas Sprites..."));
    spritesheetAction->setToolTip(tr("Open atlas sprites settings"));
    connect(spritesheetAction, &QAction::triggered, this, &MainWindow::onSettingsSpritesheetClicked);

    QAction* framesEditorAction = settingsMenu->addAction(
        style->standardIcon(QStyle::SP_FileDialogDetailedView), tr("Frames Editor..."));
    framesEditorAction->setToolTip(tr("Open frames editor settings"));
    connect(framesEditorAction, &QAction::triggered, this, &MainWindow::onSettingsFramesEditorClicked);

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

        QDoubleSpinBox* targetSpin = nullptr;
        if (m_canvas && (fw == m_canvas || m_canvas->isAncestorOf(fw))) {
            targetSpin = m_layoutZoomSpin;
        } else if (m_previewView && (fw == m_previewView || m_previewView->isAncestorOf(fw))) {
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
    const bool hasModels = m_session && !m_session->layoutModels.isEmpty() && !m_session->layoutModels.first().sprites.isEmpty();

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
    const bool hasProject = !m_projectFilePath.isEmpty();
    const bool viewEnabled = hasProject || hasModels;
    if (m_atlasWorkspaceAction)         m_atlasWorkspaceAction->setEnabled(viewEnabled);
    if (m_frameAnimWorkspaceAction)     m_frameAnimWorkspaceAction->setEnabled(viewEnabled);
    if (m_exportationWorkspaceAction)   m_exportationWorkspaceAction->setEnabled(viewEnabled);
    if (m_atlasLayoutWorkspaceAction)   m_atlasLayoutWorkspaceAction->setEnabled(viewEnabled && hasModels);

    // Toggle docks and welcome page based on project / sprite state.
    // Skip this block while any full-screen workspace is active.
    if (m_exportWorkspaceActive || m_atlasLayoutWorkspaceActive) return;

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
    const bool hasLayout = m_session && !m_session->layoutModels.isEmpty() && !m_session->layoutModels.first().sprites.isEmpty();
    if (m_atlasDimsLabel && !hasLayout)
        m_atlasDimsLabel->setVisible(false);
    const bool hasProject = !m_projectFilePath.isEmpty();

    if (!m_exportWorkspaceActive && !m_atlasLayoutWorkspaceActive) {
        m_mainStack->setVisible(!hasLayout && !hasProject);
        if (!hasLayout && !hasProject) {
            m_mainStack->setCurrentIndex(0);
            setWindowTitle(tr("Sprat GUI %1[*]").arg(SPRAT_GUI_VERSION));
            return;
        }
    }
    if (hasLayout) {
        QString projectName = QFileInfo(m_projectFilePath).dir().dirName();
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
    m_coordinateFieldOverride.x = m_pivotXSpin->value();
    m_coordinateFieldOverride.y = m_pivotYSpin->value();
}

bool MainWindow::coordinateFieldOverrideApplies() const {
    return m_coordinateFieldOverride.active
        && m_session
        && m_session->selectedSprite
        && m_coordinateFieldOverride.sprite == m_session->selectedSprite.get()
        && m_coordinateFieldOverride.markerName == m_session->selectedPointName
        && m_coordinateFieldOverride.unit == m_settings.coordUnit;
}

void MainWindow::syncPivotSpinsFromSprite() {
    syncCoordinateSpinsFromSelection();
    if (m_previewView && m_previewView->overlay())
        m_previewView->overlay()->updateLayout();
}

void MainWindow::syncCoordinateSpinsFromSelection() {
    if (!m_session || !m_session->selectedSprite) {
        if (m_coordUnitCombo) {
            m_coordUnitCombo->setEnabled(false);
        }
        return;
    }

    const QSize spriteSize = spriteCoordinateSpaceSize(m_session->selectedSprite);
    const int spriteWidth = spriteSize.width();
    const int spriteHeight = spriteSize.height();
    const bool hasDimensions = spriteWidth > 0 && spriteHeight > 0;
    const CoordUnit displayUnit = hasDimensions ? m_settings.coordUnit : CoordUnit::Pixels;

    if (m_coordUnitCombo) {
        m_coordUnitCombo->setEnabled(hasDimensions);
    }
    if (m_pivotXSpin) {
        m_pivotXSpin->setDecimals(displayUnit == CoordUnit::Percent ? 1 : 0);
    }
    if (m_pivotYSpin) {
        m_pivotYSpin->setDecimals(displayUnit == CoordUnit::Percent ? 1 : 0);
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
        setCoordinateSpinValue(m_pivotXSpin, m_session->selectedSprite->pivotX, spriteWidth, displayUnit);
        setCoordinateSpinValue(m_pivotYSpin, m_session->selectedSprite->pivotY, spriteHeight, displayUnit);
        return;
    }

    for (const auto& point : m_session->selectedSprite->points) {
        if (point.name != m_session->selectedPointName) {
            continue;
        }
        setCoordinateSpinValue(m_pivotXSpin, point.x, spriteWidth, displayUnit);
        setCoordinateSpinValue(m_pivotYSpin, point.y, spriteHeight, displayUnit);
        return;
    }

    setCoordinateSpinValue(m_pivotXSpin, m_session->selectedSprite->pivotX, spriteWidth, displayUnit);
    setCoordinateSpinValue(m_pivotYSpin, m_session->selectedSprite->pivotY, spriteHeight, displayUnit);
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

    if (!m_session || m_session->layoutModels.isEmpty()) {
        m_spriteTree->blockSignals(false);
        return;
    }

    // Strip trailing digits to find a group prefix for animation sequences.
    // e.g. "walk1" → "walk", "idle" → "idle"
    // Returns empty string if no trailing digits found.
    auto groupPrefix = [](const QString& name) -> QString {
        int end = name.size();
        while (end > 0 && name[end - 1].isDigit()) --end;
        if (end == 0 || end == name.size()) return QString();
        return name.left(end);
    };

    auto endsWithDigit = [](const QString& name) -> bool {
        return !name.isEmpty() && name.back().isDigit();
    };

    const QIcon folderIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);

    auto makeLeaf = [&](QTreeWidgetItem* parent, const SpritePtr& sprite, const QString& leafName) {
        auto* leaf = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_spriteTree);
        leaf->setText(0, leafName);
        leaf->setFlags(leaf->flags() | Qt::ItemIsUserCheckable);
        leaf->setCheckState(0, Qt::Unchecked);
        leaf->setData(0, Qt::UserRole, QVariant::fromValue(sprite));
        QPixmap pix(sprite->path);
        if (!pix.isNull())
            leaf->setIcon(0, QIcon(pix.scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    };

    auto makeGroupNode = [&](QTreeWidgetItem* parent, const QString& text) {
        auto* node = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_spriteTree);
        node->setText(0, text);
        node->setIcon(0, folderIcon);
        node->setFlags(node->flags() | Qt::ItemIsUserCheckable);
        node->setCheckState(0, Qt::Unchecked);
        return node;
    };

    // Find or create folder nodes along a path like "player/sub"
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

    // Populate sprite-tree items under `root` for a set of (sprite, localName) pairs.
    // `localName` is the display path used for folder/anim-group computation.
    using SpriteLeaf = QPair<SpritePtr, QString>;
    auto buildSubTree = [&](QTreeWidgetItem* root, const QVector<SpriteLeaf>& entries) {
        // Group by folder (everything before the last '/')
        QMap<QString, QVector<SpriteLeaf>> folderGroups;
        for (const auto& [sprite, localName] : entries) {
            int lastSlash = localName.lastIndexOf('/');
            QString folder = (lastSlash >= 0) ? localName.left(lastSlash) : QString();
            folderGroups[folder].append({sprite, localName});
        }

        for (auto it = folderGroups.constBegin(); it != folderGroups.constEnd(); ++it) {
            const QString& folderPath = it.key();
            const QVector<SpriteLeaf>& sprites = it.value();

            QTreeWidgetItem* folderNode = root;
            if (!folderPath.isEmpty())
                folderNode = findOrCreateFolderPath(root, folderPath.split('/'));

            // Sub-group numbered files by their digit-stripped prefix.
            QMap<QString, QVector<SpriteLeaf>> animGroups;
            for (const auto& [sprite, localName] : sprites) {
                const int ls = localName.lastIndexOf('/');
                const QString leafName = (ls >= 0) ? localName.mid(ls + 1) : localName;
                if (endsWithDigit(leafName)) {
                    const QString prefix = groupPrefix(leafName);
                    animGroups[prefix.isEmpty() ? leafName : prefix].append({sprite, leafName});
                } else {
                    animGroups[leafName].append({sprite, leafName});
                }
            }

            struct GroupInfo { bool isAnimGroup = false; };
            QMap<QString, GroupInfo> groupInfo;
            int groupNodeCount = 0;
            for (auto git = animGroups.constBegin(); git != animGroups.constEnd(); ++git) {
                const QVector<SpriteLeaf>& members = git.value();
                if (members.size() <= 1) continue;
                bool allDigits = true;
                for (const auto& [s, ln] : members)
                    if (!endsWithDigit(ln)) { allDigits = false; break; }
                if (allDigits && !groupPrefix(members.first().second).isEmpty()) {
                    groupInfo[git.key()].isAnimGroup = true;
                    ++groupNodeCount;
                }
            }

            const bool skipSingleSequenceNode = (groupNodeCount == 1);

            for (auto git = animGroups.constBegin(); git != animGroups.constEnd(); ++git) {
                const QString& prefix = git.key();
                const QVector<SpriteLeaf>& members = git.value();
                if (members.size() == 1) {
                    makeLeaf(folderNode, members.first().first, members.first().second);
                } else if (groupInfo[prefix].isAnimGroup && !skipSingleSequenceNode) {
                    auto* gNode = makeGroupNode(folderNode, prefix);
                    for (const auto& [sprite, leafName] : members)
                        makeLeaf(gNode, sprite, leafName);
                } else {
                    for (const auto& [sprite, leafName] : members)
                        makeLeaf(folderNode, sprite, leafName);
                }
            }
        }
    };

    // Collect all sprites from every layout model.
    QVector<SpritePtr> allSprites;
    for (const auto& model : m_session->layoutModels)
        allSprites.append(model.sprites);

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
            const int hiddenCount = src.hiddenFolders.size() + src.excludedFiles.size();
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

            buildSubTree(sourceNode, perSource[si]);

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
        }

        if (!unassigned.isEmpty()) {
            auto* otherNode = makeGroupNode(nullptr, tr("Other"));
            buildSubTree(otherNode, unassigned);
        }

        // ── Global "Excluded" section (shown only when "Hidden" toggle is ON) ──
        // All excluded items from every source appear in one collapsible node at
        // the bottom, so the main tree stays clean when the toggle is off.
        if (m_showHiddenItems) {
            int totalExcluded = 0;
            for (const auto& src : m_session->sources)
                totalExcluded += src.excludedFiles.size();
            if (totalExcluded > 0) {
                const QColor dimColor = QApplication::palette().color(QPalette::Disabled, QPalette::Text);
                auto* exclRoot = makeGroupNode(nullptr, tr("Excluded (%1)").arg(totalExcluded));
                QFont ef = exclRoot->font(0);
                ef.setItalic(true);
                exclRoot->setFont(0, ef);
                exclRoot->setForeground(0, dimColor);
                exclRoot->setData(0, Qt::UserRole + 2, 3); // type: excluded-section header
                exclRoot->setFlags(exclRoot->flags() & ~Qt::ItemIsUserCheckable);

                const bool multiSource = m_session->sources.size() > 1;
                for (int si = 0; si < m_session->sources.size(); ++si) {
                    const auto& src = m_session->sources[si];
                    for (const QString& relPath : src.excludedFiles) {
                        auto* exclItem = new QTreeWidgetItem(exclRoot);
                        exclItem->setText(0, multiSource ? src.name + ": " + relPath : relPath);
                        exclItem->setForeground(0, dimColor);
                        exclItem->setToolTip(0, tr("Excluded — right-click to re-include"));
                        exclItem->setData(0, Qt::UserRole + 2, 2);      // type: excluded item
                        exclItem->setData(0, Qt::UserRole + 3, si);     // source index
                        exclItem->setData(0, Qt::UserRole + 4, relPath);// relative path
                        exclItem->setFlags(exclItem->flags() & ~Qt::ItemIsUserCheckable);
                    }
                }
            }
        }
    } else {
        // Single source (or no source tracking): existing atlas-based grouping.
        for (int mi = 0; mi < m_session->layoutModels.size(); ++mi) {
            const auto& model = m_session->layoutModels[mi];
            QTreeWidgetItem* atlasRoot = nullptr;
            if (m_session->layoutModels.size() > 1)
                atlasRoot = makeGroupNode(nullptr, tr("Atlas %1").arg(mi + 1));
            QVector<SpriteLeaf> entries;
            entries.reserve(model.sprites.size());
            for (const auto& sprite : model.sprites)
                entries.append({sprite, sprite->name});
            buildSubTree(atlasRoot, entries);
        }
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
