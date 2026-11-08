# Changelog

All notable changes to sprat-gui are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Puppet workspace: the part panel groups how a part is placed under **Rig** and what the pose does to it under **Transformation** (now with **Shows**), each with a **Lock**. Rig's keeps the placement from changing in every pose; Transformation's keeps the part as it is in that pose only ("In pose ..."), on the canvas too, and a pull bends only the joints below a locked part. Locks are saved in the project, not exported to sprat-cli
- Puppet workspace: a **Grid** button above the canvas shows a grid from the puppet's origin, using the grid's cell size, offset and colour from Settings, as the Sprites and Nine-Slice workspaces do
- Puppet workspace (`Alt+P`), for sprat-cli puppets (cutout rigs) of an atlas. Add puppets, parts and the sprites each part can show; a New Puppet window names the puppet and builds its parts by dragging sprites onto them (several sprites dropped on empty space make one part, named after the folder or the sprites), with a menu to rename or delete parts and exclude sprites; parts are listed in draw order and dragged to reorder them. The canvas draws the puppet at rest, with crosses at its origin and at the selected part's pivot; drag a part, or use the arrow keys, to move it; the view stays where it is, so changes are easy to compare, and Fit zooms to the whole puppet. Drag a part's pivot onto another part's marker to attach it there, or set the attachment, leeway and turn limits in the right panel. Turn any part by its round handle or the Angle field (an attached part within its limits, a positioned one around its pivot), drag an attached part to pull it: its joint slides within its leeway, then the parts it hangs from turn (moving a hand bends the arm); every part's markers are shown on the canvas, and a part's marker list chooses which ones are shown and offered for joints; or Shift+drag it to move its joint within its leeway; this isn't saved, and Reset puts the puppet back at rest. The right panel also edits names and positions. Every change can be undone. Poses: a strip under the canvas shows Rest (the rig) and every pose as thumbnails; select a pose and turn, pull or move parts to edit it, add, duplicate, rename, reorder or remove poses; Rest is a pose too (`rest`, exported with the others and playable in animations) that starts out setting every part as the rig has it and can't be renamed, moved or deleted; the variant a part shows in it is its rest variant; double-click a variant in the tree, or right-click it for Show in Pose, to have the selected pose show it; a part's variants are listed by name; and tick the parts a pose sets in a list under the puppet tree (the others are faint), so poses combine; the part panel picks the variant a part shows. Animations: an Animations tab lists the puppet's animations (sprat-cli sequences); each is a sequence of poses added from its context menu, each reached in some frames and held for some, linear or snapping, at its fps and loop mode, played on the canvas with a playhead to scrub, and other animations can play along to preview how they combine. Animations have frames of their own: right-click a step for **New Frame Before**/**After** (a copy of that step's pose, kept with the animation and hidden from the Poses tab), and edit any step on the canvas when stopped where its pose has arrived. A step playing a pose from the Poses tab gets its own copy when you change it, so editing one animation never changes another or the shared pose; **Save as Pose...** copies a frame to the Poses tab, and **Onion skin** shows the steps before (red) and after (green) faintly. Renaming, duplicating or deleting an animation does the same to its frames. Puppets are saved in the project and exported for sprat-cli's `raw` format and as `puppets.txt` in Layout text exports
- `deploy/build-from-source.sh` builds sprat-cli and sprat-gui from source on Linux and macOS: it checks Git and the build dependencies (printing the install command for what's missing), clones or updates both repositories in a `sprat` folder, builds the sprat-cli version sprat-gui needs, and sets sprat-gui up to use the tools. Run it again to update
- Frame Animation workspace: each timeline has a loop mode (Loop, Once, Ping-pong or Ping-pong once). The preview, GIF/video export and exported metadata play it that way; an alias plays the way its source does. Projects saved before keep looping

### Changed
- Puppet workspace: the canvas no longer draws lines from each part's parent to the point it hangs from, which crossed the sprites and pointed at new markers for no reason; the panel and the tree say what hangs from what
- Puppet workspace: in the animation preview, parts showing an animation (a talking mouth) play its frames at its own speed and loop mode, instead of holding its first frame
- Puppet workspace: drop a part's pivot ring on another part, away from its markers, to hang it there from a new point marker (named after the part), or pick **New marker at its pivot** in the panel, or put the part in place and double-click its pivot ring to hang it there from the part under its pivot (a menu asks which when several parts, or none, are under it). The marker is added to every sprite the parent shows, at the same distance from each sprite's pivot, so markers no longer have to be placed beforehand in the Sprites workspace; `Shift` still places the part by position. One undo takes back the markers and the attachment
- Puppet workspace: a part can ease its own way in an animation step (the eyes snap while the arm moves): select the step and the part, and set **Ease** in the right panel's new **Animation** group. The part's position and angle, which the selected pose sets, are grouped under **Transformation**. Saved in the project and written as sprat-cli's `step:`/`linear:` step settings, which need a sprat-cli release that reads them
- Puppet workspace: reordering parts is easier. Dragging a part lifts its row out and opens a gap where it will land, and it no longer has to be dropped exactly on a row (anywhere over the puppet's rows works, and below the last row puts it last). Right-click a part, in the tree or on the canvas, for **Bring to Front** and **Send to Back** as well as **Bring Forward** and **Send Backward**
- Importing many sprites is faster and no longer freezes the window: sprite list thumbnails load in the background (shared by every list, so pickers open at once), and adding sprites to the atlas no longer slows down quadratically (2,500 sprites: 2.9 s → 1.5 s to the laid-out canvas)
- Canvases say what to do when they have nothing to show (Sprites: select a sprite; Nine-Slice: select or add sprites; Puppets: select a puppet), and Nine-Slice's W/H are off without a sprite
- Puppet workspace: the canvas uses the workspace colour like the other canvases instead of white; the tree's names line up whether or not they are starred, and its second column is "Details"; animation frames are named `walk frame 1` so they don't read as animations; parts aren't faded while editing an animation frame, so the onion skin stands out; the Animations list says its ticks play along
- The status bar shows how many sprites are loaded ("63 sprite(s)") instead of "Frames: 63 (manual selection)"
- Frame Animation: the add-timeline button is labelled **Add**
- Help › Quick Start describes the app as it is now: the File menu's Add Source actions, every workspace including Puppets, saving project folders and the Exportation workspace without its Cancel button. Help › Hotkeys lists the Puppets workspace's keys, and the README is brought up to date (the Sprites and Atlases workspaces, sources, shortcuts that no longer existed)
- Sprites workspace: right-click a source for **Sync Source to Layout** and **Sync Layout to Source...**, which had no way to be reached
- About credits the icons as adapted with AI from the SVG Repo icons used before
- New icons, drawn in one style (thin rounded outlines) that follows light and dark themes, replacing a mix of SVGRepo icons and the platform's built-in ones: each action has its own (Exclude and Delete, Save, Save As and Export, adding a folder, an image or a URL no longer share icons), menus that had none have one (Open ZIP Project, the Add Source button's menu), and message dialogs use matching information, question, warning and error icons
- Messages are easier to read: each idea is its own paragraph (what went wrong, what sprat-cli said, what to do), and what sprat-cli printed is shown in a fixed-width block, cut when it is long. Layout errors during export now include spratlayout's own message. Messages that used internal terms ("temporary frame list", "No session.", "virtual animation group") say what happened and what to do instead
- The status bar says when the project was autosaved ("Autosaved project at 07/11/2026 14:32", in your locale's format), and when a restored autosave was made
- Exportation: the **Cancel** button is gone; the workspace tabs switch away
- Frame Animation workspace: the sprite column is gone; the timeline list sits on the left and the selected timeline with its preview on the right. With several atlases, an **Atlas** selector above the timelines picks whose timelines are shown
- Frame Animation workspace: **Add timeline** opens a sprite picker (as in the Nine-Slice workspace); tick sprites or whole groups, or create it empty. Right-click the timeline list for **Auto-create Timelines...**: pick one or more groups (or a source), and each group holding sprites becomes a timeline named after it, inside a folder named after its source and parent groups. Right-click a frame for **Add Frames Before...** / **Add Frames After...**, or empty space in the frames for **Add Frames...**
- Sprites picked or dropped into a timeline are ordered by their numbers (`walk2` before `walk10`), not alphabetically
- Nine-Slice workspace: the sprite list shows only nine-sliced sprites. **Add** (under the list, as in the Atlases workspace) opens a sprite picker where you tick several sprites, or a whole folder, at once; **Remove** (or Delete) stops nine-slicing the selected ones and keeps their insets for later. This replaces the "Nine-sliced only" filter and the "Nine-Sliced" toggle
- Workspaces now have tabs at the top of the window (Atlases, Sprites, Nine-Slice, Frame Animation, Exportation), so the current workspace is always shown and one click switches. They replace the Workspace menu; its shortcuts (Alt+A, Alt+S, Alt+N, Alt+F, Alt+X) still work
- The Export workspace shortcut is now Alt+X. Alt+E is the Edit menu's key on Windows and Linux, and the clash made Alt+E do nothing
- File > Load is now File > Open Project (Ctrl+O). It asks for the project folder, the same folder Save As creates, instead of the project.spart.json inside it. ZIP projects open with File > Open ZIP Project
- File > Export has a shortcut: Ctrl+E
- Help > Hotkeys now lists the shortcuts as they are bound, in the platform's notation (⌘ on macOS), including marker copy/paste and nudging with the arrow keys
- When the layout switches to another profile because the selected one failed, a notice says which profile is now used (exports use it too)
- Layout and sprat-cli errors say what went wrong and where to fix it, instead of "Error", and open over the app window
- Frame Animation workspace: onion skin now defaults to off
- Sprites workspace: trim rect now defaults to on
- Atlas Sprites: deduplication mode now defaults to Exact instead of None
- Frame Animation workspace: animations can now run at up to 100 FPS (was 60). Projects with faster animations load capped at 100 FPS, the fastest speed a GIF export can show
- Export: the "Raw (sprat-cli format)" format is now called "Layout text (sprat-cli files)", so it can't be confused with sprat-cli's new `raw` format. Saved projects keep their choice

### Security
- Opening a sprite archive can no longer overwrite files outside the folder it is extracted to. Links inside archives (which a `.tar` could use to write to any of your files) are now skipped
- Exporting can no longer delete folders outside the export folder. A project file could name a profile or atlas output folder with an absolute path or `..`, and the export cleared that folder before writing. Such names are now dropped when the project is opened, and the export refuses them
- A post-export command stored in a project now asks before it runs for the first time, so a project from someone else can't run commands on your computer unnoticed. Commands you type in the Export workspace run without asking
- In-app updates are checked against the SHA-256 checksum GitHub publishes for each release file before they are installed

### Fixed
- Puppet workspace: an animation playing in the preview follows changes at once: a new fps changes its speed (it kept the old one until played again), and removing its last step stops it
- Sprites workspace: ticking or unticking sprites in the sprite tree updates the onion skin at once; it used to show the old ones until Onion was turned off and on again
- Messages lost their paragraph breaks and ran together as one block, and text in angle brackets from sprat-cli (such as `<stdin>`) disappeared from them
- Switching to the Sprites or Frame Animation workspace no longer makes the window taller (it could grow past a laptop screen and hide the status bar)
- Sprites: clicking empty space on the sprite preview now moves the selected marker there (centred on the click) instead of moving the pivot and deselecting the marker; clicking the pivot's crosshair selects the pivot without moving it
- Sprites: a marker created in the Markers dialog is selected when the dialog closes, so the next click on the preview positions it
- Projects with several atlases keep the pivots, markers, names and nine-slice insets of sprites in atlases other than the selected one. They used to be lost on save, and when switching to another atlas after opening a project
- Exporting several atlases writes each atlas's own markers and animations. The first atlas used to get the selected atlas's animations, and the others got no markers or animations
- Save (Ctrl+S) on a project opened from a ZIP updates the ZIP, instead of writing a project file and `sprites/` folder next to it
- Restoring an autosave of an open project finds its sprites and excluded sprites again
- Undoing "Exclude from Layout" puts the sprites back in their atlas and takes them off the excluded list; redo excludes them again
- After importing an archive and removing or excluding sprites, the window title no longer shows "/" as the folder
- Excluded sprites are kept in the project: saving copies their images into the project folder, and they stay listed (and can be included again) after the project is moved, renamed or opened from a ZIP. Before, their images were left behind and they couldn't be brought back
- Sync, and the periodic folder check, no longer add excluded sprites back into the layout
- A project opened from a ZIP no longer loses its sprites after the first layout (they were deleted from the temporary folder it was extracted to)
- Save (Ctrl+S) copies newly added images into the project before writing the project file, so the file never points at their temporary locations
- Opening a ZIP project failed with "Invalid image path" (or laid out nothing): its sprites were looked for next to the ZIP instead of inside it
- Opening a project while on the Nine-Slice, Atlases or Exportation tab put the Sprites panel over that workspace
- The window title and File > Open Recent name a project after its folder (or ZIP) and show where it is, instead of the parent folder, `project.spart.json`, or a temporary folder
- After opening a project, the status bar keeps "Loaded N sprites" instead of switching to "CLI ready"
- Opening another project (Open Project, Open Recent, or dropping a project) now asks to save unsaved changes first, instead of discarding them. Closing the app no longer closes it when saving was canceled or failed
- Ctrl+V in the sprite preview pastes copied markers again, instead of importing from the clipboard
- Frame detection no longer freezes its dialog while detecting; it shows progress, keeps the frames when detection fails and says why, and settings changed in a canceled dialog are no longer kept
- Zoom shortcuts (Ctrl+plus/minus, Ctrl+0, Ctrl+1) work on the current workspace's view even when the focus is in a side panel or on a button
- When no profile can fit the sprites, the layout stops with one message instead of also trying to switch to an empty profile
- Removing a source no longer deletes its files permanently when the system temp folder is on another drive (common on Linux), so undoing the removal brings them back
- In-app updates: on macOS the new app replaces the old one instead of being copied over it (leftover files could make macOS report the app as damaged); on Linux the bundled profiles config and transforms are updated too, and running from an AppImage updates the AppImage itself. The update now waits for the app to close instead of a fixed 2 seconds
- Windows: installing sprat-cli from the app no longer fails because the downloaded ZIP was saved without a `.zip` extension
- Linux: installing sprat-cli from the app no longer runs one compile job per CPU core, which could run out of memory on machines with little RAM. It now runs one job per 2 GiB of free memory
- Writing files back to a source ZIP no longer leaves a broken ZIP when a file can't be read or written; the original ZIP is only replaced once the new one is complete
- Opening a project ZIP whose project file can't be fully read now shows an error instead of loading a cut-off project, and archive warnings no longer stop extraction partway while reporting success
- Atlas names with slashes or `..` no longer turn into export folders outside the export
- macOS: the app no longer shows as "damaged" and refuses to open (seen on macOS 27 beta). The bundle is now re-signed after packaging, and the bundled sprat-cli tools, profiles config and transforms moved from `Contents/MacOS` to `Contents/Resources/cli`, since non-code files in `Contents/MacOS` made the bundle unsignable
- macOS: installing sprat-cli from the app now puts it where the app looks for it, and copies the profiles config and transforms correctly
- macOS: the app now runs on Intel Macs and on macOS 11 and later. It is a universal (Apple Silicon and Intel) build, and its bundled sprat-cli tools no longer need Rosetta or Homebrew libraries
- Upgrading sprat-cli from the app no longer asks again on every start when Settings points at another CLI tools folder (as `build-from-source.sh` sets up). After installing, the app now uses the tools it just installed
- The app no longer crashes when quitting after sprites were loaded (seen on macOS)
- Export: LibGDX exports with sprite rotation enabled no longer fail; the layout now rotates sprites counter-clockwise, as libGDX expects
- Export: profiles with Zopfli enabled no longer fail when the sprat-cli build lacks Zopfli; the atlas is saved as a regular PNG and the export log says so
- Windows: loading and exporting sprites from folders whose names contain non-ASCII characters (e.g. `spritës`, `雪`) no longer fails (requires Windows 10 version 1903 or later)
- Linux: installing sprat-cli from the app no longer replaces the installed tools with a different version when a `sprat-cli` checkout of another version sits next to the working folder; that checkout is only used when its version matches, and the built tools are checked before anything is installed
- Export: with perceptual deduplication, the export now uses the similarity threshold from Settings, like the layout canvas, instead of always the default
- Export: the preview now shows the atlas at the profile's target resolution, like the exported file
- Export: the list of files an export will create or overwrite now names the Unity metadata files correctly (`json.json`, `spritesheet.png.meta`)
- Updates: the update prompt no longer offers an older release (e.g. v0.11.0 to a v0.12.0 build); it only appears when a newer version is published
- Nine-Slice workspace: insets can no longer add up to more than the sprite (left + right within its width, top + bottom within its height), and the preview no longer clamps them to half the sprite. Projects saved with insets that don't fit get the right and bottom insets reduced when the sprite is opened or exported, instead of failing to export
- Export: nine-slice insets set in the Nine-Slice workspace are now written to the export
- Nine-Slice workspace: opening it no longer resets the first sprite's insets, fill modes and size
- Edits in the Nine-Slice workspace now count as unsaved changes: the app asks to save them before closing, and saving clears the unsaved marker in the title bar
- Export: projects with alias timelines no longer fail to export ("Invalid alias line"); aliases and their flip are now written the way sprat-cli reads them
- Export: sprite, marker, alias and animation names containing quotes or line breaks no longer break the export. Unknown marker kinds in a project file are skipped, and unknown nine-slice fill modes fall back to stretch, instead of being passed to sprat-cli

### Dependencies
- Updated sprat-cli to v0.32.4, which describes puppets (rigs, poses and pose sequences) and writes raw format version 8
- Updated sprat-cli to v0.28.1, which labels layout values, reports mistakes in hand-written markers and animations files, and writes raw format version 4
- Updated sprat-cli to v0.24.0, which adds animation loop modes and rejects animations faster than 100 FPS
- macOS: the bundled sprat-cli tools are built from source with the app instead of being taken from sprat-cli's macOS release

## [0.8.0] - 2026-06-15

### Added
- **Sources dialog** redesigned with per-source "Sync Layout to Source" and "Sync Source to Layout" buttons
- **Sync Layout to Source**: writes current layout sprites back to the original source file — repacks archives (ZIP), re-renders PNG atlases, and re-exports GIFs from the current pivot/marker state
- **Sync Source to Layout**: re-imports any changes from the original source; disabled when no cached copy exists
- Pre-sync diff summary in the confirmation dialog showing how many files will be added, updated, or deleted
- Source-not-found recovery: if the original file is missing, the user is offered to recreate it from the cached copy, embed live sprites from the layout, pick a new location from the filesystem, or remove the source
- Missing-file detection: if individual files are absent inside an existing source, the user is offered to restore them from the project cache or locate a replacement folder
- `ArchiveExtractor::listEntries` utility for enumerating archive contents without extraction
- Enable/disable logic for sync buttons based on source type and cached copy availability (URL sources always disabled)
- ProfilesDialog: real-time duplicate name detection highlights the Name field with a red border before accepting
- MarkersDialog: tooltips on all coordinate, radius, and dimension spinboxes now show the field's purpose and valid range

### Changed
- CLI execution error dialog now shows the binary path and platform-specific diagnostic steps (`ldd` on Linux, `xattr` Gatekeeper removal on macOS, DLL guidance on Windows)
- ExportDiffDialog minimum size increased to 600×400 (default 660×460); a summary line ("New: X · Will overwrite: Y") now appears above the file tree
- Export workspace preview pane shows "Generating preview…" while atlas packing is in progress instead of the static "No preview available" placeholder
- Multi-selection indicator in the sprite editor now appears in both propagation modes: "changes apply to all" when propagation is on, "changes apply to current frame only" when off
- CLI install overlay switches to an indeterminate progress bar and shows "Building CLI tools…" once the download reaches 100%, so users see that extraction/compilation is ongoing
- Animation export missing-tools warning now includes platform-specific install commands (apt/dnf/brew/winget) instead of a generic message

### Fixed
- Export menu "Export" action now shows the overwrite confirmation dialog (ExportDiffDialog) before writing, matching the behaviour of the Exportation workspace button
- Cancel button on the loading overlay now reports "Installation canceled" or "Layout canceled" instead of the generic "Operation canceled"
- Silent failure when copying sprite files to the project folder during save: errors are now logged with source and destination paths

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
