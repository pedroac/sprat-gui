#include "AtlasesManagementWorkspace.h"
#include "NavigatorPanel.h"
#include "NavigatorTreeWidget.h"
#include "ProjectSession.h"
#include "SpriteTreeUtils.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMap>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QSet>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <functional>

// ---------------------------------------------------------------------------

AtlasesManagementWorkspace::AtlasesManagementWorkspace(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void AtlasesManagementWorkspace::setupUi() {
    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    auto* splitter = m_splitter;
    rootLayout->addWidget(splitter);

    // --- Left panel: atlas list ---
    auto* leftPanel = new QWidget(splitter);
    leftPanel->setMinimumWidth(150);
    leftPanel->setMaximumWidth(220);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(8, 8, 8, 8);
    leftLayout->setSpacing(4);

    auto* atlasLabel = new QLabel(tr("Atlases"), leftPanel);
    QFont boldFont = atlasLabel->font();
    boldFont.setBold(true);
    atlasLabel->setFont(boldFont);
    leftLayout->addWidget(atlasLabel);

    m_atlasList = new QListWidget(leftPanel);
    m_atlasList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_atlasList->setAcceptDrops(true);
    m_atlasList->installEventFilter(this);
    m_atlasList->viewport()->installEventFilter(this);
    leftLayout->addWidget(m_atlasList);

    auto* btnRow = new QHBoxLayout();
    m_addBtn = new QPushButton(QApplication::style()->standardIcon(QStyle::SP_FileDialogNewFolder), tr("Add"), leftPanel);
    m_addBtn->setToolTip(tr("Add atlas"));
    m_removeBtn = new QPushButton(QApplication::style()->standardIcon(QStyle::SP_DialogDiscardButton), tr("Remove"), leftPanel);
    m_removeBtn->setToolTip(tr("Remove selected atlas"));
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_removeBtn);
    btnRow->addStretch();
    leftLayout->addLayout(btnRow);

    splitter->addWidget(leftPanel);

    // --- Center: stacked widget (Navigation = sprite tree, Layout = canvas pane) ---
    m_centerStack = new QStackedWidget(this);

    m_navigator = new NavigatorPanel(m_centerStack);
    {
        NavigatorPanel::Config cfg;
        cfg.checkboxes    = true;
        cfg.showHidden    = false;
        cfg.atlasCombo    = false;
        cfg.filterBar     = true;
        cfg.selectionMode = QAbstractItemView::ExtendedSelection;
        m_navigator->configure(cfg);
    }
    m_centerStack->addWidget(m_navigator);  // page 0: Navigation

    m_canvasPane = new QWidget(m_centerStack);
    auto* canvasPaneLayout = new QVBoxLayout(m_canvasPane);
    canvasPaneLayout->setContentsMargins(0, 0, 0, 0);
    m_centerStack->addWidget(m_canvasPane);  // page 1: Layout

    splitter->addWidget(m_centerStack);

    // --- Right panel ---
    m_rightPanel = new QWidget(splitter);
    auto* rightPanel = m_rightPanel;
    rightPanel->setMinimumWidth(180);
    rightPanel->setMaximumWidth(280);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(8, 8, 8, 8);
    rightLayout->setSpacing(6);

    // Mode toggle
    auto* modeRow = new QHBoxLayout();
    modeRow->setSpacing(2);
    auto* navBtn    = new QPushButton(tr("Navigation"), rightPanel);
    auto* layoutBtn = new QPushButton(tr("Layout"), rightPanel);
    navBtn->setCheckable(true);
    layoutBtn->setCheckable(true);
    navBtn->setChecked(true);
    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->addButton(navBtn,    0);
    m_modeGroup->addButton(layoutBtn, 1);
    modeRow->addWidget(navBtn);
    modeRow->addWidget(layoutBtn);
    rightLayout->addLayout(modeRow);

    rightLayout->addSpacing(4);

    // Atlas name (no group box)
    auto* nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel(tr("Name:"), rightPanel));
    m_atlasNameEdit = new QLineEdit(rightPanel);
    m_atlasNameEdit->setPlaceholderText(tr("Atlas name"));
    nameRow->addWidget(m_atlasNameEdit, 1);
    rightLayout->addLayout(nameRow);

    // Sprite search — visible in Layout mode only; Navigation mode uses NavigatorPanel's filter bar
    m_spriteFilterEdit = new QLineEdit(rightPanel);
    m_spriteFilterEdit->setPlaceholderText(tr("Search sprites..."));
    m_spriteFilterEdit->setClearButtonEnabled(true);
    m_spriteFilterEdit->setVisible(false);
    rightLayout->addWidget(m_spriteFilterEdit);
    m_spriteFilterLabel = new QLabel(rightPanel);
    m_spriteFilterLabel->setStyleSheet(QStringLiteral("color: #888; font-size: 11px;"));
    m_spriteFilterLabel->setVisible(false);
    rightLayout->addWidget(m_spriteFilterLabel);

    // Zoom row (visible in Layout mode only)
    m_zoomRow = new QWidget(rightPanel);
    auto* zoomRowLayout = new QHBoxLayout(m_zoomRow);
    zoomRowLayout->setContentsMargins(0, 0, 0, 0);
    zoomRowLayout->addWidget(new QLabel(tr("Zoom:"), m_zoomRow));
    m_zoomSpin = new QDoubleSpinBox(m_zoomRow);
    m_zoomSpin->setRange(10.0, 800.0);
    m_zoomSpin->setValue(100.0);
    m_zoomSpin->setSuffix(QStringLiteral("%"));
    m_zoomSpin->setSingleStep(10.0);
    zoomRowLayout->addWidget(m_zoomSpin, 1);
    m_zoomRow->setVisible(false);
    rightLayout->addWidget(m_zoomRow);

    // Resolution row
    auto* resRow = new QHBoxLayout();
    resRow->addWidget(new QLabel(tr("Source Resolution:"), rightPanel));
    m_resolutionCombo = new QComboBox(rightPanel);
    resRow->addWidget(m_resolutionCombo, 1);
    rightLayout->addLayout(resRow);

    // Pack efficiency stats (shown when layout data is available)
    m_statsLabel = new QLabel(rightPanel);
    m_statsLabel->setWordWrap(true);
    m_statsLabel->setStyleSheet(QStringLiteral("color: #888; font-size: 11px;"));
    m_statsLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_statsLabel->setVisible(false);
    rightLayout->addWidget(m_statsLabel);

    // Profiles group
    auto* profilesGroup = new QGroupBox(tr("Profiles"), rightPanel);
    auto* profilesLayout = new QVBoxLayout(profilesGroup);
    profilesLayout->setContentsMargins(8, 8, 8, 8);
    profilesLayout->setSpacing(4);

    // "From Default" checkbox — hidden for the Default (neutral) atlas
    m_profilesGlobalCheckBox = new QCheckBox(tr("From Default"), profilesGroup);
    m_profilesGlobalCheckBox->setChecked(true);
    m_profilesGlobalCheckBox->setToolTip(tr(
        "When checked, this atlas uses the Default atlas profile selection.\n"
        "When unchecked, this atlas has its own profile selection."));
    profilesLayout->addWidget(m_profilesGlobalCheckBox);

    // Combobox shown when "From Default" is checked on a non-Default atlas
    m_profileCombo = new QComboBox(profilesGroup);
    profilesLayout->addWidget(m_profileCombo);

    // Checklist shown for the Default atlas or when "From Default" is unchecked
    m_profileList = new QListWidget(profilesGroup);
    m_profileList->setVisible(false);
    profilesLayout->addWidget(m_profileList);

    // Manage button — shown only in combobox mode
    m_manageBtn = new QPushButton(tr("Manage..."), profilesGroup);
    profilesLayout->addWidget(m_manageBtn);
    connect(m_manageBtn, &QPushButton::clicked, this, &AtlasesManagementWorkspace::manageProfilesRequested);

    rightLayout->addWidget(profilesGroup);

    rightLayout->addStretch();

    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);

    // Connections
    connect(m_atlasList, &QListWidget::currentRowChanged,
            this, &AtlasesManagementWorkspace::onAtlasListSelectionChanged);
    connect(m_atlasNameEdit, &QLineEdit::editingFinished,
            this, &AtlasesManagementWorkspace::onAtlasNameEditFinished);
    connect(m_addBtn, &QPushButton::clicked,
            this, &AtlasesManagementWorkspace::onAddClicked);
    connect(m_removeBtn, &QPushButton::clicked,
            this, &AtlasesManagementWorkspace::onRemoveClicked);
    connect(m_navigator->tree(), &NavigatorTreeWidget::customContextMenuRequested,
            this, &AtlasesManagementWorkspace::onSpriteContextMenu);
    connect(m_spriteFilterEdit, &QLineEdit::textChanged,
            this, &AtlasesManagementWorkspace::filterSpriteTree);
    connect(m_navigator, &NavigatorPanel::excludeKeyPressed,
            this, [this](QTreeWidgetItem* /*item*/) {
        const int srcRow = m_atlasList->currentRow();
        if (srcRow < 0 || srcRow >= m_atlases.size()) return;
        QSet<QString> pathSet;
        // Collect from all visually selected items (not just the current item).
        for (auto* selected : m_navigator->tree()->selectedItems())
            for (const QString& p : SpriteTreeUtils::collectDescendantPaths(selected))
                pathSet.insert(p);
        if (pathSet.isEmpty()) return;
        QStringList paths(pathSet.cbegin(), pathSet.cend());
        if (m_atlases[srcRow].isExcluded) {
            for (int i = 0; i < m_atlases.size(); ++i) {
                if (m_atlases[i].isNeutral) {
                    emit moveSpritesRequested(paths, srcRow, i);
                    break;
                }
            }
        } else {
            for (int i = 0; i < m_atlases.size(); ++i) {
                if (m_atlases[i].isExcluded) {
                    emit moveSpritesRequested(paths, srcRow, i);
                    break;
                }
            }
        }
    });

    connect(navBtn,    &QPushButton::clicked, this, [this]() { onViewModeToggled(0); });
    connect(layoutBtn, &QPushButton::clicked, this, [this]() { onViewModeToggled(1); });

    connect(m_zoomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double val) {
        if (!m_syncing) emit zoomChanged(val);
    });

    connect(m_resolutionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (!m_syncing) emit resolutionChanged(m_resolutionCombo->currentText());
    });

    connect(m_profilesGlobalCheckBox, &QCheckBox::toggled, this, [this](bool global) {
        if (m_syncing) return;
        setProfilesGlobal(global);
        emit profilesGlobalChanged(global);
    });

    connect(m_profileList, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (m_syncing) return;
        // Always keep at least one profile enabled.
        if (item->checkState() == Qt::Unchecked) {
            bool anyChecked = false;
            for (int i = 0; i < m_profileList->count(); ++i) {
                if (m_profileList->item(i)->checkState() == Qt::Checked) { anyChecked = true; break; }
            }
            if (!anyChecked) {
                m_syncing = true;
                item->setCheckState(Qt::Checked);
                m_syncing = false;
                return;
            }
            if (item == m_profileList->currentItem())
                ensureValidProfileSelection();
        }
        if (isProfilesGlobal()) {
            m_syncing = true;
            emit profileEnablementChanged(enabledProfiles());
            m_syncing = false;
            rebuildProfileCombo();
        } else {
            // Per-atlas mode: push updated enablement into the selected atlas
            const int row = m_atlasList->currentRow();
            if (row >= 0 && row < m_atlases.size()) {
                QStringList checked;
                for (int i = 0; i < m_profileList->count(); ++i) {
                    auto* it = m_profileList->item(i);
                    if (it->checkState() == Qt::Checked)
                        checked << it->data(Qt::UserRole).toString();
                }
                m_atlases[row].exportConfig.profiles = checked;
                emit atlasExportProfilesChanged(row, checked);
            }
        }
        updateProfileListEnabledStates();
    });
    connect(m_profileList, &QListWidget::currentItemChanged,
            this, [this](QListWidgetItem* current, QListWidgetItem*) {
        if (m_syncing || !current) return;
        if (current->checkState() == Qt::Unchecked)
            current->setCheckState(Qt::Checked);  // itemChanged handles enablement signals
        emit selectedProfileChanged(current->data(Qt::UserRole).toString());
    });

    connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (m_syncing || idx < 0) return;
        emit selectedProfileChanged(m_profileCombo->itemData(idx).toString());
    });
}

