#include "MarkerRepository.h"
#include <algorithm>

MarkerRepository::MarkerRepository(QObject* parent) : QObject(parent) {}

const QVector<MarkerTemplate>& MarkerRepository::markerTemplates() const { return m_markerTemplates; }
const QVector<NamedPoint>&     MarkerRepository::markerClipboard()  const { return m_markerClipboard; }

void MarkerRepository::setMarkerTemplates(const QVector<MarkerTemplate>& templates) {
    m_markerTemplates = templates;
    emit markerTemplatesChanged();
}

void MarkerRepository::addMarkerTemplate(const MarkerTemplate& tmpl) {
    for (auto& t : m_markerTemplates) {
        if (t.name == tmpl.name) {
            t = tmpl;
            emit markerTemplatesChanged();
            return;
        }
    }
    m_markerTemplates.append(tmpl);
    emit markerTemplatesChanged();
}

void MarkerRepository::removeMarkerTemplate(const QString& name) {
    const int before = m_markerTemplates.size();
    m_markerTemplates.erase(
        std::remove_if(m_markerTemplates.begin(), m_markerTemplates.end(),
                       [&name](const MarkerTemplate& t) { return t.name == name; }),
        m_markerTemplates.end());
    if (m_markerTemplates.size() != before)
        emit markerTemplatesChanged();
}

void MarkerRepository::setMarkerClipboard(const QVector<NamedPoint>& points) {
    m_markerClipboard = points;
    emit markerClipboardChanged();
}

void MarkerRepository::clear() {
    m_markerTemplates.clear();
    m_markerClipboard.clear();
    emit markerTemplatesChanged();
    emit markerClipboardChanged();
}
