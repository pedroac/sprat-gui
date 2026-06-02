# Changelog

All notable changes to sprat-gui are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Sources dialog** redesigned with per-source "Sync Layout to Source" and "Sync Source to Layout" buttons
- **Sync Layout to Source**: writes current layout sprites back to the original source file — repacks archives (ZIP), re-renders PNG atlases, and re-exports GIFs from the current pivot/marker state
- **Sync Source to Layout**: re-imports any changes from the original source; disabled when no cached copy exists
- Pre-sync diff summary in the confirmation dialog showing how many files will be added, updated, or deleted
- Source-not-found recovery: if the original file is missing, the user is offered to recreate it from the cached copy, embed live sprites from the layout, pick a new location from the filesystem, or remove the source
- Missing-file detection: if individual files are absent inside an existing source, the user is offered to restore them from the project cache or locate a replacement folder
- `ArchiveExtractor::listEntries` utility for enumerating archive contents without extraction
- Enable/disable logic for sync buttons based on source type and cached copy availability (URL sources always disabled)

## [0.7.0]

### Added
- Onion skin preview in the animation test panel
- Frames editor settings (configurable overlay and display options)

### Changed
- Full undo/redo now covers layout changes, marker edits, and project-level state (not just pivots)
- Timeline tree interactions overhauled with drag-reorder, inline rename, and keyboard navigation
- UI polish: replaced redundant text labels with icons throughout toolbars and dialogs

## [0.6.0]

### Added
- WASM: sprite folder browser allows selecting image folders in the browser environment
- Timeline undo/redo support

### Changed
- Enhanced WASM file management with improved upload/download flows
- Optimized background file scanning for large folders

### Fixed
- Default sort order corrected for newly loaded sprite folders

## [0.5.0]

### Added
- Undo/redo for pivot and marker edits (`Ctrl+Z` / `Ctrl+Y`)
- Portable ZIP projects now embed all source images
- Profile presets: save and restore full layout-profile configurations
- Help menu with Quick Start guide and Hotkeys reference
- Keyboard shortcuts for sprite deletion (`Delete`) and atlas view switching (`Alt+L` / `Alt+N`)

### Changed
- Animation panel improvements: frame reordering via drag-and-drop, better FPS controls
- File loading UX overhauled; layout rebuild debounced to reduce redundant re-packs
- Navigator improvements including context actions for grouping, timeline creation, and folder operations

## [0.4.0]

### Added
- Watch mode: live file-system monitoring auto-resyncs the project when source images change
- Split sprites: right-click a sprite in the layout canvas to divide it into sub-frames
- Sprite Navigator: hierarchical tree view of all sprites organised by folder (toggle with `Alt+N`)
- CLI Log dock for inspecting raw CLI tool output
- Remote import: load sprite folders or ZIP files from URLs directly into the project
- Granular settings sections (Spritesheet, Styles, CLI Tools, …)

### Changed
- Dock layout reorganised into Atlas, Animation, and Debug groups
- Lazy layout rebuild with immediate visual feedback; full repack deferred 2 seconds after last change
- Rendering, hit-testing, and lookup hot paths optimised for large atlases

## [0.3.2]

### Fixed
- Upgraded libarchive to v3.8.5 (security and stability)
- Resolved "Unsupported ZIP compression method" error on Windows by ensuring zlib availability
- Fixed wide-character pathname handling for libarchive on Windows

## [0.3.0]

### Added
- Per-profile GPU texture compression: DXT1 (RGB) and DXT5 (RGBA) output as `.dds`
- Per-profile pixel dilation (0–16 passes) to reduce dark halos around trimmed sprites
- Deduplication mode selection: Exact (byte-for-byte) or Perceptual (visually similar)

### Changed
- Settings dialog reorganised into logical groups (Spritesheet, Styles, CLI Tools)

## [0.2.5]

### Added
- CLI tool integration stability improvements with robust version parsing
- Version persistence to QSettings for faster startup heuristic
- Manual CLI path selection dialog (ProvidePath feature)
- Real-time installation progress feedback via installLog signal connection
- itch.io web demo deployment

### Fixed
- Fragile CLI version string parsing using `output.split()`
- Prevent CLI operations from executing against old binaries during upgrade
- Handle edge cases with whitespace, newlines, and format variants in version strings
- WASM Asyncify resize assertion error resolved by disabling strict checks

### Changed
- m_cliReady heuristic now gates on both binary presence AND version match