void AtlasesManagementWorkspace::setSources(const QVector<ProjectSource>& sources) {
    m_sources = sources;  // kept for API compatibility; NavigatorPanel uses session->sources directly
}

void AtlasesManagementWorkspace::setSession(const ProjectSession* session) {
    m_session = session;
}

static QString atlasItemLabel(const AtlasEntry& a) {
    return QStringLiteral("%1 (%2)").arg(a.name).arg(a.spritePaths.size());
}

void AtlasesManagementWorkspace::setAtlases(const QVector<AtlasEntry>& atlases, int activeIndex) {
    m_atlases = atlases;

    m_atlasList->blockSignals(true);
    m_atlasList->clear();
    for (const auto& atlas : atlases) {
        auto* item = new QListWidgetItem(atlasItemLabel(atlas));
        if (atlas.isNeutral) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
            item->setToolTip(tr("Default atlas — receives all unassigned sprites"));
        } else if (atlas.isExcluded) {
            QFont f = item->font();
            f.setItalic(true);
            item->setFont(f);
            item->setForeground(QApplication::palette().color(QPalette::Disabled, QPalette::Text));
            item->setToolTip(tr("Excluded sprites — not included in any atlas output"));
        }
        m_atlasList->addItem(item);
    }
    const int selectIdx = qBound(0, activeIndex, m_atlasList->count() - 1);
    m_atlasList->setCurrentRow(selectIdx);
    m_atlasList->blockSignals(false);

    updateRightPanel();
    refreshSpriteList(atlases);
}

