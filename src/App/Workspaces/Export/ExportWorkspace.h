#pragma once
#include <QWidget>
#include <QList>
#include <QPair>
#include "models.h"
#include "SpratProfilesConfig.h"
#include "IWorkspace.h"

class QLabel;
class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QGroupBox;
class QTableWidget;
class QTreeWidget;
class IAtlasViewport;
class ZoomableGraphicsView;

class ExportWorkspace : public QWidget, public IWorkspace {
    Q_OBJECT
public:
    explicit ExportWorkspace(QWidget* parent = nullptr);

    // IWorkspace
    void enter() override;
    void leave() override;
    QWidget* widget() override { return this; }

    void populate(const QVector<SpratProfile>& profiles,
                  const QString& selectedProfileName,
                  const SaveConfig& lastConfig,
                  const QString& startDir);

    SaveConfig getConfig() const;

    void setViewport(IAtlasViewport* viewport);
    void clearViewport();

    void showExportLog(const QVector<ExportLogEntry>& entries, const QString& destination);

    /** Populate the preview atlas combo with atlas names.
     *  @param names          Display names (one per non-excluded atlas).
     *  @param activeSessionIndex  Real session atlas index to pre-select.
     *  @param sessionIndices Parallel list of real session indices stored as combo item data,
     *                        so previewRefreshRequested carries the true session index.
     */
    void setAtlasNames(const QStringList& names, int activeSessionIndex,
                       const QList<int>& sessionIndices);

    /** Populate the per-atlas overrides table.  Shows/hides the group based on count.
     *  @param atlasNames  Display name for each entry, parallel to @p configs.
     *                     When empty, names are resolved from the preview atlas combo (fallback). */
    void setAtlasExportConfigs(const QList<QPair<int,AtlasExportConfig>>& configs,
                               const QStringList& atlasNames = {});

    /** Populate the preset combo. */
    void setPresets(const QVector<ExportPreset>& presets);

    QDoubleSpinBox* zoomSpin() const { return m_zoomSpin; }

    double savedZoom() const { return m_savedZoom; }
    void   setSavedZoom(double z) { m_savedZoom = z; }

signals:
    void exportRequested(SaveConfig config);
    void cancelled();
    /** Emitted when any preview combo (atlas, profile, or scale filter) changes. */
    void previewRefreshRequested(int atlasIndex, QString profileName, QString scaleFilter);
    /** Emitted when a per-atlas override combo changes. */
    void atlasExportConfigChanged(int sessionAtlasIndex, AtlasExportConfig config);
    /** Emitted when the user saves a new preset. */
    void savePresetRequested(ExportPreset preset);
    /** Emitted when the user deletes the current preset. */
    void deletePresetRequested(QString presetName);

private slots:
    void onZoomSpinChanged(double value);
    void onViewZoomChanged(double zoom);
    void onAnyComboChanged();
    void onPresetComboChanged(int index);

private:
    void setupUi();
    void onExportClicked();
    void setPreviewWidget(QWidget* preview);
    void clearPreviewWidget();

    QWidget*             m_previewPane         = nullptr;
    QLabel*              m_noPreviewLabel      = nullptr;  // shown when no viewport is set
    QWidget*             m_viewportWidget      = nullptr;  // non-owning, currently displayed viewport
    QLineEdit*           m_outputPathEdit      = nullptr;
    QLineEdit*           m_postExportCommandEdit   = nullptr;
    QComboBox*           m_transformCombo          = nullptr;
    QComboBox*           m_scaleFilterCombo        = nullptr;
    QComboBox*           m_profileCombo       = nullptr;
    QComboBox*           m_previewAtlasCombo  = nullptr;
    QDoubleSpinBox*      m_zoomSpin         = nullptr;
    QPushButton*         m_exportBtn        = nullptr;
    ZoomableGraphicsView* m_currentView     = nullptr;

    // Preset row
    QComboBox*   m_presetCombo   = nullptr;
    QPushButton* m_savePresetBtn = nullptr;
    QPushButton* m_delPresetBtn  = nullptr;

    // Per-atlas overrides section
    QGroupBox*    m_atlasOverridesGroup = nullptr;
    QTableWidget* m_atlasOverridesTable = nullptr;
    QList<QPair<int,AtlasExportConfig>> m_atlasConfigs;  // sessionIndex → config

    // All globally-enabled profiles (set in populate()); used to re-filter per atlas.
    QVector<SpratProfile> m_allProfiles;

    void updatePreviewProfileCombo(const QString& preferredProfile = {});

    // Export log panel
    QGroupBox*   m_logGroup     = nullptr;
    QTreeWidget* m_logTree      = nullptr;
    QPushButton* m_logRevealBtn = nullptr;
    QString      m_logDestination;

    // Saved viewport zoom (persisted across enter/leave cycles)
    double m_savedZoom = -1.0;
};
