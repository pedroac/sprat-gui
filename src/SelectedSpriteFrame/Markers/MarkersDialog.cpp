#include "MarkersDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QMessageBox>

/**
 * @brief Constructs the MarkersDialog.
 */
MarkersDialog::MarkersDialog(SpritePtr sprite, QWidget* parent) 
    : QDialog(parent), m_sprite(sprite) 
{
    setWindowTitle("Markers Configuration");
    resize(450, 500);
    setupUi();
    refreshList();
}

/**
 * @brief Initializes the UI components.
 */
void MarkersDialog::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Add Section
    QHBoxLayout* addLayout = new QHBoxLayout();
    m_addTypeCombo = new QComboBox();
    m_addTypeCombo->addItems({"point", "circle", "rectangle", "polygon"});
    addLayout->addWidget(m_addTypeCombo);
    
    m_addNameEdit = new QLineEdit();
    m_addNameEdit->setPlaceholderText("New marker name");
    addLayout->addWidget(m_addNameEdit);
    
    QPushButton* addBtn = new QPushButton(QIcon::fromTheme("list-add"), "Add");
    connect(addBtn, &QPushButton::clicked, this, &MarkersDialog::onAddClicked);
    connect(m_addNameEdit, &QLineEdit::returnPressed, this, &MarkersDialog::onAddClicked);
    addLayout->addWidget(addBtn);
    mainLayout->addLayout(addLayout);

    // List
    m_listWidget = new QListWidget();
    connect(m_listWidget, &QListWidget::itemSelectionChanged, this, &MarkersDialog::onSelectionChanged);
    mainLayout->addWidget(m_listWidget);

    QPushButton* removeBtn = new QPushButton(QIcon::fromTheme("list-remove"), "Remove Selected");
    connect(removeBtn, &QPushButton::clicked, this, &MarkersDialog::onRemoveClicked);
    mainLayout->addWidget(removeBtn);

    // Editor
    m_editorGroup = new QGroupBox("Edit Marker");
    QVBoxLayout* editLayout = new QVBoxLayout(m_editorGroup);
    
    auto createRow = [&](const QString& label, QWidget* widget) -> QWidget* {
        QWidget* rowWidget = new QWidget();
        QHBoxLayout* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(new QLabel(label));
        rowLayout->addWidget(widget);
        rowLayout->addStretch();
        return rowWidget;
    };

    m_nameEdit = new QLineEdit();
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &MarkersDialog::onFieldChanged);
    editLayout->addWidget(createRow("Name:", m_nameEdit));

    m_typeCombo = new QComboBox();
    m_typeCombo->addItems({"point", "circle", "rectangle", "polygon"});
    // connect(m_typeCombo, &QComboBox::currentTextChanged, this, &MarkersDialog::onFieldChanged);
    // editLayout->addWidget(createRow("Type:", m_typeCombo));

    m_xyRow = new QWidget();
    QHBoxLayout* xyLayout = new QHBoxLayout(m_xyRow);
    xyLayout->setContentsMargins(0, 0, 0, 0);
    xyLayout->addWidget(new QLabel("X:"));
    m_xSpin = new QSpinBox(); m_xSpin->setRange(0, 9999);
    connect(m_xSpin, &QSpinBox::editingFinished, this, &MarkersDialog::onFieldChanged);
    xyLayout->addWidget(m_xSpin);
    xyLayout->addWidget(new QLabel("Y:"));
    m_ySpin = new QSpinBox(); m_ySpin->setRange(0, 9999);
    connect(m_ySpin, &QSpinBox::editingFinished, this, &MarkersDialog::onFieldChanged);
    xyLayout->addWidget(m_ySpin);

    m_radiusSpin = new QSpinBox(); m_radiusSpin->setRange(1, 9999);
    connect(m_radiusSpin, &QSpinBox::editingFinished, this, &MarkersDialog::onFieldChanged);
    
    m_radiusRow = new QWidget();
    QHBoxLayout* radiusLayout = new QHBoxLayout(m_radiusRow);
    radiusLayout->setContentsMargins(0, 0, 0, 0);
    radiusLayout->addWidget(new QLabel("Radius:"));
    radiusLayout->addWidget(m_radiusSpin);
    xyLayout->addWidget(m_radiusRow);

    m_rectRow = new QWidget();
    QHBoxLayout* rectLayout = new QHBoxLayout(m_rectRow);
    rectLayout->setContentsMargins(0, 0, 0, 0);
    rectLayout->addWidget(new QLabel("W:"));
    m_wSpin = new QSpinBox(); m_wSpin->setRange(1, 9999);
    connect(m_wSpin, &QSpinBox::editingFinished, this, &MarkersDialog::onFieldChanged);
    rectLayout->addWidget(m_wSpin);
    rectLayout->addWidget(new QLabel("H:"));
    m_hSpin = new QSpinBox(); m_hSpin->setRange(1, 9999);
    connect(m_hSpin, &QSpinBox::editingFinished, this, &MarkersDialog::onFieldChanged);
    rectLayout->addWidget(m_hSpin);
    
    xyLayout->addWidget(m_rectRow);
    xyLayout->addStretch();
    editLayout->addWidget(m_xyRow);

    m_clearPolyBtn = new QPushButton("Clear Polygon Vertices");
    connect(m_clearPolyBtn, &QPushButton::clicked, this, &MarkersDialog::onClearPolygon);
    m_polyRow = m_clearPolyBtn;
    editLayout->addWidget(m_polyRow);

    mainLayout->addWidget(m_editorGroup);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::accept);
    mainLayout->addWidget(buttonBox);

    updateEditorState();
}

