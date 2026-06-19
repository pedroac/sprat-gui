#pragma once
#include <QString>

/**
 * @enum SyncMode
 * @brief Enum representing folder synchronization modes.
 */
enum class SyncMode {
    None = 0,
    Manual,
    Watch
};

/**
 * @brief Converts SyncMode enum to string representation.
 */
inline QString syncModeToString(SyncMode mode) {
    switch (mode) {
    case SyncMode::None:
        return "none";
    case SyncMode::Manual:
        return "manual";
    case SyncMode::Watch:
        return "watch";
    }
    return "none";
}

/**
 * @brief Converts string representation to SyncMode enum.
 */
inline SyncMode syncModeFromString(const QString& mode) {
    const QString normalized = mode.trimmed().toLower();
    if (normalized == "manual") {
        return SyncMode::Manual;
    }
    if (normalized == "watch") {
        return SyncMode::Watch;
    }
    return SyncMode::None;
}