// ---------------------------------------------------------------------------

void AtlasesManagementWorkspace::refreshSpriteList(const QVector<AtlasEntry>& allAtlases, const QStringList& /*activeFramePaths*/) {
    m_atlases = allAtlases;

    // Keep atlas list labels (name + sprite count) in sync.
    for (int i = 0; i < allAtlases.size() && i < m_atlasList->count(); ++i)
        m_atlasList->item(i)->setText(atlasItemLabel(allAtlases[i]));

    // Rebuild the navigator tree for the currently selected atlas.
    if (m_session && m_navigator)
        m_navigator->refresh(m_session, false, m_atlasList->currentRow());

    updateStatsLabel();
}

void AtlasesManagementWorkspace::updateStatsLabel() {
    if (!m_statsLabel) return;

    const int row = m_atlasList ? m_atlasList->currentRow() : -1;
    if (row < 0 || row >= m_atlases.size()) {
        m_statsLabel->setVisible(false);
        return;
    }

    const AtlasEntry& atlas = m_atlases[row];
    if (atlas.isExcluded || atlas.layoutModels.isEmpty()) {
        m_statsLabel->setVisible(false);
        return;
    }

    qint64 totalArea = 0;
    qint64 usedArea  = 0;
    for (const auto& lm : atlas.layoutModels) {
        totalArea += static_cast<qint64>(lm.atlasWidth) * lm.atlasHeight;
        for (const auto& sprite : lm.sprites) {
            if (sprite)
                usedArea += static_cast<qint64>(sprite->rect.width()) * sprite->rect.height();
        }
    }

    if (totalArea == 0) {
        m_statsLabel->setVisible(false);
        return;
    }

    auto fmtArea = [](qint64 px) -> QString {
        if (px >= 1'000'000)
            return QStringLiteral("%1 Mpx\u00B2").arg(px / 1'000'000.0, 0, 'f', 1);
        if (px >= 1'000)
            return QStringLiteral("%1 kpx\u00B2").arg(px / 1'000.0, 0, 'f', 1);
        return QStringLiteral("%1 px\u00B2").arg(px);
    };

    const double efficiency = 100.0 * usedArea / totalArea;
    const qint64 wastedArea = totalArea - usedArea;
    const int    pageCount  = atlas.layoutModels.size();

    QString text = tr("Pack: %1%").arg(efficiency, 0, 'f', 1);
    if (pageCount > 1)
        text += tr("  (%1 pages)").arg(pageCount);
    text += QLatin1Char('\n');
    text += tr("Used: %1  Wasted: %2").arg(fmtArea(usedArea), fmtArea(wastedArea));

    m_statsLabel->setText(text);
    m_statsLabel->setVisible(true);
}

int AtlasesManagementWorkspace::selectedAtlasIndex() const {
    return m_atlasList->currentRow();
}

void AtlasesManagementWorkspace::updateRightPanel() {
    const int row = m_atlasList->currentRow();
    const bool valid = row >= 0 && row < m_atlases.size();
    const bool isExcluded = valid && m_atlases[row].isExcluded;

    // Excluded atlas needs no right panel and no Layout mode.
    if (m_rightPanel) m_rightPanel->setVisible(!isExcluded);
    if (isExcluded) {
        if (m_viewMode == ViewMode::Layout) {
            onViewModeToggled(0);
            if (m_modeGroup && m_modeGroup->button(0))
                m_modeGroup->button(0)->setChecked(true);
        }
        return;
    }

    m_atlasNameEdit->setEnabled(valid && !m_atlases[row].isNeutral);
    m_removeBtn->setEnabled(valid && !m_atlases[row].isNeutral);
    if (valid) {
        m_atlasNameEdit->blockSignals(true);
        m_atlasNameEdit->setText(m_atlases[row].name);
        m_atlasNameEdit->blockSignals(false);
    }
    // In per-atlas mode refresh the profile checkboxes for the new selection
    if (!isProfilesGlobal() && valid && !m_atlases[row].isNeutral)
        loadAtlasProfilesIntoList(row);
    updateProfilesVisibility();
}

void AtlasesManagementWorkspace::updateProfilesVisibility() {
    const int row = m_atlasList ? m_atlasList->currentRow() : -1;
    const bool isNeutral = (row >= 0 && row < m_atlases.size()) && m_atlases[row].isNeutral;
    const bool globalChecked = m_profilesGlobalCheckBox && m_profilesGlobalCheckBox->isChecked();

    // Default (neutral) atlas: hide the "From Default" checkbox, show checklist
    // Non-default, From Default checked: show combobox + Manage, hide checklist
    // Non-default, From Default unchecked: show checklist, hide combobox + Manage
    const bool showChecklist = isNeutral || !globalChecked;

    if (m_profilesGlobalCheckBox) m_profilesGlobalCheckBox->setVisible(!isNeutral);
    if (m_profileList)  m_profileList->setVisible(showChecklist);
    if (m_profileCombo) m_profileCombo->setVisible(!showChecklist);
    if (m_manageBtn)    m_manageBtn->setVisible(!showChecklist);
}

void AtlasesManagementWorkspace::rebuildProfileCombo() {
    if (!m_profileCombo || !m_profileList) return;
    const QString current = m_profileCombo->currentData().toString();
    m_profileCombo->blockSignals(true);
    m_profileCombo->clear();
    for (int i = 0; i < m_profileList->count(); ++i) {
        auto* item = m_profileList->item(i);
        if (item->checkState() == Qt::Checked)
            m_profileCombo->addItem(item->text(), item->data(Qt::UserRole));
    }
    const int idx = m_profileCombo->findData(current);
    m_profileCombo->setCurrentIndex(idx >= 0 ? idx : (m_profileCombo->count() > 0 ? 0 : -1));
    m_profileCombo->blockSignals(false);
}

// ---------------------------------------------------------------------------
// Event filter — handles sprite drops on the atlas list.
// Accepts "application/x-sprat-sprite" from both the sprite tree (within this
// workspace) and the main navigator tree widget.
// ---------------------------------------------------------------------------
void AtlasesManagementWorkspace::setDragHoverRow(int row) {
    if (m_dragHoverRow == row) return;
    // Clear previous highlight
    if (m_dragHoverRow >= 0 && m_dragHoverRow < m_atlasList->count())
        m_atlasList->item(m_dragHoverRow)->setBackground(QBrush());
    m_dragHoverRow = row;
    // Apply new highlight
    if (m_dragHoverRow >= 0 && m_dragHoverRow < m_atlasList->count()) {
        QColor c = m_atlasList->palette().color(QPalette::Highlight);
        c.setAlpha(100);
        m_atlasList->item(m_dragHoverRow)->setBackground(c);
    }
}

bool AtlasesManagementWorkspace::eventFilter(QObject* obj, QEvent* event) {
    // DEL key is now handled via NavigatorPanel::excludeKeyPressed signal.

    if (obj != m_atlasList && obj != m_atlasList->viewport())
        return QWidget::eventFilter(obj, event);

    static const char mime[] = "application/x-sprat-sprite";

    // Normalize coordinates to viewport-relative, which is what itemAt() expects.
    auto toViewportPos = [&](const QPoint& pos) -> QPoint {
        return (obj == m_atlasList->viewport())
            ? pos
            : m_atlasList->viewport()->mapFrom(m_atlasList, pos);
    };
    auto viewportPos = [&](const QDropEvent* e) -> QPoint {
        return toViewportPos(e->position().toPoint());
    };

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* e = static_cast<QMouseEvent*>(event);
        if (e->button() == Qt::RightButton) {
            // Save which atlas is currently shown so ContextMenu can use it.
            // Consuming the event prevents Qt from changing the list selection,
            // which would rebuild the navigator and lose the checked state.
            m_contextMenuSourceAtlasRow = m_atlasList->currentRow();
            return true;
        }
        return QWidget::eventFilter(obj, event);
    }
    case QEvent::ContextMenu: {
        auto* e = static_cast<QContextMenuEvent*>(event);
        const QPoint vpPos    = toViewportPos(e->pos());
        QListWidgetItem* item = m_atlasList->itemAt(vpPos);
        const int tgtRow      = item ? m_atlasList->row(item) : -1;
        const int srcRow      = m_contextMenuSourceAtlasRow;
        m_contextMenuSourceAtlasRow = -1;

        // Only show when right-clicking a different atlas while checked sprites exist.
        const QStringList checked = m_navigator ? m_navigator->checkedPaths() : QStringList{};
        if (tgtRow >= 0 && tgtRow < m_atlases.size()
                && tgtRow != srcRow && srcRow >= 0
                && !checked.isEmpty()) {
            QMenu menu(this);
            menu.addAction(tr("Move checked sprites to \"%1\"").arg(m_atlases[tgtRow].name),
                           this, [this, checked, srcRow, tgtRow]() {
                emit moveSpritesRequested(checked, srcRow, tgtRow);
            });
            menu.exec(m_atlasList->viewport()->mapToGlobal(vpPos));
            return true;
        }
        return QWidget::eventFilter(obj, event);
    }
    case QEvent::DragEnter: {
        auto* e = static_cast<QDragEnterEvent*>(event);
        if (e->mimeData()->hasFormat(mime)) {
            e->acceptProposedAction();
            QListWidgetItem* item = m_atlasList->itemAt(viewportPos(e));
            setDragHoverRow(item ? m_atlasList->row(item) : -1);
        }
        return true;
    }
    case QEvent::DragMove: {
        auto* e = static_cast<QDragMoveEvent*>(event);
        if (e->mimeData()->hasFormat(mime)) {
            QListWidgetItem* item = m_atlasList->itemAt(viewportPos(e));
            setDragHoverRow(item ? m_atlasList->row(item) : -1);
            e->acceptProposedAction();
        }
        return true;
    }
    case QEvent::DragLeave: {
        setDragHoverRow(-1);
        return true;
    }
    case QEvent::Drop: {
        setDragHoverRow(-1);
        auto* e = static_cast<QDropEvent*>(event);
        if (e->mimeData()->hasFormat(mime)) {
            const QStringList paths = QString::fromUtf8(
                e->mimeData()->data(mime)).split('\n', Qt::SkipEmptyParts);
            QListWidgetItem* target = m_atlasList->itemAt(viewportPos(e));
            if (target) {
                emit moveSpritesRequested(paths, m_atlasList->currentRow(), m_atlasList->row(target));
                e->acceptProposedAction();
            } else if (!paths.isEmpty()) {
                // Dropped on empty area — resolve atlas name from group name or first sprite.
                QString atlasName;
                if (e->mimeData()->hasFormat("application/x-sprat-group-name"))
                    atlasName = QString::fromUtf8(
                        e->mimeData()->data("application/x-sprat-group-name")).trimmed();
                if (atlasName.isEmpty())
                    atlasName = QFileInfo(paths.first()).baseName();

                int existingIdx = -1;
                for (int i = 0; i < m_atlases.size(); ++i) {
                    if (!m_atlases[i].isExcluded && m_atlases[i].name == atlasName) {
                        existingIdx = i;
                        break;
                    }
                }

                const int srcRow = m_atlasList->currentRow();
                if (existingIdx >= 0 && srcRow >= 0)
                    emit moveSpritesRequested(paths, srcRow, existingIdx);
                else if (existingIdx < 0)
                    emit createAtlasFromGroupRequested(atlasName, paths);
                else { e->ignore(); return true; }
                e->acceptProposedAction();
            } else {
                e->ignore();
            }
        }
        return true;
    }
    default:
        return QWidget::eventFilter(obj, event);
    }
}

