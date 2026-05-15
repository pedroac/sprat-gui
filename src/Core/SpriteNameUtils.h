#pragma once

#include "models.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>

/**
 * @brief Ensures all sprite names across layout models are unique.
 *
 * Names now include folder paths (e.g. "player/walk1") and are naturally
 * unique across directories. For any remaining true duplicates, append
 * _2, _3, … to the newer entries (sprites later in the vector are
 * considered newer).
 */
inline void ensureUniqueSpriteNames(QVector<LayoutModel>& models,
                                    const QString& sourceFolder)
{
    Q_UNUSED(sourceFolder)

    // Collect every sprite across all models in order
    QVector<SpritePtr> all;
    for (auto& m : models)
        for (auto& s : m.sprites)
            all.append(s);

    if (all.isEmpty()) return;

    // Append _2, _3, … for any duplicate names
    QHash<QString, int> counts;
    for (auto& s : all) {
        int& n = counts[s->name];
        ++n;
        if (n > 1)
            s->name = s->name + QStringLiteral("_%1").arg(n);
    }
}
