#pragma once
#include <QWidget>
#include <QPointF>
#include <QList>
#include <QVector>
#include "IWorkspace.h"
#include "models.h"
#include "../../../Profiles/SpratProfilesConfig.h"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTimer;
class QTreeWidgetItem;
class LayoutCanvas;
class NavigatorPanel;
class SpriteEditorPanel;
class ProjectSession;
class QUndoStack;
struct AppSettings;
struct CliPaths;
struct MarkerTemplate;
class MarkerRepository;

/**
 * @class AtlasWorkspace
 * @brief Owns all UI and view-state for the Atlas (Sprites) workspace.
 *
 * AtlasWorkspace is the content widget of the atlas dock. It owns
 * m_canvas, m_navigatorPanel, m_spriteEditorPanel, m_atlasViewStack,
 * m_atlasSplitter, and the profile/resolution controls.
 *
 * Public accessors let MainWindow keep its existing pointer members valid
 * while ownership is logically in AtlasWorkspace.  Those mirrors will be
 * removed in a later clean-up pass.
 *
 * Lifecycle is driven by switchWorkspace() via IWorkspace::enter()/leave().
 * enter() restores preview-canvas and splitter view state.
 * leave() saves it.
 *
 * widget() returns nullptr: AtlasWorkspace is the atlas-dock content, not
 * a central-stack page.
 */
class AtlasWorkspace : public QWidget, public IWorkspace {
    Q_OBJECT
public:
    /**
     * @param session    Non-owning pointer to the active project session.
     * @param undoStack  Non-owning pointer to the application undo stack.
     * @param settings   Non-owning pointer to the application settings.
     * @param cliPaths   Non-owning pointer to the CLI paths config.
     * @param parent     Parent widget.
     */
    AtlasWorkspace(ProjectSession* session,
                   QUndoStack*     undoStack,
                   AppSettings*    settings,
                   CliPaths*       cliPaths,
                   QWidget*        parent = nullptr);

    // ── IWorkspace ────────────────────────────────────────────────────────────
    void    enter()  override;
    void    leave()  override;
    QWidget* widget() override { return nullptr; }  // dock-only

    // ── Widget accessors (used by MainWindow for backward compat) ─────────────
    LayoutCanvas*      canvas()               const { return m_canvas; }
    NavigatorPanel*    navigatorPanel()        const { return m_navigatorPanel; }
    SpriteEditorPanel* spriteEditorPanel()     const { return m_spriteEditorPanel; }
    QStackedWidget*    atlasViewStack()        const { return m_atlasViewStack; }
    QSplitter*         atlasSplitter()         const { return m_atlasSplitter; }
    QComboBox*         profileCombo()          const { return m_profileCombo; }
    QPushButton*       addProfilesBtn()        const { return m_addProfilesBtn; }
    QComboBox*         sourceResolutionCombo() const { return m_sourceResolutionCombo; }
    QTimer*            sourceResolutionDebounceTimer() const { return m_sourceResolutionDebounceTimer; }
    double             layoutZoom()            const { return m_layoutZoom; }
    bool               showHiddenItems()       const { return m_showHiddenItems; }

    // ── Setters (MainWindow → AtlasWorkspace) ─────────────────────────────────
    /**
     * Update the session pointer (called when a new project is loaded).
     * Does not refresh the navigator — call refreshNavigator() separately.
     */
    void setSession(ProjectSession* session);

    /** Populate profile combo and hidden-profile selector with the given list. */
    void setProfiles(const QVector<SpratProfile>& profiles, const QString& current);

    /** Replace the resolution combo contents and select @p current. */
    void setResolutionOptions(const QStringList& options, const QString& current);

    /** Programmatically select a sprite (syncs pivot spins, overlay, etc.). */
    void selectSprite(SpritePtr sprite);

    /** Rebuild the navigator tree from the current session state. */
    void refreshNavigator();

    /** Rebuild the handle combo from the current selected sprite's points. */
    void refreshHandleCombo();
    void refreshMarkerDisplay();

    /** Enable / disable controls based on project / loading state. */
    void updateUiState(bool hasProject, bool isLoading, bool cliReady);

    /** Update the layout canvas zoom (percent). */
    void setCanvasZoom(double percent);

    /**
     * Push an undo command that replaces the selected sprite's named points
     * and refreshes the handle combo + preview overlay.
     * Called by MainWindow after clipboard paste or template application.
     */
    void applyMarkers(SpritePtr sprite, const QVector<NamedPoint>& points);

    /** Rebuild the marker-templates button menu. */
    void refreshMarkerTemplatesMenu(const QVector<MarkerTemplate>& templates);

    /** Returns the owned MarkerRepository (clipboard + templates). */
    MarkerRepository* markerRepository() const { return m_markerRepo; }

    /** Set the canvas layout zoom (internal zoom state mirror). */
    void setLayoutZoom(double zoom) { m_layoutZoom = zoom; }

    /** Re-sync the sprite editor from the current session state (call after undo/redo). */
    void refreshSpriteEditor();

    /** Clear any pending coordinate override (call before undo/redo). */
    void clearCoordinateOverride();

