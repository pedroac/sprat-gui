#include "SettingsDialog.h"
#include <QDir>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QFileDialog>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QApplication>
#include <QScrollArea>
#include <QStyle>
#include <QScrollBar>
#include <QTimer>

SettingsDialog::SettingsDialog(const AppSettings& settings, const CliPaths& cliPaths, QWidget* parent, Section section)
    : QDialog(parent), m_settings(settings), m_cliPaths(cliPaths), m_initialSection(section) {
    setupUi();
}

void SettingsDialog::setupUi() {
    switch (m_initialSection) {
        case Section::Spritesheet: setWindowTitle(tr("Atlas Sprites Settings")); break;
        case Section::FramesEditor: setWindowTitle(tr("Frames Editor Settings")); break;
        case Section::AtlasLayout: setWindowTitle(tr("Atlas Layout Settings")); break;
        case Section::Exportation: setWindowTitle(tr("Exportation Settings")); break;
        case Section::SpritesNavigator: setWindowTitle(tr("Sprites Navigator Settings")); break;
#ifndef Q_OS_WASM
        case Section::CliTools: setWindowTitle(tr("CLI Tools Settings")); break;
#endif
    }

    QVBoxLayout* layout = new QVBoxLayout(this);

    QWidget* content = new QWidget(this);
    QVBoxLayout* contentLayout = new QVBoxLayout(content);

    // Atlas Sprites Group
    m_spritesheetGroup = new QGroupBox(tr("Atlas Sprites"), content);
    QFormLayout* spritesheetForm = new QFormLayout(m_spritesheetGroup);

    auto* dedupDesc = new QLabel(tr("Deduplication creates aliases for identical or similar sprites, "
                                    "allowing them to share the same space in the atlas."), this);
    dedupDesc->setWordWrap(true);
    dedupDesc->setStyleSheet("color: #666; margin-bottom: 4px;");
    spritesheetForm->addRow(dedupDesc);

    m_deduplicateModeCombo = new QComboBox(this);
    m_deduplicateModeCombo->addItem(tr("None (no deduplication)"), "none");
    m_deduplicateModeCombo->addItem(tr("Exact (identical images)"), "exact");
    m_deduplicateModeCombo->addItem(tr("Perceptual (similar images)"), "perceptual");

    int deducateIndex = m_deduplicateModeCombo->findData(m_settings.deduplicateMode);
    if (deducateIndex >= 0) {
        m_deduplicateModeCombo->setCurrentIndex(deducateIndex);
    }

    m_deduplicateModeCombo->setToolTip(tr("Remove duplicate sprites from the layout"));
    m_deduplicateModeCombo->setAccessibleName(tr("Deduplicate mode"));
    spritesheetForm->addRow(tr("Deduplicate Sprites:"), m_deduplicateModeCombo);

    // Sync controls
    m_syncModeCombo = new QComboBox(this);
    m_syncModeCombo->addItem(tr("None (no sync)"), (int)SyncMode::None);
    m_syncModeCombo->addItem(tr("Manual (sync on demand)"), (int)SyncMode::Manual);
    m_syncModeCombo->addItem(tr("Watch (live monitoring)"), (int)SyncMode::Watch);
    int syncIndex = m_syncModeCombo->findData((int)m_settings.syncMode);
    if (syncIndex >= 0) {
        m_syncModeCombo->setCurrentIndex(syncIndex);
    }
    m_syncModeCombo->setToolTip(tr("How to synchronize source folder with project"));
    m_syncModeCombo->setAccessibleName(tr("Sync mode"));
    connect(m_syncModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onSyncModeChanged);
    spritesheetForm->addRow(tr("Sync Mode:"), m_syncModeCombo);

    m_syncNowBtn = new QPushButton(tr("Sync Now"), this);
    m_syncNowBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_BrowserReload));
    m_syncNowBtn->setEnabled(m_settings.syncMode != SyncMode::None);
    connect(m_syncNowBtn, &QPushButton::clicked, this, &SettingsDialog::onSyncNowClicked);
    spritesheetForm->addRow("", m_syncNowBtn);

    contentLayout->addWidget(m_spritesheetGroup);
    m_spritesheetGroup->setVisible(m_initialSection == Section::Spritesheet);

    // Frames Editor Group
    m_framesEditorGroup = new QGroupBox(tr("Frames Editor"), content);
    QFormLayout* framesEditorForm = new QFormLayout(m_framesEditorGroup);

    m_canvasColorBtn = createColorButton(m_settings.workspaceColor);
    m_canvasColorBtn->setToolTip(tr("Color of the workspace area outside sprites"));
    m_canvasColorBtn->setAccessibleName(tr("Workspace color"));
    connect(m_canvasColorBtn, &QPushButton::clicked, this, [this]() { pickColor(m_canvasColorBtn, m_settings.workspaceColor); });
    framesEditorForm->addRow(tr("Workspace Background:"), m_canvasColorBtn);

    m_frameColorBtn = createColorButton(m_settings.spriteFrameColor);
    m_frameColorBtn->setToolTip(tr("Background color of sprite frames"));
    m_frameColorBtn->setAccessibleName(tr("Sprite frame color"));
    connect(m_frameColorBtn, &QPushButton::clicked, this, [this]() { pickColor(m_frameColorBtn, m_settings.spriteFrameColor); });
    framesEditorForm->addRow(tr("Sprite Frame Background:"), m_frameColorBtn);

    m_checkerboardCheck = new QCheckBox(tr("Show Transparency Checkerboard"), this);
    m_checkerboardCheck->setChecked(m_settings.showCheckerboard);
    m_checkerboardCheck->setToolTip(tr("Show checkerboard pattern for transparent areas"));
    m_checkerboardCheck->setAccessibleName(tr("Transparency checkerboard"));
    framesEditorForm->addRow("", m_checkerboardCheck);

    m_onionSkinOpacitySpin = new QSpinBox(this);
    m_onionSkinOpacitySpin->setRange(0, 100);
    m_onionSkinOpacitySpin->setSuffix(tr(" %"));
    m_onionSkinOpacitySpin->setValue(m_settings.onionSkinOpacity);
    m_onionSkinOpacitySpin->setToolTip(tr("Opacity of ghost overlays for other checked frames (0 = disabled)"));
    framesEditorForm->addRow(tr("Onion skin opacity:"), m_onionSkinOpacitySpin);

    m_propagateEditsCheck = new QCheckBox(tr("Apply edits to all checked frames"), this);
    m_propagateEditsCheck->setChecked(m_settings.propagateEditsToChecked);
    m_propagateEditsCheck->setToolTip(tr("Automatically apply pivot and marker edits to all checked frames simultaneously"));
    framesEditorForm->addRow("", m_propagateEditsCheck);

    m_flipbookModeCombo = new QComboBox(this);
    m_flipbookModeCombo->addItem(tr("None"), "none");
    m_flipbookModeCombo->addItem(tr("Same group"), "same_group");
    m_flipbookModeCombo->addItem(tr("All frames"), "all");
    {
        int idx = m_flipbookModeCombo->findData(flipbookModeToString(m_settings.flipbookMode));
        if (idx >= 0) m_flipbookModeCombo->setCurrentIndex(idx);
    }
    m_flipbookModeCombo->setToolTip(tr("Keep the pivot at the same screen position when navigating between frames"));
    framesEditorForm->addRow(tr("Flipbook pivot:"), m_flipbookModeCombo);

    m_frameZoomModeCombo = new QComboBox(this);
    m_frameZoomModeCombo->addItem(tr("Fit to frame"), "fit");
    m_frameZoomModeCombo->addItem(tr("No change (keep previous)"), "keep");
    m_frameZoomModeCombo->addItem(tr("100%"), "reset_100");
    {
        int idx = m_frameZoomModeCombo->findData(frameZoomModeToString(m_settings.frameZoomMode));
        if (idx >= 0) m_frameZoomModeCombo->setCurrentIndex(idx);
    }
    m_frameZoomModeCombo->setToolTip(tr("How zoom behaves when changing frames (outside flipbook mode)"));
    framesEditorForm->addRow(tr("Zoom on frame change:"), m_frameZoomModeCombo);

    // Disable zoom-on-change when flipbook is "All frames" (zoom is locked by flipbook logic).
    auto updateZoomComboEnabled = [this]() {
        m_frameZoomModeCombo->setEnabled(m_flipbookModeCombo->currentData().toString() != "all");
    };
    updateZoomComboEnabled();
    connect(m_flipbookModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [updateZoomComboEnabled](int) { updateZoomComboEnabled(); });

    m_trimRectColorBtn = createColorButton(m_settings.trimRectColor);
    m_trimRectColorBtn->setToolTip(tr("Color of the trim area boundary rectangle"));
    m_trimRectColorBtn->setAccessibleName(tr("Trim rect color"));
    connect(m_trimRectColorBtn, &QPushButton::clicked, this, [this]() { pickColor(m_trimRectColorBtn, m_settings.trimRectColor); });
    framesEditorForm->addRow(tr("Trim Rect Color:"), m_trimRectColorBtn);

    m_trimRectStyleCombo = new QComboBox(this);
    m_trimRectStyleCombo->addItem(tr("None"), (int)Qt::NoPen);
    m_trimRectStyleCombo->addItem(tr("Solid"), (int)Qt::SolidLine);
    m_trimRectStyleCombo->addItem(tr("Dash"), (int)Qt::DashLine);
    m_trimRectStyleCombo->addItem(tr("Dot"), (int)Qt::DotLine);
    m_trimRectStyleCombo->addItem(tr("DashDot"), (int)Qt::DashDotLine);
    m_trimRectStyleCombo->addItem(tr("DashDotDot"), (int)Qt::DashDotDotLine);
    {
        int idx = m_trimRectStyleCombo->findData((int)m_settings.trimRectStyle);
        if (idx >= 0) m_trimRectStyleCombo->setCurrentIndex(idx);
    }
    m_trimRectStyleCombo->setToolTip(tr("Visual style for the trim area boundary"));
    m_trimRectStyleCombo->setAccessibleName(tr("Trim rect style"));
    framesEditorForm->addRow(tr("Trim Rect Style:"), m_trimRectStyleCombo);

    // Grid overlay controls
    m_gridColorBtn = createColorButton(m_settings.gridColor);
    m_gridColorBtn->setToolTip(tr("Color of the grid overlay lines (supports transparency)"));
    m_gridColorBtn->setAccessibleName(tr("Grid color"));
    connect(m_gridColorBtn, &QPushButton::clicked, this, [this]() { pickColorWithAlpha(m_gridColorBtn, m_settings.gridColor); });
    framesEditorForm->addRow(tr("Grid Color:"), m_gridColorBtn);

    {
        auto* row = new QHBoxLayout();
        m_gridCellWidthSpin = new QSpinBox(this);
        m_gridCellWidthSpin->setRange(1, 9999);
        m_gridCellWidthSpin->setSuffix(tr(" px"));
        m_gridCellWidthSpin->setValue(m_settings.gridCellWidth);
        m_gridCellWidthSpin->setToolTip(tr("Grid cell width in pixels"));
        row->addWidget(new QLabel(tr("W:"), this));
        row->addWidget(m_gridCellWidthSpin);
        m_gridCellHeightSpin = new QSpinBox(this);
        m_gridCellHeightSpin->setRange(1, 9999);
        m_gridCellHeightSpin->setSuffix(tr(" px"));
        m_gridCellHeightSpin->setValue(m_settings.gridCellHeight);
        m_gridCellHeightSpin->setToolTip(tr("Grid cell height in pixels"));
        row->addWidget(new QLabel(tr("H:"), this));
        row->addWidget(m_gridCellHeightSpin);
        framesEditorForm->addRow(tr("Grid Cell Size:"), row);
    }

    {
        auto* row = new QHBoxLayout();
        m_gridOffsetXSpin = new QSpinBox(this);
        m_gridOffsetXSpin->setRange(0, 9999);
        m_gridOffsetXSpin->setSuffix(tr(" px"));
        m_gridOffsetXSpin->setValue(m_settings.gridOffsetX);
        m_gridOffsetXSpin->setToolTip(tr("Grid horizontal offset in pixels"));
        row->addWidget(new QLabel(tr("X:"), this));
        row->addWidget(m_gridOffsetXSpin);
        m_gridOffsetYSpin = new QSpinBox(this);
        m_gridOffsetYSpin->setRange(0, 9999);
        m_gridOffsetYSpin->setSuffix(tr(" px"));
        m_gridOffsetYSpin->setValue(m_settings.gridOffsetY);
        m_gridOffsetYSpin->setToolTip(tr("Grid vertical offset in pixels"));
        row->addWidget(new QLabel(tr("Y:"), this));
        row->addWidget(m_gridOffsetYSpin);
        framesEditorForm->addRow(tr("Grid Offset:"), row);
    }

    contentLayout->addWidget(m_framesEditorGroup);
    m_framesEditorGroup->setVisible(m_initialSection == Section::FramesEditor);

    // Atlas Layout Group
    m_atlasLayoutGroup = new QGroupBox(tr("Atlas Layout"), content);
    QFormLayout* atlasLayoutForm = new QFormLayout(m_atlasLayoutGroup);

    m_borderColorBtn = createColorButton(m_settings.borderColor);
    m_borderColorBtn->setToolTip(tr("Color of borders around sprites"));
    m_borderColorBtn->setAccessibleName(tr("Border color"));
    connect(m_borderColorBtn, &QPushButton::clicked, this, [this]() { pickColor(m_borderColorBtn, m_settings.borderColor); });
    atlasLayoutForm->addRow(tr("Border Color:"), m_borderColorBtn);

    m_detectionSelectedColorBtn = createColorButton(m_settings.detectionSelectedColor);
    m_detectionSelectedColorBtn->setToolTip(tr("Color for frames selected in detection dialog"));
    m_detectionSelectedColorBtn->setAccessibleName(tr("Detection selected color"));
    connect(m_detectionSelectedColorBtn, &QPushButton::clicked, this, [this]() { pickColor(m_detectionSelectedColorBtn, m_settings.detectionSelectedColor); });
    atlasLayoutForm->addRow(tr("Detection Selected Color:"), m_detectionSelectedColorBtn);

    m_borderStyleCombo = new QComboBox(this);
    m_borderStyleCombo->addItem(tr("None"), (int)Qt::NoPen);
    m_borderStyleCombo->addItem(tr("Solid"), (int)Qt::SolidLine);
    m_borderStyleCombo->addItem(tr("Dash"), (int)Qt::DashLine);
    m_borderStyleCombo->addItem(tr("Dot"), (int)Qt::DotLine);
    m_borderStyleCombo->addItem(tr("DashDot"), (int)Qt::DashDotLine);
    m_borderStyleCombo->addItem(tr("DashDotDot"), (int)Qt::DashDotDotLine);
    {
        int idx = m_borderStyleCombo->findData((int)m_settings.borderStyle);
        if (idx >= 0) m_borderStyleCombo->setCurrentIndex(idx);
    }
    m_borderStyleCombo->setToolTip(tr("Visual style for sprite borders"));
    m_borderStyleCombo->setAccessibleName(tr("Border style"));
    atlasLayoutForm->addRow(tr("Border Style:"), m_borderStyleCombo);

    m_layoutZoomOnChangeCombo = new QComboBox(this);
    m_layoutZoomOnChangeCombo->addItem(tr("No change (keep current)"), "no_change");
    m_layoutZoomOnChangeCombo->addItem(tr("Fit to view"), "fit");
    m_layoutZoomOnChangeCombo->addItem(tr("100%"), "reset_100");
    {
        int idx = m_layoutZoomOnChangeCombo->findData(layoutZoomOnChangeToString(m_settings.layoutZoomOnChange));
        if (idx >= 0) m_layoutZoomOnChangeCombo->setCurrentIndex(idx);
    }
    m_layoutZoomOnChangeCombo->setToolTip(tr("How zoom behaves after each atlas layout rebuild"));
    atlasLayoutForm->addRow(tr("Zoom on layout change:"), m_layoutZoomOnChangeCombo);

    m_layoutLabelModeCombo = new QComboBox(this);
    m_layoutLabelModeCombo->addItem(tr("Name"), "name");
    m_layoutLabelModeCombo->addItem(tr("Full path"), "full_path");
    m_layoutLabelModeCombo->addItem(tr("None"), "none");
    {
        int idx = m_layoutLabelModeCombo->findData(layoutLabelModeToString(m_settings.layoutLabelMode));
        if (idx >= 0) m_layoutLabelModeCombo->setCurrentIndex(idx);
    }
    m_layoutLabelModeCombo->setToolTip(tr("Text shown on sprite labels in the atlas layout canvas"));
    atlasLayoutForm->addRow(tr("Show names:"), m_layoutLabelModeCombo);

    contentLayout->addWidget(m_atlasLayoutGroup);
    m_atlasLayoutGroup->setVisible(m_initialSection == Section::AtlasLayout);

    // Exportation Group
    m_exportationGroup = new QGroupBox(tr("Exportation"), content);
    QFormLayout* exportationForm = new QFormLayout(m_exportationGroup);

    m_exportZoomOnChangeCombo = new QComboBox(this);
    m_exportZoomOnChangeCombo->addItem(tr("Fit to view"), "fit");
    m_exportZoomOnChangeCombo->addItem(tr("No change (keep current)"), "no_change");
    m_exportZoomOnChangeCombo->addItem(tr("100%"), "reset_100");
    {
        int idx = m_exportZoomOnChangeCombo->findData(exportZoomOnChangeToString(m_settings.exportZoomOnChange));
        if (idx >= 0) m_exportZoomOnChangeCombo->setCurrentIndex(idx);
    }
    m_exportZoomOnChangeCombo->setToolTip(tr("How zoom behaves after each export preview update"));
    exportationForm->addRow(tr("Zoom on preview change:"), m_exportZoomOnChangeCombo);

    // Output folder row
    auto* exportFolderRow = new QHBoxLayout();
    m_exportDefaultFolderEdit = new QLineEdit(m_settings.exportDefaultOutputFolder, this);
    m_exportDefaultFolderEdit->setReadOnly(true);
    m_exportDefaultFolderEdit->setPlaceholderText(tr("Not set"));
    m_exportDefaultFolderEdit->setToolTip(tr("Default output folder for new exports"));
    auto* exportFolderBtn = new QPushButton(tr("Browse..."), this);
    exportFolderBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
    connect(exportFolderBtn, &QPushButton::clicked, this, [this]() {
        const QString current = m_exportDefaultFolderEdit->text();
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Select Default Export Folder"), current.isEmpty() ? QDir::homePath() : current);
        if (!dir.isEmpty())
            m_exportDefaultFolderEdit->setText(dir);
    });
    exportFolderRow->addWidget(m_exportDefaultFolderEdit, 1);
    exportFolderRow->addWidget(exportFolderBtn);
    exportationForm->addRow(tr("Default output folder:"), exportFolderRow);

    m_exportDefaultFormatCombo = new QComboBox(this);
    m_exportDefaultFormatCombo->addItem(tr("None (no metadata)"), "none");
    m_exportDefaultFormatCombo->addItem(tr("json"), "json");
    m_exportDefaultFormatCombo->addItem(tr("csv"), "csv");
    m_exportDefaultFormatCombo->addItem(tr("xml"), "xml");
    m_exportDefaultFormatCombo->addItem(tr("css"), "css");
    {
        int idx = m_exportDefaultFormatCombo->findData(m_settings.exportDefaultFormat);
        if (idx >= 0) m_exportDefaultFormatCombo->setCurrentIndex(idx);
    }
    m_exportDefaultFormatCombo->setToolTip(tr("Default metadata format for new exports"));
    exportationForm->addRow(tr("Default format:"), m_exportDefaultFormatCombo);

    m_exportDefaultScaleFilterCombo = new QComboBox(this);
    m_exportDefaultScaleFilterCombo->addItem(tr("Nearest (default)"), "nearest");
    m_exportDefaultScaleFilterCombo->addItem(tr("Bilinear"),           "bilinear");
    m_exportDefaultScaleFilterCombo->addItem(tr("Bicubic"),            "bicubic");
    m_exportDefaultScaleFilterCombo->addItem(tr("Mitchell"),           "mitchell");
    {
        int idx = m_exportDefaultScaleFilterCombo->findData(m_settings.exportDefaultScaleFilter);
        if (idx >= 0) m_exportDefaultScaleFilterCombo->setCurrentIndex(idx);
    }
    m_exportDefaultScaleFilterCombo->setToolTip(tr("Default scale filter for new exports"));
    exportationForm->addRow(tr("Default scale filter:"), m_exportDefaultScaleFilterCombo);

    contentLayout->addWidget(m_exportationGroup);
    m_exportationGroup->setVisible(m_initialSection == Section::Exportation);

    // Sprites Navigator Group
    m_navigatorGroup = new QGroupBox(tr("Sprites Navigator"), content);
    QFormLayout* navigatorForm = new QFormLayout(m_navigatorGroup);

    m_spritePreviewCheck = new QCheckBox(tr("Show tooltip"), this);
    m_spritePreviewCheck->setChecked(m_settings.spritePreviewEnabled);
    m_spritePreviewCheck->setToolTip(tr("Show a floating image preview when hovering a sprite name"));
    navigatorForm->addRow("", m_spritePreviewCheck);

    m_tooltipDelaySpin = new QDoubleSpinBox(this);
    m_tooltipDelaySpin->setRange(0.1, 5.0);
    m_tooltipDelaySpin->setSingleStep(0.1);
    m_tooltipDelaySpin->setDecimals(1);
    m_tooltipDelaySpin->setSuffix(tr(" s"));
    m_tooltipDelaySpin->setValue(m_settings.spritePreviewDelay);
    m_tooltipDelaySpin->setToolTip(tr("Delay before the image preview appears"));
    navigatorForm->addRow(tr("Tooltip delay:"), m_tooltipDelaySpin);

    m_groupSimilarCheck = new QCheckBox(tr("Group similar"), this);
    m_groupSimilarCheck->setChecked(m_settings.navigatorGroupSimilar);
    m_groupSimilarCheck->setToolTip(tr("Group consecutive numbered frames into animation sequence nodes"));
    navigatorForm->addRow("", m_groupSimilarCheck);

    contentLayout->addWidget(m_navigatorGroup);
    m_navigatorGroup->setVisible(m_initialSection == Section::SpritesNavigator);

