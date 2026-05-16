#pragma once

/**
 * @namespace AppConstants
 * @brief Centralized application configuration constants.
 *
 * All magic numbers, timeout intervals, and configurable limits are defined here
 * for easy discovery and modification.
 */
namespace AppConstants {

// ============================================================================
// Timer Intervals (milliseconds)
// ============================================================================

/// Autosave interval: 5 minutes
constexpr int kAutosaveIntervalMs = 300000;

/// Layout debounce interval: prevents excessive layout rebuilds on rapid changes
constexpr int kLayoutDebounceMs = 2000;

/// Watch mode periodic check interval
constexpr int kWatchModeCheckMs = 2000;

/// Source resolution combo box debounce
constexpr int kSourceResDebounceMs = 350;

/// Main window and canvas resize debounce
constexpr int kResizeDebounceMs = 200;

/// Folder watcher debounce interval
constexpr int kFolderWatchDebounceMs = 500;

/// Layout process (spratlayout) timeout: 5 minutes
constexpr int kLayoutProcessTimeoutMs = 300000;

/// CLI tools startup delay (allows for initialization)
constexpr int kCliStartupDelayMs = 1000;

// ============================================================================
// UI Limits and Thresholds
// ============================================================================

/// Maximum number of undo/redo stack items
constexpr int kUndoStackLimit = 50;

/// Maximum number of recent projects to keep in menu
constexpr int kRecentProjectsMax = 5;

/// Maximum number of blocks in CLI output log before pruning
constexpr int kCliLogMaxBlocks = 5000;

/// Threshold for layout change buffer before forcing immediate rebuild
/// (prevents excessive debouncing when many rapid changes accumulate)
constexpr int kLayoutBufferFullThreshold = 20;

}  // namespace AppConstants