## [0.2.4] - 2026-04-15

### Added
- Windows build improvements with proper DLL deployment via windeployqt
- Enhanced CLI tool configuration and installation UI

### Fixed
- Windows-specific path and binary naming issues
- LibArchive dependency resolution for Windows builds

### Changed
- Updated build scripts for better cross-platform compatibility

## [0.2.3] - 2026-03-20

### Added
- WebAssembly (WASM) support for browser-based deployment
- Web demo at https://sprat-gui.itch.io

### Fixed
- WASM Asyncify stability issues and resize assertion errors
- File operation feedback for WASM builds

### Changed
- Improved WASM-specific build configuration
- Enhanced file dialog handling for browser environment

## [0.2.2] - 2026-02-10

### Added
- Support for macOS bundle creation and deployment
- Cross-platform binary distribution preparation

### Fixed
- CMake configuration for macOS application bundles
- Qt6 dependency discovery across platforms

### Changed
- Improved build script organization (separate Linux/Windows/WASM)

## [0.2.1] - 2026-01-25

### Added
- Comprehensive CI/build infrastructure improvements
- Windows batch build script (build_windows.bat)
- Linux shell build script (build.sh)

### Fixed
- Missing QCoreApplication includes in build files
- Windows DLL dependency resolution (R6025 errors)
- ZIP extraction compatibility on Windows

### Changed
- Upgraded LibArchive to v3.8.5 for security and stability
- Updated build configuration for better dependency management

## [0.2.0] - 2025-12-01

### Added
- Complete Qt6 migration from Qt5
- Concurrent task execution for responsive UI
- Autosave functionality for project recovery
- Multi-selection support in layout canvas
- Arrow key navigation in sprite grid
- Frame list filtering by name

### Fixed
- Resource loading in QML/Qt6 context
- Settings persistence across application restarts
- Timeline synchronization with layout changes

### Changed
- Major refactor to Qt6 API
- Improved animation preview responsiveness
- Enhanced keyboard/mouse control handling

## [0.1.0] - 2025-08-15

### Added
- Initial release of sprat-gui
- GUI frontend for sprat-cli sprite sheet generation
- Frame detection from single images (spratframes integration)
- Sprite sheet layout generation from image folders (spratlayout integration)
- Visual sprite editing with pivots and markers
- Timeline authoring with automatic generation from frame names
- Animation preview and playback controls
- Animation export to GIF (ImageMagick) and video (FFmpeg)
- Project save/load in JSON and ZIP formats
- Settings dialog for customization (colors, borders, styles)
- CLI tool management and installation UI
- Cross-platform support (Linux, macOS, Windows)
- Comprehensive keyboard shortcuts and pointer controls
- Drag-and-drop support for files and folders
- Frame-level undo/redo operations
- Test suite with unit tests

### Features

#### Sprite Sheet Management
- Automatic frame detection using spratframes
- Manual frame splitting with visual interface
- Batch frame operations and management
- Frame search/filter functionality

#### Layout Editing
- Visual pivot point editing per sprite
- Multiple marker types (point, circle, rectangle, polygon)
- Marker info clipboard copy
- Alignment tools and quick operations
- Multi-frame operations (apply pivot/marker to selection)

#### Animation System
- Manual and automatic timeline creation
- Timeline generation from frame naming patterns
- Frame-by-frame animation preview
- FPS and zoom controls
- Animation export with progress feedback

#### Project Management
- JSON format for human-readable project files
- ZIP format for portable projects with assets
- Autosave with recovery on crash
- Selective data persistence options

#### Developer Experience
- CLI tool auto-detection and installation
- Configurable tool paths via Settings
- Comprehensive error messages
- Built-in troubleshooting guide
- Support for local sprat-cli development builds

## Notes

### Breaking Changes
- Qt5 → Qt6 migration in v0.2.0 (requires Qt6 runtime)
- WASM deployment adds additional build target requirements

### Known Limitations
- ImageMagick and FFmpeg optional for animation export
- ZIP support requires system zip/unzip utilities (usually pre-installed)
- Frame detection works best with consistent sprite dimensions

### Future Considerations
- Platform-specific installers (.deb, .rpm, .msi)
- Signed/notarized macOS bundles
- Built-in update checker and auto-update mechanism
- Additional animation format support
- Plugin system for custom tools integration

---

For migration guides and detailed release notes, see the [README.md](README.md) and [GitHub Releases](https://github.com/pedroac/sprat-gui/releases).