#ifndef Q_OS_WASM
    m_cliGroup = new QGroupBox(tr("CLI Tools"), content);
    QFormLayout* cliForm = new QFormLayout(m_cliGroup);

    QHBoxLayout* baseDirLayout = new QHBoxLayout();
    m_cliBaseDirEdit = new QLineEdit(m_cliPaths.baseDir, this);
    m_cliBaseDirEdit->setReadOnly(true);
    m_cliBaseDirBtn = new QPushButton(tr("Change..."), this);
    m_cliBaseDirBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon));
    connect(m_cliBaseDirBtn, &QPushButton::clicked, this, &SettingsDialog::pickCliBaseDir);
    baseDirLayout->addWidget(m_cliBaseDirEdit);
    baseDirLayout->addWidget(m_cliBaseDirBtn);
    cliForm->addRow(tr("Base Directory:"), baseDirLayout);
    
    contentLayout->addWidget(m_cliGroup);
    m_cliGroup->setVisible(m_initialSection == Section::CliTools);
#endif

    contentLayout->addStretch(1);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidget(content);
    layout->addWidget(m_scrollArea, 1);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* resetBtn = buttons->addButton(tr("Reset"), QDialogButtonBox::ResetRole);
    resetBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogResetButton));

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(resetBtn, &QPushButton::clicked, this, &SettingsDialog::resetToDefaults);
    layout->addWidget(buttons);
}

