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
 *
 * Uses a set-based approach so that a generated suffix (e.g. "walk_2")
 * is guaranteed not to clash with an already-present name bearing that
 * same suffix naturally (e.g. a file literally named "walk_2.png").
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

    // First pass: record all names that appear more than once so we only
    // touch true duplicates and leave unique names alone.
    QSet<QString> usedNames;
    for (auto& s : all) {
        if (usedNames.contains(s->name)) {
            // Collision detected — find the next free suffix.
            int suffix = 2;
            QString candidate;
            do {
                candidate = s->name + QStringLiteral("_%1").arg(suffix++);
            } while (usedNames.contains(candidate));
            s->name = candidate;
        }
        usedNames.insert(s->name);
    }
}
