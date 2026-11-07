#include "PuppetPropertiesPanel.h"
#include "PuppetModels.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

/// Largest distance from the puppet's origin a position spin box accepts.
constexpr int MAX_POSITION = 99999;
/// Largest leeway the spin box accepts.
constexpr int MAX_LEEWAY = 9999;
constexpr int THUMBNAIL_SIZE = 128;

/// Most height of the part's marker list; it scrolls beyond that.
constexpr int MARKER_LIST_HEIGHT = 110;
const char* WARNING_STYLE = "color: #d9534f;";

QLabel* makeWarningLabel(QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setStyleSheet(QLatin1String(WARNING_STYLE));
    label->setWordWrap(true);
    label->hide();
    return label;
}

QLabel* makeTitle(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

}  // namespace

PuppetPropertiesPanel::PuppetPropertiesPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    m_pages = new QStackedWidget(this);
    layout->addWidget(m_pages);

    // Nothing selected
    {
        auto* page = new QWidget(m_pages);
        auto* pageLayout = new QVBoxLayout(page);
        auto* hint = new QLabel(tr("Select a puppet, part or variant to see its properties."), page);
        hint->setWordWrap(true);
        hint->setAlignment(Qt::AlignCenter);
        pageLayout->addWidget(hint);
        pageLayout->addStretch();
        m_pages->addWidget(page);
    }

    // Puppet
    {
        auto* page = new QWidget(m_pages);
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->addWidget(makeTitle(tr("Puppet"), page));
        auto* form = new QFormLayout;
        addNameRows(form, page, m_puppetName, m_puppetNameProblem);
        m_puppetName->setObjectName(QStringLiteral("puppetNameEdit"));
        pageLayout->addLayout(form);
        m_puppetInfo = new QLabel(page);
        pageLayout->addWidget(m_puppetInfo);
        pageLayout->addStretch();
        m_pages->addWidget(page);
    }

    // Part
    {
        auto* page = new QWidget(m_pages);
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->addWidget(makeTitle(tr("Part"), page));
        auto* form = new QFormLayout;
        addNameRows(form, page, m_partName, m_partNameProblem);
        m_shownVariant = new QComboBox(page);
        m_shownVariant->setObjectName(QStringLiteral("partShownVariant"));
        m_shownVariant->setToolTip(tr("The variant the part shows in the selected pose. At rest, parts "
                                      "show their first (★) variant"));
        form->addRow(tr("Shows:"), m_shownVariant);
        connect(m_shownVariant, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (index >= 0) emit shownVariantChanged(m_shownVariant->itemData(index).toInt());
        });
        m_partName->setObjectName(QStringLiteral("partNameEdit"));

        // Rows hold widgets or plain layouts, never container widgets, so every label lines
        // up with its field whatever the style.
        m_positionRow = new QHBoxLayout;
        auto makeCoordinate = [this, page](const QString& prefix) {
            auto* spin = new QSpinBox(page);
            spin->setToolTip(tr("Where the part's pivot goes, from the puppet's origin (the large cross). "
                                "Y grows downwards."));
            spin->setRange(-MAX_POSITION, MAX_POSITION);
            spin->setPrefix(prefix);
            spin->setSuffix(tr(" px"));
            // Typing commits on Enter or leaving the field, not at every digit.
            spin->setKeyboardTracking(false);
            connect(spin, &QSpinBox::valueChanged, this, [this]() {
                emit positionChanged(QPoint(m_positionX->value(), m_positionY->value()));
            });
            return spin;
        };
        m_positionX = makeCoordinate(tr("X: "));
        m_positionX->setObjectName(QStringLiteral("partPositionX"));
        m_positionY = makeCoordinate(tr("Y: "));
        m_positionY->setObjectName(QStringLiteral("partPositionY"));
        m_positionRow->addWidget(m_positionX);
        m_positionRow->addWidget(m_positionY);
        m_partForm = form;
        buildPlacement(page, form);
        pageLayout->addLayout(form);

        m_partProblem = makeWarningLabel(page);
        pageLayout->addWidget(m_partProblem);
        pageLayout->addStretch();
        m_pages->addWidget(page);
    }

    // Variant
    {
        auto* page = new QWidget(m_pages);
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->addWidget(makeTitle(tr("Variant"), page));
        auto* form = new QFormLayout;
        addNameRows(form, page, m_variantName, m_variantNameProblem);
        m_variantName->setObjectName(QStringLiteral("variantNameEdit"));
        m_variantName->setToolTip(tr("Leave empty to use the default name, which follows the sprite or animation"));
        m_variantSource = new QLabel(page);
        m_variantSource->setWordWrap(true);
        m_variantSource->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(tr("Shows:"), m_variantSource);
        pageLayout->addLayout(form);

        m_variantThumbnail = new QLabel(page);
        m_variantThumbnail->setAlignment(Qt::AlignCenter);
        m_variantThumbnail->setMinimumHeight(THUMBNAIL_SIZE);
        pageLayout->addWidget(m_variantThumbnail);

        m_restLabel = new QLabel(tr("★ Rest variant: the one the rest pose shows. To change it, select rest "
                                    "below the canvas and pick another in the part's Shows."), page);
        m_restLabel->setWordWrap(true);
        pageLayout->addWidget(m_restLabel);
        pageLayout->addStretch();
        m_pages->addWidget(page);
    }

    showNothing();
}