void SettingsDialog::focusSection(Section section) {
    Q_UNUSED(section);
}

QPushButton* SettingsDialog::createColorButton(const QColor& color) {
    QPushButton* btn = new QPushButton(this);
    updateColorButton(btn, color);
    return btn;
}

void SettingsDialog::updateColorButton(QPushButton* btn, const QColor& color) {
    QString qss = QString("background-color: %1; border: 1px solid #555;").arg(color.name());
    btn->setStyleSheet(qss);
    btn->setText(color.name());
}

void SettingsDialog::pickColor(QPushButton* btn, QColor& color) {
    QColor newColor = QColorDialog::getColor(color, this, tr("Select Color"));
    if (newColor.isValid()) {
        color = newColor;
        updateColorButton(btn, color);
    }
}

void SettingsDialog::pickColorWithAlpha(QPushButton* btn, QColor& color) {
    QColor newColor = QColorDialog::getColor(color, this, tr("Select Color"),
                                             QColorDialog::ShowAlphaChannel);
    if (newColor.isValid()) {
        color = newColor;
        updateColorButton(btn, color);
    }
}

#ifndef Q_OS_WASM
void SettingsDialog::pickCliBaseDir() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select CLI Tools Directory"), m_cliPaths.baseDir);
    if (!dir.isEmpty()) {
        m_cliPaths.baseDir = dir;
        m_cliBaseDirEdit->setText(dir);
    }
}
#endif

