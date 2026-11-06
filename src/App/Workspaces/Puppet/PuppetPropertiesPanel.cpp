#include "PuppetPropertiesPanel.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

/// Largest distance from the puppet's origin a position spin box accepts.
constexpr int MAX_POSITION = 99999;
constexpr int THUMBNAIL_SIZE = 128;

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
        form->addRow(tr("Name:"), buildNameRow(m_puppetName, m_puppetNameProblem));
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
        form->addRow(tr("Name:"), buildNameRow(m_partName, m_partNameProblem));
        m_partName->setObjectName(QStringLiteral("partNameEdit"));

        m_positionRow = new QWidget(page);
        auto* positionLayout = new QHBoxLayout(m_positionRow);
        positionLayout->setContentsMargins(0, 0, 0, 0);
        auto makeCoordinate = [this](const QString& prefix) {
            auto* spin = new QSpinBox(m_positionRow);
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
        positionLayout->addWidget(m_positionX);
        positionLayout->addWidget(m_positionY);
        m_positionRow->setToolTip(tr("Where the part's pivot goes, from the puppet's origin (the large cross). "
                                     "Y grows downwards."));
        form->addRow(tr("Position:"), m_positionRow);
        m_partForm = form;
        pageLayout->addLayout(form);

        // Under the form, not in a row of it: a wrapped label in a form row gets clipped.
        m_attachment = new QLabel(page);
        m_attachment->setWordWrap(true);
        pageLayout->addWidget(m_attachment);

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
        form->addRow(tr("Name:"), buildNameRow(m_variantName, m_variantNameProblem));
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

        m_restLabel = new QLabel(tr("★ Rest variant: the part shows it unless a pose picks another."), page);
        m_restLabel->setWordWrap(true);
        pageLayout->addWidget(m_restLabel);
        m_makeRestButton = new QPushButton(tr("Make Rest Variant"), page);
        m_makeRestButton->setToolTip(tr("Move this variant first, so the part shows it at rest"));
        connect(m_makeRestButton, &QPushButton::clicked, this, &PuppetPropertiesPanel::makeRestVariantRequested);
        pageLayout->addWidget(m_makeRestButton, 0, Qt::AlignLeft);
        pageLayout->addStretch();
        m_pages->addWidget(page);
    }

    showNothing();
}

QWidget* PuppetPropertiesPanel::buildNameRow(QLineEdit*& edit, QLabel*& problem) {
    auto* row = new QWidget(this);
    auto* rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(2);
    edit = new QLineEdit(row);
    problem = makeWarningLabel(row);
    rowLayout->addWidget(edit);
    rowLayout->addWidget(problem);
    QLineEdit* field = edit;
    QLabel* label = problem;
    connect(field, &QLineEdit::textEdited, this, [this, field, label]() { checkName(field, label); });
    connect(field, &QLineEdit::editingFinished, this, [this, field, label]() { commitName(field, label); });
    return row;
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

void PuppetPropertiesPanel::showPart(const QString& name, std::optional<QPoint> position, const QString& attachment,
                                     const QString& problem, NameValidator validator) {
    m_validator = std::move(validator);
    m_shownName = name;
    m_partName->setText(name);
    m_partNameProblem->hide();
    {
        const QSignalBlocker blockX(m_positionX);
        const QSignalBlocker blockY(m_positionY);
        m_positionX->setValue(position ? position->x() : 0);
        m_positionY->setValue(position ? position->y() : 0);
    }
    // An attached part is placed by its attachment; a part with neither shows the problem.
    m_partForm->setRowVisible(m_positionRow, position.has_value());
    m_attachment->setText(attachment);
    m_attachment->setVisible(!attachment.isEmpty());
    m_partProblem->setText(problem);
    m_partProblem->setVisible(!problem.isEmpty());
    m_pages->setCurrentIndex(2);
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
    m_makeRestButton->setVisible(!isRest);
    m_pages->setCurrentIndex(3);
}