void PuppetPropertiesPanel::addNameRows(QFormLayout* form, QWidget* page, QLineEdit*& edit, QLabel*& problem) {
    edit = new QLineEdit(page);
    problem = makeWarningLabel(page);
    form->addRow(tr("Name:"), edit);
    form->addRow(static_cast<QWidget*>(nullptr), problem);
    QLineEdit* field = edit;
    QLabel* label = problem;
    connect(field, &QLineEdit::textEdited, this, [this, field, label]() { checkName(field, label); });
    connect(field, &QLineEdit::editingFinished, this, [this, field, label]() { commitName(field, label); });
}

void PuppetPropertiesPanel::checkName(QLineEdit* edit, QLabel* problem) {
    const QString text = m_validator ? m_validator(edit->text()) : QString();
    problem->setText(text);
    problem->setVisible(!text.isEmpty());
}

void PuppetPropertiesPanel::commitName(QLineEdit* edit, QLabel* problem) {
    const QString name = edit->text();
    if (name == m_shownName) {
        problem->hide();
        return;
    }
    if (m_validator && !m_validator(name).isEmpty()) {
        // Put the name back; the problem stays shown so it's clear why.
        const QSignalBlocker blocker(edit);
        edit->setText(m_shownName);
        return;
    }
    problem->hide();
    m_shownName = name;
    emit renameRequested(name);
}

void PuppetPropertiesPanel::showNothing() {
    m_validator = {};
    m_pages->setCurrentIndex(0);
}

void PuppetPropertiesPanel::showPuppet(const QString& name, int partCount, NameValidator validator) {
    m_validator = std::move(validator);
    m_shownName = name;
    m_puppetName->setText(name);
    m_puppetNameProblem->hide();
    m_puppetInfo->setText(tr("%n part(s)", nullptr, partCount));
    m_pages->setCurrentIndex(1);
}