void SettingsDialog::resetToDefaults() {
    m_settings = AppSettings();
    updateColorButton(m_canvasColorBtn, m_settings.workspaceColor);
    updateColorButton(m_frameColorBtn, m_settings.spriteFrameColor);
    updateColorButton(m_borderColorBtn, m_settings.borderColor);
    updateColorButton(m_detectionSelectedColorBtn, m_settings.detectionSelectedColor);
    m_checkerboardCheck->setChecked(m_settings.showCheckerboard);
    if (m_onionSkinOpacitySpin) m_onionSkinOpacitySpin->setValue(AppSettings().onionSkinOpacity);
    if (m_propagateEditsCheck) m_propagateEditsCheck->setChecked(AppSettings().propagateEditsToChecked);
    if (m_flipbookModeCombo) {
        int idx = m_flipbookModeCombo->findData(flipbookModeToString(AppSettings().flipbookMode));
        if (idx >= 0) m_flipbookModeCombo->setCurrentIndex(idx);
    }
    if (m_frameZoomModeCombo) {
        int idx = m_frameZoomModeCombo->findData(frameZoomModeToString(AppSettings().frameZoomMode));
        if (idx >= 0) m_frameZoomModeCombo->setCurrentIndex(idx);
    }
    if (m_layoutZoomOnChangeCombo) {
        int idx = m_layoutZoomOnChangeCombo->findData(layoutZoomOnChangeToString(AppSettings().layoutZoomOnChange));
        if (idx >= 0) m_layoutZoomOnChangeCombo->setCurrentIndex(idx);
    }
    if (m_layoutLabelModeCombo) {
        int idx = m_layoutLabelModeCombo->findData(layoutLabelModeToString(AppSettings().layoutLabelMode));
        if (idx >= 0) m_layoutLabelModeCombo->setCurrentIndex(idx);
    }
    if (m_exportZoomOnChangeCombo) {
        int idx = m_exportZoomOnChangeCombo->findData(exportZoomOnChangeToString(AppSettings().exportZoomOnChange));
        if (idx >= 0) m_exportZoomOnChangeCombo->setCurrentIndex(idx);
    }
    if (m_exportDefaultFolderEdit) {
        m_exportDefaultFolderEdit->setText(QDir::homePath() + "/Sprat");
    }
    if (m_exportDefaultFormatCombo) {
        int idx = m_exportDefaultFormatCombo->findData(AppSettings().exportDefaultFormat);
        if (idx >= 0) m_exportDefaultFormatCombo->setCurrentIndex(idx);
    }
    if (m_exportDefaultScaleFilterCombo) {
        int idx = m_exportDefaultScaleFilterCombo->findData(AppSettings().exportDefaultScaleFilter);
        if (idx >= 0) m_exportDefaultScaleFilterCombo->setCurrentIndex(idx);
    }

    int deduplicateIndex = m_deduplicateModeCombo->findData(m_settings.deduplicateMode);
    if (deduplicateIndex >= 0) {
        m_deduplicateModeCombo->setCurrentIndex(deduplicateIndex);
    }

    int borderIndex = m_borderStyleCombo->findData((int)m_settings.borderStyle);
    if (borderIndex >= 0) {
        m_borderStyleCombo->setCurrentIndex(borderIndex);
    }
    if (m_spritePreviewCheck) m_spritePreviewCheck->setChecked(AppSettings().spritePreviewEnabled);
    if (m_tooltipDelaySpin)   m_tooltipDelaySpin->setValue(AppSettings().spritePreviewDelay);
    if (m_groupSimilarCheck)  m_groupSimilarCheck->setChecked(AppSettings().navigatorGroupSimilar);
    if (m_trimRectColorBtn) updateColorButton(m_trimRectColorBtn, AppSettings().trimRectColor);
    if (m_trimRectStyleCombo) {
        int idx = m_trimRectStyleCombo->findData((int)AppSettings().trimRectStyle);
        if (idx >= 0) m_trimRectStyleCombo->setCurrentIndex(idx);
    }
    if (m_gridColorBtn) updateColorButton(m_gridColorBtn, AppSettings().gridColor);
    if (m_gridCellWidthSpin)  m_gridCellWidthSpin->setValue(AppSettings().gridCellWidth);
    if (m_gridCellHeightSpin) m_gridCellHeightSpin->setValue(AppSettings().gridCellHeight);
    if (m_gridOffsetXSpin)    m_gridOffsetXSpin->setValue(AppSettings().gridOffsetX);
    if (m_gridOffsetYSpin)    m_gridOffsetYSpin->setValue(AppSettings().gridOffsetY);

    int syncIndex = m_syncModeCombo->findData((int)m_settings.syncMode);
    if (syncIndex >= 0) {
        m_syncModeCombo->setCurrentIndex(syncIndex);
    }
}

