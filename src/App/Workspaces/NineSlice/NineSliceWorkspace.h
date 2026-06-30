#pragma once
#include <QWidget>
#include "AppSettings.h"
#include "IWorkspace.h"

class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
class QComboBox;
class QTreeWidgetItem;
class NineSliceEditorCanvas;
class NavigatorPanel;
class ProjectSession;

/**
 * @brief Workspace for editing per-sprite nine-slice configurations.
 *
 * Two-panel layout:
 *  - Left:  NavigatorPanel (sprite tree + filter) with an optional
 *           "nine-sliced only" toggle above the tree.
 *  - Right: Nine-Sliced toggle + inset/mode fields + slice editor canvas.
 *
 * Selecting a sprite in the navigator loads it into the canvas and config.
 * Checking "Nine-Sliced" enables the inset fields and shows slice lines.
 */
class NineSliceWorkspace : public QWidget, public IWorkspace {
    Q_OBJECT
public:
    explicit NineSliceWorkspace(ProjectSession* session, QWidget* parent = nullptr);

    // IWorkspace
    void enter() override;
    void leave() override;
    QWidget* widget() override { return this; }

    void setSettings(const AppSettings& settings);
    NineSliceEditorCanvas* canvas() const { return m_canvas; }
    NavigatorPanel* navigatorPanel() const { return m_navigatorPanel; }

signals:
    void definitionsChanged();

private slots:
    void onNavigatorSelectionChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onNineSlicedToggled(bool checked);
    void onInsetSpinChanged();
    void onCanvasInsetsChanged(int left, int top, int right, int bottom);
    void onFillModeChanged();
    void onCanvasZoomChanged(double zoom);
    void onCanvasTargetSizeChanged(int w, int h);
    void onNavigatorContextMenuRequested(const QPoint& pos);

private:
    void setupUi();
    void loadSprite(const QString& path);
    void saveCurrentSprite();
    void applyNineSliceFilter();

    ProjectSession* m_session = nullptr;
    bool m_updatingUi = false;
    AppSettings m_settings;
    QString m_currentSpritePath;

    NavigatorPanel* m_navigatorPanel      = nullptr;
    QPushButton*    m_filterNineSlicedBtn = nullptr;
    QPushButton*    m_nineSlicedBtn       = nullptr;

    QWidget* m_configGroup = nullptr;

    QSpinBox*  m_leftSpin    = nullptr;
    QSpinBox*  m_topSpin     = nullptr;
    QSpinBox*  m_rightSpin   = nullptr;
    QSpinBox*  m_bottomSpin  = nullptr;
    QComboBox* m_hModeCombo  = nullptr;
    QComboBox* m_vModeCombo  = nullptr;
    QDoubleSpinBox* m_zoomSpin   = nullptr;
    QSpinBox*       m_widthSpin  = nullptr;
    QSpinBox*       m_heightSpin = nullptr;
    QPushButton*    m_gridCheck     = nullptr;
    QPushButton*    m_colorizeCheck = nullptr;

    NineSliceEditorCanvas* m_canvas = nullptr;
};