/**
 * @brief Refreshes the list of markers from the sprite data.
 */
void MarkersDialog::refreshList() {
    m_updating = true;
    int currentRow = m_listWidget->currentRow();
    m_listWidget->clear();
    if (m_sprite) {
        for (const auto& p : m_sprite->points) {
            m_listWidget->addItem(QString("%1 (%2)").arg(p.name, p.kind));
        }
    }
    if (currentRow >= 0 && currentRow < m_listWidget->count()) {
        m_listWidget->setCurrentRow(currentRow);
    }
    m_updating = false;
    updateEditorState();
}

/**
 * @brief Adds a new marker to the sprite.
 */
void MarkersDialog::onAddClicked() {
    if (!m_sprite) {
        return;
    }
    QString name = m_addNameEdit->text().trimmed();
    if (name.isEmpty()) {
        name = QString("point%1").arg(m_sprite->points.size() + 1);
    }
    
    // Check duplicate
    for (const auto& p : m_sprite->points) {
        if (p.name == name) {
            QMessageBox::warning(this, "Error", "Marker name already exists.");
            return;
        }
    }

    NamedPoint p;
    p.name = name;
    p.kind = m_addTypeCombo->currentText();
    p.x = m_sprite->rect.width() / 2;
    p.y = m_sprite->rect.height() / 2;
    
    if (p.kind == "polygon") {
        // Default triangle
        p.polygonPoints = {
            QPoint(p.x, p.y - 20),
            QPoint(p.x - 20, p.y + 20),
            QPoint(p.x + 20, p.y + 20)
        };
        p.x = p.polygonPoints[0].x();
        p.y = p.polygonPoints[0].y();
    }

    m_sprite->points.append(p);
    m_addNameEdit->clear();
    refreshList();
    m_listWidget->setCurrentRow(m_sprite->points.size() - 1);
    emit markersChanged();
}

/**
 * @brief Removes the selected marker.
 */
void MarkersDialog::onRemoveClicked() {
    if (!m_sprite) {
        return;
    }
    int row = m_listWidget->currentRow();
    if (row >= 0 && row < m_sprite->points.size()) {
        m_sprite->points.removeAt(row);
        refreshList();
        emit markersChanged();
    }
}

void MarkersDialog::onSelectionChanged() {
    updateEditorState();
}

/**
 * @brief Updates the editor fields based on the selected marker.
 */
void MarkersDialog::updateEditorState() {
    if (m_updating || !m_sprite) {
        return;
    }
    int row = m_listWidget->currentRow();
    bool hasSel = (row >= 0 && row < m_sprite->points.size());
    m_editorGroup->setVisible(hasSel);

    if (hasSel) {
        m_updating = true;
        const auto& p = m_sprite->points[row];
        m_nameEdit->setText(p.name);
        m_typeCombo->setCurrentText(p.kind);
        m_xSpin->setValue(p.x);
        m_ySpin->setValue(p.y);
        m_radiusSpin->setValue(p.radius);
        m_wSpin->setValue(p.w);
        m_hSpin->setValue(p.h);

        bool isCircle = p.kind == "circle";
        bool isRect = p.kind == "rectangle";
        bool isPoly = p.kind == "polygon";

        m_radiusRow->setVisible(isCircle);
        m_rectRow->setVisible(isRect);
        m_polyRow->setVisible(isPoly);
        m_xyRow->setVisible(!isPoly);
        m_updating = false;
    }
}

/**
 * @brief Updates the marker data when an editor field changes.
 */
void MarkersDialog::onFieldChanged() {
    if (m_updating || !m_sprite) {
        return;
    }
    int row = m_listWidget->currentRow();
    if (row < 0 || row >= m_sprite->points.size()) {
        return;
    }

    auto& p = m_sprite->points[row];
    p.name = m_nameEdit->text();
    p.kind = m_typeCombo->currentText();
    p.x = m_xSpin->value();
    p.y = m_ySpin->value();
    p.radius = m_radiusSpin->value();
    p.w = m_wSpin->value();
    p.h = m_hSpin->value();
    
    emit markersChanged();
}

/**
 * @brief Clears the polygon vertices for the selected marker.
 */
void MarkersDialog::onClearPolygon() {
    if (!m_sprite) {
        return;
    }
    int row = m_listWidget->currentRow();
    if (row >= 0 && row < m_sprite->points.size()) {
        m_sprite->points[row].polygonPoints.clear();
        emit markersChanged();
    }
}