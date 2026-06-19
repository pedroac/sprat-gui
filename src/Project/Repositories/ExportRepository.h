#pragma once
#include <QObject>
#include <QVector>
#include "ProjectModels.h"
#include "ExportModels.h"

/**
 * @class ExportRepository
 * @brief Owns named export presets and the last-used save configuration.
 */
class ExportRepository : public QObject {
    Q_OBJECT
public:
    explicit ExportRepository(QObject* parent = nullptr);

    const QVector<ExportPreset>& exportPresets()   const;
    const SaveConfig&            lastSaveConfig()  const;

    void setExportPresets(const QVector<ExportPreset>& presets);

    /** Appends or replaces a preset by name. */
    void addExportPreset(const ExportPreset& preset);
    void removeExportPreset(const QString& name);
    void setLastSaveConfig(const SaveConfig& config);

    void clear();

signals:
    void exportPresetsChanged();
    void lastSaveConfigChanged(SaveConfig config);

private:
    QVector<ExportPreset> m_exportPresets;
    SaveConfig            m_lastSaveConfig;
};
