#pragma once

#include "models.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>

/**
 * @brief Ensures all sprite names across layout models are unique.
 *
 * Strategy:
 *  1. Start with baseName (filename without extension) — already set.
 *  2. For any duplicates, prefix with the parent folder name relative to
 *     sourceFolder (e.g. "player/hurt1" becomes "player_hurt1").
 *  3. If duplicates still remain, append _2, _3, … to the newer entries
 *     (sprites later in the vector are considered newer).
 */
inline void ensureUniqueSpriteNames(QVector<LayoutModel>& models,
                                    const QString& sourceFolder)
{
    // Collect every sprite across all models in order
    QVector<SpritePtr> all;
    for (auto& m : models)
        for (auto& s : m.sprites)
            all.append(s);

    if (all.isEmpty()) return;

    // ------------------------------------------------------------------
    // Pass 1: detect duplicates and prefix with parent folder
    // ------------------------------------------------------------------
    QSet<QString> seen;
    QSet<QString> duplicated;
    for (const auto& s : all) {
        if (seen.contains(s->name))
            duplicated.insert(s->name);
        seen.insert(s->name);
    }

    if (!duplicated.isEmpty()) {
        for (auto& s : all) {
            if (!duplicated.contains(s->name))
                continue;
            // Determine relative parent folder
            QString dir = QFileInfo(s->path).absolutePath();
            QString prefix;
            if (!sourceFolder.isEmpty() && dir.startsWith(sourceFolder)) {
                prefix = dir.mid(sourceFolder.size());
                if (prefix.startsWith('/')) prefix = prefix.mid(1);
            }
            if (prefix.isEmpty())
                prefix = QFileInfo(s->path).dir().dirName();

            if (!prefix.isEmpty())
                s->name = prefix.replace('/', '_') + QStringLiteral("_") + s->name;
        }
    }

    // ------------------------------------------------------------------
    // Pass 2: if still duplicated, append _2, _3, …
    // ------------------------------------------------------------------
    QHash<QString, int> counts;
    for (auto& s : all) {
        int& n = counts[s->name];
        ++n;
        if (n > 1)
            s->name = s->name + QStringLiteral("_%1").arg(n);
    }
}