void AtlasesManagementWorkspace::filterSpriteTree(const QString& text) {
    if (m_viewMode == ViewMode::Layout) {
        // In Layout mode: forward the query to the canvas for dim-based filtering.
        if (m_spriteFilterLabel) m_spriteFilterLabel->setVisible(false);
        emit layoutFilterChanged(text);
    }
    // Navigation mode: NavigatorPanel handles filtering via its own filter bar.
}

void AtlasesManagementWorkspace::onAtlasListSelectionChanged() {
    updateRightPanel();
    if (m_spriteFilterEdit) {
        m_spriteFilterEdit->blockSignals(true);
        m_spriteFilterEdit->clear();
        m_spriteFilterEdit->blockSignals(false);
    }
    if (m_spriteFilterLabel) m_spriteFilterLabel->setVisible(false);
    // Clear canvas dim filter when in Layout mode.
    filterSpriteTree(QString());
    refreshSpriteList(m_atlases);
    emit atlasSelected(m_atlasList->currentRow());
}

void AtlasesManagementWorkspace::onAtlasNameEditFinished() {
    const int row = m_atlasList->currentRow();
    if (row < 0 || row >= m_atlases.size() || m_atlases[row].isNeutral || m_atlases[row].isExcluded) return;
    const QString newName = m_atlasNameEdit->text().trimmed();
    if (newName.isEmpty() || newName == m_atlases[row].name) return;
    emit atlasRenamed(row, newName);
}

