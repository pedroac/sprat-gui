#pragma once
#include <QWidget>
#include "models.h"

class NavigatorPanel;
class ProjectSession;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTreeWidgetItem;

/**
 * @class AtlasesManagementWorkspace
 * @brief Workspace for managing named atlases within a project.
 *
 * Allows creating, renaming, and removing named atlases, and assigning
 * sprites to atlases via drag-and-drop or context menu. Provides two
 * view modes: Navigation (sprite tree) and Layout (packed atlas canvas).
 */
class AtlasesManagementWorkspace : public QWidget {
    Q_OBJECT
public:
    enum class ViewMode { Navigation, Layout };

    explicit AtlasesManagementWorkspace(QWidget* parent = nullptr);

    void setAtlases(const QVector<AtlasEntry>& atlases, int activeIndex);
    void setSources(const QVector<ProjectSource>& sources);
    void setSession(const ProjectSession* session);
    void refreshSpriteList(const QVector<AtlasEntry>& allAtlases, const QStringList& activeFramePaths = {});
    int  selectedAtlasIndex() const;

    // Canvas for Layout mode
    void     setCanvasWidget(QWidget* widget);
    void     clearCanvasWidget();
    ViewMode viewMode() const { return m_viewMode; }

    // Zoom (Layout mode)
    void   setZoom(double percent);  // no signal emitted
    double currentZoom() const;

    // Profile management
    void        setProfiles(const QStringList& names, const QStringList& labels,
                            const QString& currentProfile,
                            const QStringList& enabledProfiles = {});
    void        setSelectedProfile(const QString& name);  // no signal emitted
    QString     selectedProfile() const;
    QStringList enabledProfiles()  const;
    /** Switch between global (all atlases) and per-atlas profile enablement. */
    void        setProfilesGlobal(bool global);
    bool        isProfilesGlobal() const;

    // Resolution selector
    void    setResolutionOptions(const QStringList& options, const QString& current);
    void    setCurrentResolution(const QString& res);  // no signal emitted
    QString currentResolution() const;

signals:
    void atlasSelected(int index);
    void atlasRenamed(int index, const QString& newName);
    void addAtlasRequested();
    void removeAtlasRequested(int index);
    void moveSpritesRequested(const QStringList& spritePaths, int sourceAtlasIndex, int targetAtlasIndex);
    void createAtlasFromGroupRequested(const QString& groupName, const QStringList& paths);
    void autoCreateAtlasesRequested(const QVector<QPair<QString, QStringList>>& groups);

    void selectedProfileChanged(const QString& profileName);
    void profileEnablementChanged(const QStringList& enabledProfiles);
    void profilesGlobalChanged(bool global);
    void manageProfilesRequested();
    void resolutionChanged(const QString& resolution);

    /** Emitted when the per-atlas export profile override changes for the selected atlas. */
    void atlasExportProfilesChanged(int atlasIndex, QStringList profiles);

    void viewModeChanged(AtlasesManagementWorkspace::ViewMode mode);
    void zoomChanged(double percent);
    void layoutFilterChanged(const QString& query);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onAtlasListSelectionChanged();
    void onAtlasNameEditFinished();
    void onAddClicked();
    void onRemoveClicked();
    void onSpriteContextMenu(const QPoint& pos);
    void filterSpriteTree(const QString& text);
    void onViewModeToggled(int buttonId);

private:
    void setupUi();
    void updateRightPanel();
    void updateProfilesVisibility();
    void rebuildProfileCombo();
    void updateProfileListEnabledStates();
    void updateStatsLabel();
    void setDragHoverRow(int row);
    void ensureValidProfileSelection();
    void loadAtlasProfilesIntoList(int atlasRow);

    // Left panel
    QListWidget* m_atlasList   = nullptr;
    QPushButton* m_addBtn      = nullptr;
    QPushButton* m_removeBtn   = nullptr;

    // Center
    QStackedWidget* m_centerStack = nullptr;
    QWidget*        m_canvasPane  = nullptr;
    NavigatorPanel* m_navigator   = nullptr;

    // Right panel
    QButtonGroup*   m_modeGroup         = nullptr;
    QLineEdit*      m_atlasNameEdit     = nullptr;
    QLineEdit*      m_spriteFilterEdit  = nullptr;
    QLabel*         m_spriteFilterLabel = nullptr;
    QWidget*        m_zoomRow           = nullptr;
    QDoubleSpinBox* m_zoomSpin          = nullptr;
    QComboBox*      m_resolutionCombo   = nullptr;
    QLabel*         m_statsLabel        = nullptr;
    QListWidget*    m_profileList       = nullptr;
    QComboBox*      m_profileCombo      = nullptr;
    QPushButton*    m_manageBtn         = nullptr;

    // Profile "From Default" checkbox
    QCheckBox*   m_profilesGlobalCheckBox = nullptr;
    QStringList  m_savedGlobalProfiles;   ///< Saved when switching to per-atlas mode

    QWidget*   m_rightPanel          = nullptr;
    QSplitter* m_splitter            = nullptr;
    bool       m_splitterInitialized = false;

    QVector<AtlasEntry>    m_atlases;
    QVector<ProjectSource> m_sources;
    const ProjectSession*  m_session = nullptr;
    int      m_dragHoverRow              = -1;
    int      m_contextMenuSourceAtlasRow = -1;
    bool     m_syncing      = false;
    ViewMode m_viewMode     = ViewMode::Navigation;
};
