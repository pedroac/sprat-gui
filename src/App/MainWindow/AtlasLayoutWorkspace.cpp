#include "AtlasLayoutWorkspace.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

AtlasLayoutWorkspace::AtlasLayoutWorkspace(QWidget* parent)
    : QWidget(parent) {
    setupUi();
}

void AtlasLayoutWorkspace::setupUi() {
    auto* topLayout = new QVBoxLayout(this);
    topLayout->setContentsMargins(0, 0, 0, 0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    topLayout->addWidget(splitter);

    // Left: canvas pane
    m_canvasPane = new QWidget(splitter);
    auto* paneLayout = new QVBoxLayout(m_canvasPane);
    paneLayout->setContentsMargins(0, 0, 0, 0);
    splitter->addWidget(m_canvasPane);

    // Right: side panel
    auto* panel = new QWidget(splitter);
    panel->setMinimumWidth(180);
    panel->setMaximumWidth(280);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 8, 8, 8);
    splitter->addWidget(panel);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);

    // View group (populated by setViewControls)
    m_viewGroup = new QGroupBox(tr("View"), panel);
    auto* viewLayout = new QVBoxLayout(m_viewGroup);
    viewLayout->setContentsMargins(8, 8, 8, 8);
    panelLayout->addWidget(m_viewGroup);

    // Profiles group
    auto* profilesGroup = new QGroupBox(tr("Profiles"), panel);
    auto* profilesLayout = new QVBoxLayout(profilesGroup);
    profilesLayout->setContentsMargins(8, 8, 8, 8);

    m_profileList = new QListWidget(profilesGroup);
    profilesLayout->addWidget(m_profileList);

    auto* manageBtn = new QPushButton(tr("Manage..."), profilesGroup);
    profilesLayout->addWidget(manageBtn);
    connect(manageBtn, &QPushButton::clicked, this, &AtlasLayoutWorkspace::manageProfilesRequested);

    panelLayout->addWidget(profilesGroup);

    // Atlas group (hidden when count <= 1)
    m_atlasGroup = new QGroupBox(tr("Atlas"), panel);
    auto* atlasLayout = new QVBoxLayout(m_atlasGroup);
    atlasLayout->setContentsMargins(8, 8, 8, 8);

    m_atlasList = new QListWidget(m_atlasGroup);
    atlasLayout->addWidget(m_atlasList);
    m_atlasGroup->setVisible(false);
    panelLayout->addWidget(m_atlasGroup);

    panelLayout->addStretch();

    // Profile list signals
    connect(m_profileList, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (m_syncing) return;
        if (item->checkState() == Qt::Unchecked) {
            // If the unchecked item was the current selection, move selection
            if (item == m_profileList->currentItem()) {
                ensureValidSelection();
            }
        }
        m_syncing = true;
        emit profileEnablementChanged(enabledProfiles());
        m_syncing = false;
    });

    connect(m_profileList, &QListWidget::currentItemChanged,
            this, [this](QListWidgetItem* current, QListWidgetItem* /*previous*/) {
        if (m_syncing) return;
        if (!current) return;
        if (current->checkState() == Qt::Unchecked) {
            ensureValidSelection();
            return;
        }
        emit selectedProfileChanged(current->data(Qt::UserRole).toString());
    });

    // Atlas list signal
    connect(m_atlasList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_syncing) return;
        if (row >= 0) emit selectedAtlasChanged(row);
    });
}

void AtlasLayoutWorkspace::setCanvasWidget(QWidget* widget) {
    clearCanvasWidget();
    if (!widget || !m_canvasPane) return;
    widget->setParent(m_canvasPane);
    m_canvasPane->layout()->addWidget(widget);
    widget->show();
}

void AtlasLayoutWorkspace::clearCanvasWidget() {
    if (!m_canvasPane) return;
    QLayout* layout = m_canvasPane->layout();
    if (!layout) return;
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->setParent(nullptr);
        delete item;
    }
}

void AtlasLayoutWorkspace::setViewControls(QComboBox* resolutionCombo, QDoubleSpinBox* zoomSpin) {
    if (!m_viewGroup) return;
    QLayout* old = m_viewGroup->layout();
    if (old) {
        while (QLayoutItem* it = old->takeAt(0)) delete it;
    }
    auto* vl = qobject_cast<QVBoxLayout*>(m_viewGroup->layout());
    if (!vl) {
        vl = new QVBoxLayout(m_viewGroup);
        vl->setContentsMargins(8, 8, 8, 8);
    }

    if (resolutionCombo) {
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(tr("Resolution:"), m_viewGroup));
        resolutionCombo->setParent(m_viewGroup);
        row->addWidget(resolutionCombo);
        vl->addLayout(row);
    }

    if (zoomSpin) {
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(tr("Zoom:"), m_viewGroup));
        zoomSpin->setParent(m_viewGroup);
        row->addWidget(zoomSpin);
        vl->addLayout(row);
    }
}

void AtlasLayoutWorkspace::setProfiles(const QStringList& names, const QStringList& labels,
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
        const bool enabled = enabledProfiles.isEmpty() || enabledProfiles.contains(name);
        item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
        m_profileList->addItem(item);
    }

    // Set current selection to currentProfile if it's enabled, otherwise first enabled
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
    m_syncing = false;
}

void AtlasLayoutWorkspace::setSelectedProfile(const QString& name) {
    if (!m_profileList) return;
    m_syncing = true;
    for (int i = 0; i < m_profileList->count(); ++i) {
        QListWidgetItem* item = m_profileList->item(i);
        if (item->data(Qt::UserRole).toString() == name) {
            m_profileList->setCurrentItem(item);
            break;
        }
    }
    m_syncing = false;
}

QString AtlasLayoutWorkspace::selectedProfile() const {
    if (!m_profileList) return {};
    QListWidgetItem* item = m_profileList->currentItem();
    if (!item) return {};
    return item->data(Qt::UserRole).toString();
}

QStringList AtlasLayoutWorkspace::enabledProfiles() const {
    QStringList result;
    if (!m_profileList) return result;
    for (int i = 0; i < m_profileList->count(); ++i) {
        QListWidgetItem* item = m_profileList->item(i);
        if (item->checkState() == Qt::Checked)
            result << item->data(Qt::UserRole).toString();
    }
    return result;
}

void AtlasLayoutWorkspace::setAtlasCount(int count) {
    if (!m_atlasGroup || !m_atlasList) return;
    m_syncing = true;
    m_atlasList->blockSignals(true);
    m_atlasList->clear();
    for (int i = 0; i < count; ++i)
        m_atlasList->addItem(tr("Atlas %1").arg(i + 1));
    if (count > 0)
        m_atlasList->setCurrentRow(0);
    m_atlasList->blockSignals(false);
    m_syncing = false;
    m_atlasGroup->setVisible(count > 1);
}

int AtlasLayoutWorkspace::selectedAtlasIndex() const {
    if (!m_atlasList) return 0;
    return qMax(0, m_atlasList->currentRow());
}

void AtlasLayoutWorkspace::ensureValidSelection() {
    if (!m_profileList || m_syncing) return;
    m_syncing = true;
    // Find first checked item
    for (int i = 0; i < m_profileList->count(); ++i) {
        QListWidgetItem* item = m_profileList->item(i);
        if (item->checkState() == Qt::Checked) {
            m_profileList->setCurrentItem(item);
            m_syncing = false;
            emit selectedProfileChanged(item->data(Qt::UserRole).toString());
            return;
        }
    }
    // No checked items
    m_profileList->setCurrentItem(nullptr);
    m_syncing = false;
}
