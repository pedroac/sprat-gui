#include "ExportRepository.h"
#include <algorithm>

ExportRepository::ExportRepository(QObject* parent) : QObject(parent) {}

const QVector<ExportPreset>& ExportRepository::exportPresets()  const { return m_exportPresets; }
const SaveConfig&            ExportRepository::lastSaveConfig() const { return m_lastSaveConfig; }

void ExportRepository::setExportPresets(const QVector<ExportPreset>& presets) {
    m_exportPresets = presets;
    emit exportPresetsChanged();
}

void ExportRepository::addExportPreset(const ExportPreset& preset) {
    for (auto& p : m_exportPresets) {
        if (p.name == preset.name) {
            p = preset;
            emit exportPresetsChanged();
            return;
        }
    }
    m_exportPresets.append(preset);
    emit exportPresetsChanged();
}

void ExportRepository::removeExportPreset(const QString& name) {
    const int before = m_exportPresets.size();
    m_exportPresets.erase(
        std::remove_if(m_exportPresets.begin(), m_exportPresets.end(),
                       [&name](const ExportPreset& p) { return p.name == name; }),
        m_exportPresets.end());
    if (m_exportPresets.size() != before)
        emit exportPresetsChanged();
}

void ExportRepository::setLastSaveConfig(const SaveConfig& config) {
    m_lastSaveConfig = config;
    emit lastSaveConfigChanged(config);
}

void ExportRepository::clear() {
    m_exportPresets.clear();
    m_lastSaveConfig = SaveConfig{};
    emit exportPresetsChanged();
    emit lastSaveConfigChanged(m_lastSaveConfig);
}