void AtlasesManagementWorkspace::onAddClicked() {
    emit addAtlasRequested();
}

void AtlasesManagementWorkspace::onRemoveClicked() {
    const int row = m_atlasList->currentRow();
    if (row < 0 || row >= m_atlases.size() || m_atlases[row].isNeutral || m_atlases[row].isExcluded) return;
    emit removeAtlasRequested(row);
}


void AtlasesManagementWorkspace::onSpriteContextMenu(const QPoint& pos) {
    if (!m_navigator) return;

    QTreeWidgetItem* clickedItem = m_navigator->tree()->itemAt(pos);
    if (!clickedItem) return;

    // Detect the type of the right-clicked item.
    // Source nodes have UserRole+1 (source index) but no UserRole (no sprite).
    const bool isSourceNode = clickedItem->data(0, Qt::UserRole + 1).isValid()
        && !clickedItem->data(0, Qt::UserRole).isValid();
    // Group nodes are non-leaf, non-source, non-special folder nodes.
    const bool isGroupNode = !isSourceNode
        && clickedItem->childCount() > 0
        && !clickedItem->data(0, Qt::UserRole).isValid()
        && clickedItem->data(0, Qt::UserRole + 2).toInt() == 0;

    const auto selectedItems = m_navigator->tree()->selectedItems();

    // For group/source nodes use the clicked node directly — no selection needed.
    // For leaf nodes use the full selection for multi-select support.
    QSet<QString> pathSet;
    if (isGroupNode || isSourceNode) {
        const auto paths = SpriteTreeUtils::collectDescendantPaths(clickedItem);
        pathSet = QSet<QString>(paths.begin(), paths.end());
    } else {
        for (auto* item : selectedItems)
            for (const QString& p : SpriteTreeUtils::collectDescendantPaths(item))
                pathSet.insert(p);
    }
    if (pathSet.isEmpty()) return;
    QStringList paths(pathSet.cbegin(), pathSet.cend());

    const int srcRow = m_atlasList->currentRow();
    const bool isExcludedAtlas = (srcRow >= 0 && srcRow < m_atlases.size())
                                 && m_atlases[srcRow].isExcluded;

    QMenu menu(this);
    bool hadItems = false;
    auto addSep = [&]() { if (hadItems) { menu.addSeparator(); hadItems = false; } };

    // Find special atlas indices.
    int excludedAtlasIdx = -1;
    int neutralAtlasIdx  = -1;
    for (int i = 0; i < m_atlases.size(); ++i) {
        if (m_atlases[i].isExcluded) excludedAtlasIdx = i;
        if (m_atlases[i].isNeutral)  neutralAtlasIdx  = i;
    }

    // ── Source-specific actions ─────────────────────────────────────────────
    if (isSourceNode) {
        // Pre-compute groups now, while clickedItem is still valid (popup is async).
        QVector<QPair<QString, QStringList>> groups;
        if (clickedItem) {
            std::function<void(QTreeWidgetItem*, QStringList&)> collectPaths =
                [&](QTreeWidgetItem* node, QStringList& result) {
                    auto sp = node->data(0, Qt::UserRole).value<SpritePtr>();
                    if (sp && !sp->path.isEmpty()) {
                        result << sp->path;
                    } else {
                        for (int i = 0; i < node->childCount(); ++i)
                            collectPaths(node->child(i), result);
                    }
                };
            for (int i = 0; i < clickedItem->childCount(); ++i) {
                QTreeWidgetItem* child = clickedItem->child(i);
                if (child->data(0, Qt::UserRole).isValid()) continue;      // leaf
                if (child->data(0, Qt::UserRole + 2).toInt() > 0) continue; // special
                QStringList groupPaths;
                collectPaths(child, groupPaths);
                if (!groupPaths.isEmpty())
                    groups.append({child->text(0), groupPaths});
            }
        }
        if (!groups.isEmpty()) {
            menu.addAction(tr("Autocreate atlases"), this, [this, groups]() {
                emit autoCreateAtlasesRequested(groups);
            });
            hadItems = true;
            addSep();
        }
    }

    // ── Group-specific actions ──────────────────────────────────────────────
    if (isGroupNode && clickedItem) {
        const QString groupName = clickedItem->text(0);

        // Check whether a non-excluded atlas with this exact name already exists.
        int existingAtlasIdx = -1;
        for (int i = 0; i < m_atlases.size(); ++i) {
            if (!m_atlases[i].isExcluded && m_atlases[i].name == groupName) {
                existingAtlasIdx = i;
                break;
            }
        }

        if (existingAtlasIdx >= 0 && existingAtlasIdx != srcRow) {
            // Atlas exists and is different from the current one — move sprites there.
            const int tgtIdx = existingAtlasIdx;
            menu.addAction(tr("Move \"%1\" to atlas").arg(groupName),
                           this, [this, paths, srcRow, tgtIdx]() {
                emit moveSpritesRequested(paths, srcRow, tgtIdx);
            });
            hadItems = true;
            addSep();
        } else if (existingAtlasIdx < 0) {
            // No atlas with this name — offer to create one.
            menu.addAction(tr("Create atlas from \"%1\"").arg(groupName),
                           this, [this, groupName, paths]() {
                emit createAtlasFromGroupRequested(groupName, paths);
            });
            hadItems = true;
            addSep();
        }
        // If existingAtlasIdx == srcRow the group is already this atlas — no action needed.
    }

    // ── Exclude / Re-include ────────────────────────────────────────────────
    if (!isExcludedAtlas && excludedAtlasIdx >= 0) {
        const int tgt = excludedAtlasIdx;
        QAction* excludeAct = menu.addAction(tr("Exclude"));
        connect(excludeAct, &QAction::triggered, this, [this, paths, srcRow, tgt]() {
            emit moveSpritesRequested(paths, srcRow, tgt);
        });
        menu.addSeparator();
    } else if (isExcludedAtlas && neutralAtlasIdx >= 0) {
        const int tgt = neutralAtlasIdx;
        QAction* reincludeAct = menu.addAction(tr("Re-include"));
        connect(reincludeAct, &QAction::triggered, this, [this, paths, srcRow, tgt]() {
            emit moveSpritesRequested(paths, srcRow, tgt);
        });
        menu.addSeparator();
    }

    // ── Move to atlas submenu ───────────────────────────────────────────────
    auto* moveMenu = menu.addMenu(tr("Move to atlas"));
    for (int i = 0; i < m_atlases.size(); ++i) {
        if (i == srcRow) continue;
        if (m_atlases[i].isExcluded) continue;
        const int atlasIdx = i;
        QAction* act = moveMenu->addAction(m_atlases[i].name);
        connect(act, &QAction::triggered, this, [this, paths, srcRow, atlasIdx]() {
            emit moveSpritesRequested(paths, srcRow, atlasIdx);
        });
    }

    const QPoint globalPos = m_navigator->tree()->viewport()->mapToGlobal(pos);
    menu.exec(globalPos);
}