void PuppetPropertiesPanel::showPart(const QString& name, const Placement& placement, const QString& problem,
                                     NameValidator validator) {
    m_validator = std::move(validator);
    m_shownName = name;
    m_placement = placement;
    m_partName->setText(name);
    m_partNameProblem->hide();

    const QSignalBlocker blockByPosition(m_byPosition);
    const QSignalBlocker blockAttached(m_attached);
    const QSignalBlocker blockX(m_positionX);
    const QSignalBlocker blockY(m_positionY);
    const QSignalBlocker blockParent(m_parentCombo);
    const QSignalBlocker blockParentMarker(m_parentMarkerCombo);
    const QSignalBlocker blockChildMarker(m_childMarkerCombo);
    const QSignalBlocker blockLeeway(m_leeway);
    const QSignalBlocker blockLimit(m_limitAngle);
    const QSignalBlocker blockMin(m_angleMin);
    const QSignalBlocker blockMax(m_angleMax);
    const QSignalBlocker blockAngle(m_angle);

    const bool attached = !placement.position && !placement.parent.isEmpty();
    m_byPosition->setChecked(!attached);
    m_attached->setChecked(attached);
    // Nothing to hang from: only another part, not hanging from this one, can hold it.
    m_attached->setEnabled(attached || !placement.parents.isEmpty());
    m_attached->setToolTip(m_attached->isEnabled() ? QString()
                                                   : tr("Add another part first: this part hangs from one of its markers"));
    m_positionX->setValue(placement.position ? placement.position->x() : 0);
    m_positionY->setValue(placement.position ? placement.position->y() : 0);

    m_parentCombo->clear();
    for (const QString& parent : placement.parents) m_parentCombo->addItem(parent, parent);
    if (attached && m_parentCombo->findData(placement.parent) < 0) {
        // A hand-written file can name a part that's missing, or one that hangs from this one.
        m_parentCombo->addItem(QStringLiteral("⚠ %1").arg(placement.parent), placement.parent);
    }
    // A positioned part offers its first possible parent, ready for Attached.
    const QString parent = attached ? placement.parent : placement.parents.value(0);
    m_parentCombo->setCurrentIndex(qMax(0, m_parentCombo->findData(parent)));
    fillMarkers(m_parentMarkerCombo, placement.parentMarkers.value(parent),
                attached ? placement.parentMarker : QString());
    fillMarkers(m_childMarkerCombo, placement.childMarkers,
                attached ? placement.childMarker : QStringLiteral("pivot"));
    m_leeway->setValue(placement.leeway);
    const bool limited = placement.angleMin && placement.angleMax;
    m_limitAngle->setChecked(limited);
    m_angleMin->setValue(limited ? *placement.angleMin : -45);
    m_angleMax->setValue(limited ? *placement.angleMax : 45);
    updatePlacementRows();
    m_angle->setValue(placement.angle);
    m_angle->setEnabled(placement.posing);
    m_angle->setToolTip(placement.posing
        ? tr("Turns the part in the selected pose, clockwise: from its parent when attached, around its "
             "pivot when placed by position")
        : tr("Select a pose below the canvas to turn parts"));
    {
        const QSignalBlocker blockVariant(m_shownVariant);
        m_shownVariant->clear();
        // By name, as in the tree; each item holds the variant's index.
        for (const int v : placement.variantOrder)
            m_shownVariant->addItem(v == 0 ? QStringLiteral("★ %1").arg(placement.variants[v]) : placement.variants[v], v);
        m_shownVariant->setCurrentIndex(m_shownVariant->findData(placement.variant));
        m_shownVariant->setEnabled(placement.posing);
        m_partForm->setRowVisible(m_shownVariant, placement.variants.size() > 1);
    }
    {
        const QSignalBlocker blockMarkers(m_markers);
        m_markers->clear();
        for (const QString& marker : placement.markers) {
            auto* item = new QListWidgetItem(marker, m_markers);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(placement.hiddenMarkers.contains(marker) ? Qt::Unchecked : Qt::Checked);
        }
        m_partForm->setRowVisible(m_markers, !placement.markers.isEmpty());
        const int rows = m_markers->count() > 0 ? m_markers->sizeHintForRow(0) * m_markers->count() : 0;
        m_markers->setFixedHeight(qMin(MARKER_LIST_HEIGHT, rows + 2 * m_markers->frameWidth()));
    }

    m_partProblem->setText(problem);
    m_partProblem->setVisible(!problem.isEmpty());
    m_pages->setCurrentIndex(2);
}

