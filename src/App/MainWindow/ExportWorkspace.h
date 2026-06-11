#pragma once
#include <QWidget>
#include "models.h"
#include "SpratProfilesConfig.h"

class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class IAtlasViewport;
class ZoomableGraphicsView;

class ExportWorkspace : public QWidget {
    Q_OBJECT
public:
    explicit ExportWorkspace(QWidget* parent = nullptr);

    void populate(const QVector<SpratProfile>& profiles,
                  const QString& selectedProfileName,
                  const SaveConfig& lastConfig,
                  const QString& startDir);

    SaveConfig getConfig() const;

    void setViewport(IAtlasViewport* viewport);
    void clearViewport();

    /** Populate the preview atlas combo with atlas names.
     *  @param names          Display names (one per non-excluded atlas).
     *  @param activeSessionIndex  Real session atlas index to pre-select.
     *  @param sessionIndices Parallel list of real session indices stored as combo item data,
     *                        so previewAtlasChanged carries the true session index.
     */
    void setAtlasNames(const QStringList& names, int activeSessionIndex,
                       const QList<int>& sessionIndices);

    QDoubleSpinBox* zoomSpin() const { return m_zoomSpin; }

signals:
    void exportRequested(SaveConfig config);
    void cancelled();
    void previewSettingsChanged(QString profileName, QString scaleFilter);
    /** Emitted when the user selects a different preview atlas. */
    void previewAtlasChanged(int atlasIndex);

private slots:
    void onZoomSpinChanged(double value);
    void onViewZoomChanged(double zoom);

private:
    void setupUi();
    void onExportClicked();
    void setPreviewWidget(QWidget* preview);
    void clearPreviewWidget();

    QWidget*             m_previewPane      = nullptr;
    QLineEdit*           m_outputPathEdit   = nullptr;
    QComboBox*           m_transformCombo   = nullptr;
    QComboBox*           m_scaleFilterCombo = nullptr;
    QComboBox*           m_profileCombo       = nullptr;
    QComboBox*           m_previewAtlasCombo  = nullptr;
    QDoubleSpinBox*      m_zoomSpin         = nullptr;
    QPushButton*         m_exportBtn        = nullptr;
    ZoomableGraphicsView* m_currentView     = nullptr;
};