    /** Returns the coordinate space size for pivot calculations. */
    QSize spriteCoordinateSpaceSize(const SpritePtr& sprite) const;

signals:
    // ── Canvas ────────────────────────────────────────────────────────────────
    void layoutRebuildNeeded(bool immediate);
    void canvasZoomChanged(double percent);  ///< Emitted when the layout canvas zoom changes

    // ── Profile / resolution ──────────────────────────────────────────────────
    void profileChangeRequested(const QString& name);
    void resolutionChangeRequested(const QString& res);

    // ── Navigator ─────────────────────────────────────────────────────────────
    void showHiddenToggled(bool show);
    void deleteFramesRequested(const QStringList& paths);
    void addSmartFolderRequested();
    void addFramesToFolderRequested(const QString& subfolder);
    void addToTimelineRequested(const QStringList& paths);
    void createTimelineRequested(const QStringList& paths, QTreeWidgetItem* contextItem);
    void createGroupRequested(const QStringList& paths, const QString& parentFolder);
    void deleteGroupRequested(QTreeWidgetItem* item);
    void autoCreateTimelinesForSourceRequested(int sourceIndex);
    void spriteDroppedToTimeline(const QStringList& paths, const QString& targetFolder);
    void spriteSelected(SpritePtr sprite);
    void canvasSelectionChanged(const QList<SpritePtr>& selection);

    // ── Sprite editor ─────────────────────────────────────────────────────────
    void spriteRenameRequested(SpritePtr sprite, const QString& newName);
    void editAliasesRequested(SpritePtr sprite);
    void showTrimRectToggled(bool show);
    void onionSkinToggled();
    void showGridToggled(bool show);

    // ── Animation canvas sync (emitted so MainWindow can update animCanvas) ───
    void selectedMarkerChanged(const QString& name);

    // ── Status bar ────────────────────────────────────────────────────────────
    void statusMessage(const QString& text);

    // ── General data events ───────────────────────────────────────────────────
    /** Emitted after pivot/marker changes so MainWindow can refresh anim / onion skin. */
    void spriteDataChanged();

public slots:
    // Connected from external overlays (anim canvas), must be public.
    void onCanvasPivotChanged(int x, int y);
    void onMarkerSelectedFromCanvas(const QString& name);
    void onMarkerChangedFromCanvas();

private slots:
    void onPreviewZoomChanged(double value);
    void onPivotSpinChanged();
    void onCopyMarkersRequested();
    void onPasteMarkersRequested();
    void onSaveMarkerTemplate();
    void onApplyMarkerTemplate(const MarkerTemplate& tmpl);
    void onDeleteMarkerTemplate(const QString& name);
    void onPointsConfigClicked();
    void onHandleComboChanged(int index);
    void onCoordUnitChanged();
    void onLayoutZoomChanged(double value);
    void onSpriteNameEditingFinished();
    void filterSpriteTree(const QString& text);
    void onSpriteTreeContextMenu(const QPoint& pos);

private:
    void setupUi();
    void applyMarkersToSelection(const QVector<NamedPoint>& points);
    void clearCoordinateFieldOverride();
    void storeCoordinateFieldOverride();
    bool coordinateFieldOverrideApplies() const;
    void syncPivotSpinsFromSprite();
    void syncCoordinateSpinsFromSelection();

    // ── Owned widgets ─────────────────────────────────────────────────────────
    LayoutCanvas*      m_canvas                      = nullptr;
    NavigatorPanel*    m_navigatorPanel               = nullptr;
    SpriteEditorPanel* m_spriteEditorPanel            = nullptr;
    QStackedWidget*    m_atlasViewStack               = nullptr;
    QSplitter*         m_atlasSplitter                = nullptr;
    QComboBox*         m_profileCombo                 = nullptr;
    QPushButton*       m_addProfilesBtn               = nullptr;
    QComboBox*         m_sourceResolutionCombo        = nullptr;
    QTimer*            m_sourceResolutionDebounceTimer = nullptr;

    // ── View state (persisted across enter/leave cycles) ──────────────────────
    double     m_layoutZoom        = 100.0;
    double     m_savedPreviewZoom  = -1.0;
    QPointF    m_savedPreviewCenter;
    QList<int> m_atlasSplitterHSizes;
    bool       m_showHiddenItems   = false;

    struct CoordinateFieldOverride {
        bool         active       = false;
        const Sprite* sprite      = nullptr;
        QString      markerName;
        CoordUnit    unit         = CoordUnit::Pixels;
        bool         showTrimRect = false;
        double       x            = 0.0;
        double       y            = 0.0;
    };
    CoordinateFieldOverride m_coordinateFieldOverride;

    // ── Owned helpers ─────────────────────────────────────────────────────────
    MarkerRepository* m_markerRepo = nullptr;

    // ── Non-owning refs (passed from MainWindow) ──────────────────────────────
    ProjectSession* m_session   = nullptr;
    QUndoStack*     m_undoStack = nullptr;
    AppSettings*    m_settings  = nullptr;
    CliPaths*       m_cliPaths  = nullptr;
};