void PuppetPropertiesPanel::buildPlacement(QWidget* page, QFormLayout* form) {
    m_byPosition = new QRadioButton(tr("By position"), page);
    m_byPosition->setObjectName(QStringLiteral("partByPosition"));
    m_byPosition->setToolTip(tr("Place the part's pivot at a position from the puppet's origin"));
    m_attached = new QRadioButton(tr("Attached"), page);
    m_attached->setObjectName(QStringLiteral("partAttached"));
    auto* group = new QButtonGroup(page);
    group->addButton(m_byPosition);
    group->addButton(m_attached);
    form->addRow(tr("Placed:"), m_byPosition);
    form->addRow(static_cast<QWidget*>(nullptr), m_attached);
    form->addRow(tr("Position:"), m_positionRow);

    m_parentCombo = new QComboBox(page);
    m_parentCombo->setObjectName(QStringLiteral("partParentCombo"));
    m_parentCombo->setToolTip(tr("The part this one hangs from. It moves and turns with it"));
    form->addRow(tr("Hangs from:"), m_parentCombo);
    m_parentMarkerCombo = new QComboBox(page);
    m_parentMarkerCombo->setObjectName(QStringLiteral("partParentMarkerCombo"));
    m_parentMarkerCombo->setToolTip(tr("The marker of that part where this part's joint goes. "
                                       "Markers are added in the Sprites workspace"));
    form->addRow(tr("At its marker:"), m_parentMarkerCombo);
    m_childMarkerCombo = new QComboBox(page);
    m_childMarkerCombo->setObjectName(QStringLiteral("partChildMarkerCombo"));
    m_childMarkerCombo->setToolTip(tr("The point of this part that goes on that marker: its pivot, "
                                      "or one of its point markers"));
    form->addRow(tr("By its marker:"), m_childMarkerCombo);

    m_leeway = new QSpinBox(page);
    m_leeway->setObjectName(QStringLiteral("partLeeway"));
    m_leeway->setRange(0, MAX_LEEWAY);
    m_leeway->setSuffix(tr(" px"));
    m_leeway->setKeyboardTracking(false);
    m_leeway->setToolTip(tr("How far the joint may drift beyond the marker when posing"));
    form->addRow(tr("Leeway:"), m_leeway);

    m_angleRow = new QHBoxLayout;
    QHBoxLayout* angleLayout = m_angleRow;
    m_limitAngle = new QCheckBox(page);
    m_limitAngle->setObjectName(QStringLiteral("partLimitAngle"));
    m_limitAngle->setToolTip(tr("Limit how far the part turns from its parent; unticked, it turns freely"));
    auto makeAngle = [page]() {
        auto* spin = new QSpinBox(page);
        spin->setRange(-PUPPET_MAX_ANGLE, PUPPET_MAX_ANGLE);
        spin->setSuffix(QStringLiteral("°"));
        spin->setKeyboardTracking(false);
        return spin;
    };
    m_angleMin = makeAngle();
    m_angleMin->setObjectName(QStringLiteral("partAngleMin"));
    m_angleMax = makeAngle();
    m_angleMax->setObjectName(QStringLiteral("partAngleMax"));
    angleLayout->addWidget(m_limitAngle);
    angleLayout->addWidget(m_angleMin);
    angleLayout->addWidget(new QLabel(tr("to"), page));
    angleLayout->addWidget(m_angleMax);
    angleLayout->addStretch();
    form->addRow(tr("Turns:"), m_angleRow);

    m_angle = new QSpinBox(page);
    m_angle->setObjectName(QStringLiteral("partAngle"));
    m_angle->setSuffix(QStringLiteral("°"));
    m_angle->setKeyboardTracking(false);
    form->addRow(tr("Angle:"), m_angle);
    connect(m_angle, &QSpinBox::valueChanged, this, &PuppetPropertiesPanel::angleChanged);

    m_markers = new QListWidget(page);
    m_markers->setObjectName(QStringLiteral("partMarkers"));
    m_markers->setToolTip(tr("The markers shown on the canvas and offered for joints. Untick the ones "
                             "that are only for the game (hit boxes, spawn points...)"));
    // As tall as its markers: a list that can grow would make the form spread its spare height
    // between every row.
    m_markers->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    form->addRow(tr("Markers:"), m_markers);
    connect(m_markers, &QListWidget::itemChanged, this, [this]() {
        QStringList hidden;
        for (int i = 0; i < m_markers->count(); ++i) {
            if (m_markers->item(i)->checkState() != Qt::Checked) hidden.append(m_markers->item(i)->text());
        }
        emit hiddenMarkersChanged(hidden);
    });

    connect(m_byPosition, &QRadioButton::toggled, this, [this](bool checked) {
        updatePlacementRows();
        if (checked) emit detachRequested();
    });
    connect(m_attached, &QRadioButton::toggled, this, [this](bool checked) {
        updatePlacementRows();
        if (checked) emitAttach();
    });
    connect(m_parentCombo, &QComboBox::currentIndexChanged, this, [this]() {
        // Keep the marker when the new parent has one of that name.
        const QString marker = m_parentMarkerCombo->currentData().toString();
        const QString parent = m_parentCombo->currentData().toString();
        const QSignalBlocker blocker(m_parentMarkerCombo);
        fillMarkers(m_parentMarkerCombo, m_placement.parentMarkers.value(parent), QString());
        const int same = m_parentMarkerCombo->findData(marker);
        if (same >= 0) m_parentMarkerCombo->setCurrentIndex(same);
        emitAttach();
    });
    connect(m_parentMarkerCombo, &QComboBox::currentIndexChanged, this, &PuppetPropertiesPanel::emitAttach);
    connect(m_childMarkerCombo, &QComboBox::currentIndexChanged, this, &PuppetPropertiesPanel::emitAttach);
    connect(m_leeway, &QSpinBox::valueChanged, this, &PuppetPropertiesPanel::emitLimits);
    connect(m_limitAngle, &QCheckBox::toggled, this, [this]() {
        updatePlacementRows();
        emitLimits();
    });
    connect(m_angleMin, &QSpinBox::valueChanged, this, &PuppetPropertiesPanel::emitLimits);
    connect(m_angleMax, &QSpinBox::valueChanged, this, &PuppetPropertiesPanel::emitLimits);
}