// ---------------------------------------------------------------------------
// Profile management
// ---------------------------------------------------------------------------

void AtlasesManagementWorkspace::setProfiles(const QStringList& names, const QStringList& labels,
                                              const QString& currentProfile,
                                              const QStringList& enabledProfiles) {
    if (!m_profileList) return;
    m_syncing = true;
    m_profileList->blockSignals(true);
    m_profileList->clear();

    for (int i = 0; i < names.size(); ++i) {
        const QString& name  = names.at(i);
        const QString  label = (i < labels.size()) ? labels.at(i) : name;
        auto* item = new QListWidgetItem(label, m_profileList);
        item->setData(Qt::UserRole, name);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        const bool enabled = enabledProfiles.isEmpty() ? (i == 0) : enabledProfiles.contains(name);
        item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
    }

    // If no profile matched (e.g. stale names from a different project), enable the first one.
    {
        bool anyChecked = false;
        for (int i = 0; i < m_profileList->count(); ++i)
            if (m_profileList->item(i)->checkState() == Qt::Checked) { anyChecked = true; break; }
        if (!anyChecked && m_profileList->count() > 0)
            m_profileList->item(0)->setCheckState(Qt::Checked);
    }

    bool selected = false;
    for (int i = 0; i < m_profileList->count(); ++i) {
        QListWidgetItem* item = m_profileList->item(i);
        if (item->data(Qt::UserRole).toString() == currentProfile
                && item->checkState() == Qt::Checked) {
            m_profileList->setCurrentItem(item);
            selected = true;
            break;
        }
    }
    if (!selected) {
        for (int i = 0; i < m_profileList->count(); ++i) {
            QListWidgetItem* item = m_profileList->item(i);
            if (item->checkState() == Qt::Checked) {
                m_profileList->setCurrentItem(item);
                break;
            }
        }
    }

    m_profileList->blockSignals(false);

    // Also populate the profile combobox (used in "From Default" mode) —
    // built from the list's checked state so it always stays in sync.
    if (m_profileCombo) {
        m_profileCombo->blockSignals(true);
        m_profileCombo->clear();
        for (int i = 0; i < m_profileList->count(); ++i) {
            auto* item = m_profileList->item(i);
            if (item->checkState() == Qt::Checked)
                m_profileCombo->addItem(item->text(), item->data(Qt::UserRole));
        }
        const int idx = m_profileCombo->findData(currentProfile);
        m_profileCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        m_profileCombo->blockSignals(false);
    }

    m_syncing = false;

    // In per-atlas mode: save the new global profiles then show the atlas-specific ones
    const bool globalChecked = m_profilesGlobalCheckBox && m_profilesGlobalCheckBox->isChecked();
    if (!globalChecked) {
        m_savedGlobalProfiles.clear();
        for (int i = 0; i < m_profileList->count(); ++i) {
            if (m_profileList->item(i)->checkState() == Qt::Checked)
                m_savedGlobalProfiles << m_profileList->item(i)->data(Qt::UserRole).toString();
        }
        const int row = m_atlasList ? m_atlasList->currentRow() : -1;
        if (row >= 0 && row < m_atlases.size() && !m_atlases[row].isNeutral)
            loadAtlasProfilesIntoList(row);
    }

    updateProfileListEnabledStates();
    updateProfilesVisibility();
}

