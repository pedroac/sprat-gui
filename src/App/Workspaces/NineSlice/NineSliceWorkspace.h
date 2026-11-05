#pragma once
#include <QWidget>
#include "AppSettings.h"
#include "IWorkspace.h"

class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
class QComboBox;
class QStackedWidget;
class QTreeWidgetItem;
class NineSliceEditorCanvas;
class NavigatorPanel;
class ProjectSession;

/**
 * @brief Workspace for editing per-sprite nine-slice configurations.
 *
 * Two-panel layout:
 *  - Left:  the nine-sliced sprites (NavigatorPanel), with Add… (a SpritePickerDialog
 *           of the other sprites) and Remove above them.
 *  - Right: inset/mode fields + slice editor canvas.
 *
 * Selecting a sprite in the navigator loads it into the canvas and config.
 * Removing a sprite keeps its insets, so adding it again restores them.
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
    /// Rebuilds the sprite list after the project's sprites change, keeping the current sprite
    /// selected, or showing another when it is gone.
    void refreshSprites();
    /// Nine-slices the sprites at `paths`, giving any without insets thirds, and shows the first.
    void addSprites(const QStringList& paths);
    /// Stops nine-slicing the sprites at `paths`, keeping their insets, and shows a neighbour.
    void removeSprites(const QStringList& paths);
    NineSliceEditorCanvas* canvas() const { return m_canvas; }
    QDoubleSpinBox* zoomSpin() const { return m_zoomSpin; }
    NavigatorPanel* navigatorPanel() const { return m_navigatorPanel; }

signals:
    void definitionsChanged();

private slots:
    void onNavigatorSelectionChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onAddSpritesClicked();
    void onRemoveSpritesClicked();
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
    /// Rebuilds the list of nine-sliced sprites and shows the empty state when there are none.
    void refreshNavigator();
    /// Selects and loads the sprite at `path`, or the first listed sprite when `path` isn't
    /// listed. Clears the editor when the list is empty. Doesn't save the current sprite.
    void showSprite(const QString& path);
    void clearSpriteView();
    void setNineSliced(const QStringList& paths, bool nineSliced, const QString& nextPath);
    /// The listed sprite to show after the sprites at `removedPaths` leave the list.
    QString spriteAfterRemoving(const QStringList& removedPaths) const;
    void updateFixedDimensions();
    /// Caps each inset spin box at the source size minus the opposite inset, so
    /// left + right and top + bottom always fit the image, as sprat-cli requires.
    void updateInsetLimits();
    /// Lifts the caps set by updateInsetLimits().
    void resetInsetLimits();

    ProjectSession* m_session = nullptr;
    bool m_updatingUi = false;
    AppSettings m_settings;
    QString m_currentSpritePath;

    NavigatorPanel* m_navigatorPanel = nullptr;
    QStackedWidget* m_navigatorStack = nullptr;
    QWidget*        m_emptyState     = nullptr;
    QPushButton*    m_addBtn         = nullptr;
    QPushButton*    m_removeBtn      = nullptr;

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
    QPushButton*    m_linesCheck    = nullptr;
    QPushButton*    m_gridCheck     = nullptr;
    QPushButton*    m_colorizeCheck = nullptr;

    NineSliceEditorCanvas* m_canvas = nullptr;
};