void PuppetPropertiesPanel::fillMarkers(QComboBox* combo, const QVector<MarkerChoice>& markers, const QString& current) {
    combo->clear();
    for (const MarkerChoice& marker : markers) {
        if (marker.missingIn.isEmpty()) {
            combo->addItem(marker.name, marker.name);
        } else {
            combo->addItem(QStringLiteral("⚠ %1").arg(marker.name), marker.name);
            combo->setItemData(combo->count() - 1, tr("Missing in: %1").arg(marker.missingIn.join(QStringLiteral(", "))),
                               Qt::ToolTipRole);
        }
    }
    if (!current.isEmpty() && combo->findData(current) < 0) {
        combo->addItem(QStringLiteral("⚠ %1").arg(current), current);
        combo->setItemData(combo->count() - 1, tr("No variant has this marker"), Qt::ToolTipRole);
    }
    combo->setCurrentIndex(qMax(0, combo->findData(current)));
}

void PuppetPropertiesPanel::emitAttach() {
    const QString parent = m_parentCombo->currentData().toString();
    const QString parentMarker = m_parentMarkerCombo->currentData().toString();
    const QString childMarker = m_childMarkerCombo->currentData().toString();
    if (parent.isEmpty() || parentMarker.isEmpty() || childMarker.isEmpty()) return;
    emit attachRequested(parent, parentMarker, childMarker);
}

void PuppetPropertiesPanel::emitLimits() {
    emit limitsChanged(m_leeway->value(), m_limitAngle->isChecked(), m_angleMin->value(), m_angleMax->value());
}

void PuppetPropertiesPanel::updatePlacementRows() {
    const bool attached = m_attached->isChecked();
    m_partForm->setRowVisible(m_positionRow, !attached);
    m_partForm->setRowVisible(m_parentCombo, attached);
    m_partForm->setRowVisible(m_parentMarkerCombo, attached);
    m_partForm->setRowVisible(m_childMarkerCombo, attached);
    m_partForm->setRowVisible(m_leeway, attached);
    m_partForm->setRowVisible(m_angleRow, attached);
    m_angleMin->setEnabled(m_limitAngle->isChecked());
    m_angleMax->setEnabled(m_limitAngle->isChecked());
    updateAngleRange();
}

void PuppetPropertiesPanel::updateAngleRange() {
    const QSignalBlocker blocker(m_angle);
    const int angle = m_angle->value();
    const bool limited = m_attached->isChecked() && m_limitAngle->isChecked();
    if (limited) {
        m_angle->setRange(qMin(m_angleMin->value(), m_angleMax->value()),
                          qMax(m_angleMin->value(), m_angleMax->value()));
    } else {
        m_angle->setRange(-179, 180);
    }
    // A free turn goes round past 180° to -179°.
    m_angle->setWrapping(!limited);
    m_angle->setValue(angle);
}

void PuppetPropertiesPanel::setAngle(int angle) {
    const QSignalBlocker blocker(m_angle);
    m_angle->setValue(angle);
}

void PuppetPropertiesPanel::showVariant(const QString& explicitName, const QString& defaultName, const QString& source,
                                        const QPixmap& thumbnail, bool isRest, NameValidator validator) {
    m_validator = std::move(validator);
    m_shownName = explicitName;
    m_variantName->setText(explicitName);
    m_variantName->setPlaceholderText(defaultName);
    m_variantNameProblem->hide();
    m_variantSource->setText(source);
    if (thumbnail.isNull()) {
        m_variantThumbnail->setText(tr("(not in this atlas)"));
    } else {
        const QPixmap scaled = thumbnail.width() > THUMBNAIL_SIZE || thumbnail.height() > THUMBNAIL_SIZE
            ? thumbnail.scaled(THUMBNAIL_SIZE, THUMBNAIL_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            : thumbnail;
        m_variantThumbnail->setPixmap(scaled);
    }
    m_restLabel->setVisible(isRest);
    m_pages->setCurrentIndex(3);
}