AppSettings SettingsDialog::getSettings() const {
    AppSettings s = m_settings;
    s.showCheckerboard = m_checkerboardCheck->isChecked();
    s.borderStyle = (Qt::PenStyle)m_borderStyleCombo->currentData().toInt();
    s.deduplicateMode = m_deduplicateModeCombo->currentData().toString();
    s.syncMode = (SyncMode)m_syncModeCombo->currentData().toInt();
    if (m_onionSkinOpacitySpin) s.onionSkinOpacity = m_onionSkinOpacitySpin->value();
    if (m_propagateEditsCheck) s.propagateEditsToChecked = m_propagateEditsCheck->isChecked();
    if (m_flipbookModeCombo) s.flipbookMode = flipbookModeFromString(m_flipbookModeCombo->currentData().toString());
    if (m_frameZoomModeCombo) s.frameZoomMode = frameZoomModeFromString(m_frameZoomModeCombo->currentData().toString());
    if (m_layoutZoomOnChangeCombo) s.layoutZoomOnChange = layoutZoomOnChangeFromString(m_layoutZoomOnChangeCombo->currentData().toString());
    if (m_layoutLabelModeCombo) s.layoutLabelMode = layoutLabelModeFromString(m_layoutLabelModeCombo->currentData().toString());
    if (m_exportZoomOnChangeCombo) s.exportZoomOnChange = exportZoomOnChangeFromString(m_exportZoomOnChangeCombo->currentData().toString());
    if (m_exportDefaultFolderEdit) s.exportDefaultOutputFolder = m_exportDefaultFolderEdit->text().trimmed();
    if (m_exportDefaultFormatCombo) s.exportDefaultFormat = m_exportDefaultFormatCombo->currentData().toString();
    if (m_exportDefaultScaleFilterCombo) s.exportDefaultScaleFilter = m_exportDefaultScaleFilterCombo->currentData().toString();
    if (m_trimRectStyleCombo) s.trimRectStyle = (Qt::PenStyle)m_trimRectStyleCombo->currentData().toInt();
    if (m_gridCellWidthSpin)  s.gridCellWidth  = m_gridCellWidthSpin->value();
    if (m_gridCellHeightSpin) s.gridCellHeight = m_gridCellHeightSpin->value();
    if (m_gridOffsetXSpin)    s.gridOffsetX    = m_gridOffsetXSpin->value();
    if (m_gridOffsetYSpin)    s.gridOffsetY    = m_gridOffsetYSpin->value();
    if (m_spritePreviewCheck) s.spritePreviewEnabled = m_spritePreviewCheck->isChecked();
    if (m_tooltipDelaySpin)   s.spritePreviewDelay   = m_tooltipDelaySpin->value();
    if (m_groupSimilarCheck)  s.navigatorGroupSimilar = m_groupSimilarCheck->isChecked();
    return s;
}

CliPaths SettingsDialog::getCliPaths() const {
    return m_cliPaths;
}

void SettingsDialog::onSyncModeChanged(int index) {
    Q_UNUSED(index);
    // Enable "Sync Now" button only for Manual and Watch modes
    SyncMode mode = (SyncMode)m_syncModeCombo->currentData().toInt();
    m_syncNowBtn->setEnabled(mode != SyncMode::None);
}

void SettingsDialog::onSyncNowClicked() {
    qInfo() << "[SettingsDialog] Sync Now clicked, emitting signal";
    emit syncNowRequested();
}