void AtlasesManagementWorkspace::setSelectedProfile(const QString& name) {
    if (!m_profileList) return;
    m_syncing = true;
    for (int i = 0; i < m_profileList->count(); ++i) {
        QListWidgetItem* item = m_profileList->item(i);
        if (item->data(Qt::UserRole).toString() == name) {
            m_profileList->setCurrentItem(item);
            break;
        }
    }
    if (m_profileCombo) {
        const int idx = m_profileCombo->findData(name);
        if (idx >= 0) m_profileCombo->setCurrentIndex(idx);
    }
    m_syncing = false;
}

QString AtlasesManagementWorkspace::selectedProfile() const {
    const int row = m_atlasList ? m_atlasList->currentRow() : -1;
    const bool isNeutral = (row >= 0 && row < m_atlases.size()) && m_atlases[row].isNeutral;
    const bool globalChecked = m_profilesGlobalCheckBox && m_profilesGlobalCheckBox->isChecked();
    // Combobox mode: non-neutral atlas with "From Default" checked
    if (!isNeutral && globalChecked && m_profileCombo)
        return m_profileCombo->currentData().toString();
    if (!m_profileList) return {};
    QListWidgetItem* item = m_profileList->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString{};
}

QStringList AtlasesManagementWorkspace::enabledProfiles() const {
    // Per-atlas mode: list shows atlas-specific profiles; return the saved global state
    if (!isProfilesGlobal())
        return m_savedGlobalProfiles;

    QStringList result;
    if (!m_profileList) return result;
    for (int i = 0; i < m_profileList->count(); ++i) {
        QListWidgetItem* item = m_profileList->item(i);
        if (item->checkState() == Qt::Checked)
            result << item->data(Qt::UserRole).toString();
    }
    return result;
}

void AtlasesManagementWorkspace::ensureValidProfileSelection() {
    if (!m_profileList || m_syncing) return;
    m_syncing = true;
    for (int i = 0; i < m_profileList->count(); ++i) {
        QListWidgetItem* item = m_profileList->item(i);
        if (item->checkState() == Qt::Checked) {
            m_profileList->setCurrentItem(item);
            m_syncing = false;
            emit selectedProfileChanged(item->data(Qt::UserRole).toString());
            return;
        }
    }
    m_profileList->setCurrentItem(nullptr);
    m_syncing = false;
}

void AtlasesManagementWorkspace::updateProfileListEnabledStates() {
    if (!m_profileList) return;
    int checkedCount = 0;
    QListWidgetItem* onlyChecked = nullptr;
    for (int i = 0; i < m_profileList->count(); ++i) {
        auto* item = m_profileList->item(i);
        if (item->checkState() == Qt::Checked) {
            ++checkedCount;
            onlyChecked = item;
        }
    }
    m_profileList->blockSignals(true);
    for (int i = 0; i < m_profileList->count(); ++i) {
        auto* item = m_profileList->item(i);
        const auto flags = item->flags();
        if (checkedCount == 1 && item == onlyChecked) {
            item->setFlags(flags & ~Qt::ItemIsEnabled);
            item->setToolTip(tr("At least one profile must be enabled."));
        } else {
            item->setFlags(flags | Qt::ItemIsEnabled);
            item->setToolTip({});
        }
    }
    m_profileList->blockSignals(false);
}

// ---------------------------------------------------------------------------
// Profile global / per-atlas mode
// ---------------------------------------------------------------------------

void AtlasesManagementWorkspace::setProfilesGlobal(bool global) {
    if (!m_profilesGlobalCheckBox || !m_profileList) return;

    const bool wasGlobal = m_profilesGlobalCheckBox->isChecked();

    if (!global && wasGlobal) {
        // Save current list state as the global profiles before switching
        m_savedGlobalProfiles.clear();
        for (int i = 0; i < m_profileList->count(); ++i) {
            if (m_profileList->item(i)->checkState() == Qt::Checked)
                m_savedGlobalProfiles << m_profileList->item(i)->data(Qt::UserRole).toString();
        }
    }

    m_syncing = true;
    m_profilesGlobalCheckBox->setChecked(global);
    m_syncing = false;

    const int row = m_atlasList ? m_atlasList->currentRow() : -1;
    const bool valid = row >= 0 && row < m_atlases.size();
    const bool isNeutral = valid && m_atlases[row].isNeutral;

    // Only modify the list on actual mode transitions; leave it alone otherwise
    // to avoid wiping the checked state that setProfiles() just established.
    if (!global && wasGlobal) {
        // Switched to per-atlas: load this atlas's specific profiles.
        // For the neutral atlas (which always manages global profiles) — leave as-is.
        if (valid && !isNeutral)
            loadAtlasProfilesIntoList(row);
    } else if (global && !wasGlobal) {
        // Switched back to global: restore the previously saved global profiles.
        m_syncing = true;
        m_profileList->blockSignals(true);
        for (int i = 0; i < m_profileList->count(); ++i) {
            const QString name = m_profileList->item(i)->data(Qt::UserRole).toString();
            m_profileList->item(i)->setCheckState(
                m_savedGlobalProfiles.contains(name) ? Qt::Checked : Qt::Unchecked);
        }
        m_profileList->blockSignals(false);
        m_syncing = false;
        updateProfileListEnabledStates();
    }
    // Same mode as before: list is already correct, nothing to do.

    updateProfilesVisibility();
}

bool AtlasesManagementWorkspace::isProfilesGlobal() const {
    const int row = m_atlasList ? m_atlasList->currentRow() : -1;
    const bool isNeutral = (row >= 0 && row < m_atlases.size()) && m_atlases[row].isNeutral;
    return isNeutral || (!m_profilesGlobalCheckBox || m_profilesGlobalCheckBox->isChecked());
}

