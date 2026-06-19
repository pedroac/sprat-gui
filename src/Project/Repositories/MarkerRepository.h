#pragma once
#include <QObject>
#include <QVector>
#include "MarkerModels.h"

/**
 * @class MarkerRepository
 * @brief Owns marker templates and the transient marker clipboard.
 */
class MarkerRepository : public QObject {
    Q_OBJECT
public:
    explicit MarkerRepository(QObject* parent = nullptr);

    const QVector<MarkerTemplate>& markerTemplates()  const;
    const QVector<NamedPoint>&     markerClipboard()  const;

    void setMarkerTemplates(const QVector<MarkerTemplate>& templates);

    /** Appends or replaces a template by name. */
    void addMarkerTemplate(const MarkerTemplate& tmpl);
    void removeMarkerTemplate(const QString& name);
    void setMarkerClipboard(const QVector<NamedPoint>& points);

    void clear();

signals:
    void markerTemplatesChanged();
    void markerClipboardChanged();

private:
    QVector<MarkerTemplate> m_markerTemplates;
    QVector<NamedPoint>     m_markerClipboard;
};