void AtlasesManagementWorkspace::loadAtlasProfilesIntoList(int atlasRow) {
    if (atlasRow < 0 || atlasRow >= m_atlases.size() || !m_profileList) return;
    const QStringList& atlasProfiles = m_atlases[atlasRow].exportConfig.profiles;
    m_syncing = true;
    m_profileList->blockSignals(true);
    for (int i = 0; i < m_profileList->count(); ++i) {
        const QString name = m_profileList->item(i)->data(Qt::UserRole).toString();
        m_profileList->item(i)->setCheckState(
            atlasProfiles.contains(name) ? Qt::Checked : Qt::Unchecked);
    }
    m_profileList->blockSignals(false);
    m_syncing = false;
    updateProfileListEnabledStates();
}

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

void AtlasesManagementWorkspace::setResolutionOptions(const QStringList& options, const QString& current) {
    if (!m_resolutionCombo) return;
    m_syncing = true;
    m_resolutionCombo->blockSignals(true);
    m_resolutionCombo->clear();
    m_resolutionCombo->addItems(options);
    const int idx = m_resolutionCombo->findText(current);
    if (idx >= 0) m_resolutionCombo->setCurrentIndex(idx);
    m_resolutionCombo->blockSignals(false);
    m_syncing = false;
}

void AtlasesManagementWorkspace::setCurrentResolution(const QString& res) {
    if (!m_resolutionCombo) return;
    const int idx = m_resolutionCombo->findText(res);
    if (idx < 0 || idx == m_resolutionCombo->currentIndex()) return;
    m_syncing = true;
    m_resolutionCombo->setCurrentIndex(idx);
    m_syncing = false;
}

QString AtlasesManagementWorkspace::currentResolution() const {
    return m_resolutionCombo ? m_resolutionCombo->currentText() : QString{};
}

// ---------------------------------------------------------------------------
// View mode
// ---------------------------------------------------------------------------

void AtlasesManagementWorkspace::onViewModeToggled(int buttonId) {
    const ViewMode newMode = (buttonId == 1) ? ViewMode::Layout : ViewMode::Navigation;
    if (newMode == m_viewMode) return;

    // Clear the outgoing mode's filter before switching.
    if (m_viewMode == ViewMode::Layout)
        emit layoutFilterChanged(QString());  // un-dim all sprites in canvas
    // Navigation mode: NavigatorPanel clears its own filter internally; nothing to do here.

    m_viewMode = newMode;
    const bool isLayout = (m_viewMode == ViewMode::Layout);
    m_centerStack->setCurrentIndex(isLayout ? 1 : 0);
    m_zoomRow->setVisible(isLayout);
    if (m_spriteFilterLabel) m_spriteFilterLabel->setVisible(false);

    // Show the right-panel filter only in Layout mode; NavigatorPanel has its own in Navigation mode.
    if (m_spriteFilterEdit) {
        m_spriteFilterEdit->setVisible(isLayout);
        m_spriteFilterEdit->blockSignals(true);
        m_spriteFilterEdit->clear();
        m_spriteFilterEdit->blockSignals(false);
    }

    emit viewModeChanged(m_viewMode);
}

void AtlasesManagementWorkspace::setCanvasWidget(QWidget* widget) {
    clearCanvasWidget();
    if (!widget || !m_canvasPane) return;
    widget->setParent(m_canvasPane);
    m_canvasPane->layout()->addWidget(widget);
    widget->show();
}

void AtlasesManagementWorkspace::clearCanvasWidget() {
    if (!m_canvasPane) return;
    QLayout* layout = m_canvasPane->layout();
    if (!layout) return;
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
        }
        delete item;
    }
}

void AtlasesManagementWorkspace::setZoom(double percent) {
    if (!m_zoomSpin) return;
    if (qFuzzyCompare(m_zoomSpin->value(), percent)) return;
    m_syncing = true;
    m_zoomSpin->setValue(percent);
    m_syncing = false;
}

double AtlasesManagementWorkspace::currentZoom() const {
    return m_zoomSpin ? m_zoomSpin->value() : 100.0;
}

void AtlasesManagementWorkspace::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);

    // Apply initial sizes exactly once, on the first show.
    //
    // The problem: the splitter is laid out while the workspace is hidden in a
    // QStackedWidget (width = 0). Qt then redistributes proportionally from zero
    // when the page becomes visible, but stored section widths can exceed the
    // panels' maximumWidth constraints. The widget is clipped to its maximum, but
    // the handle is positioned at the (larger) section boundary — leaving a visible
    // gap between the left panel and the centre panel.
    //
    // The sum-based check is not reliable here because stored section sizes still
    // sum to the total width even when individual sections violate maximumWidth.
    // Instead, just set sensible defaults once; after that, user-adjusted positions
    // are preserved by the flag guard.
    if (m_splitterInitialized) return;
    m_splitterInitialized = true;

    // Defer until after the event loop finishes the layout pass so the splitter
    // has its real width when setSizes is called.
    QTimer::singleShot(0, this, [this]() {
        if (!m_splitter || m_splitter->width() <= 0) return;
        const int handles = m_splitter->handleWidth() * (m_splitter->count() - 1);
        const int leftW   = 190;
        const int rightW  = 230;
        const int centerW = m_splitter->width() - leftW - rightW - handles;
        if (centerW > 0)
            m_splitter->setSizes({leftW, centerW, rightW});
    });
}

// ---------------------------------------------------------------------------
// IWorkspace
// ---------------------------------------------------------------------------

void AtlasesManagementWorkspace::enter()
{
    // Data is already populated via setAtlases/setSources/setSession before enter()
    // is called. Just ensure the view mode is coherent (Navigation by default).
    if (m_viewMode == ViewMode::Layout && m_centerStack)
        m_centerStack->setCurrentIndex(1);
    else if (m_centerStack)
        m_centerStack->setCurrentIndex(0);
}

void AtlasesManagementWorkspace::leave()
{
    // Nothing to persist — selectedAtlasIndex() is already tracked by the list widget.
    // Clear the canvas widget if Layout mode was active so the canvas can be
    // re-parented back into the atlas dock without leaving stale references.
    if (m_viewMode == ViewMode::Layout)
        clearCanvasWidget();
}
