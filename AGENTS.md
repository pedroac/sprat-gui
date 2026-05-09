# Agents Guide: sprat-gui

A comprehensive guide for AI agents and developers working with the sprat-gui project. This document describes the project architecture, key components, their relationships, and how to navigate and extend the codebase.

## Table of Contents

1. [Project Overview](#project-overview)
2. [Quality & Standards](#quality--standards)
3. [Architecture & Design](#architecture--design)
4. [Component Reference](#component-reference)
5. [Integration with sprat-cli](#integration-with-sprat-cli)
6. [UI Architecture](#ui-architecture)
7. [Data Flow](#data-flow)
8. [Common Tasks](#common-tasks)
9. [Development Workflow](#development-workflow)
10. [Deployment & Distribution](#deployment--distribution)
11. [Documentation Standards](#documentation-standards)

---

## Project Overview

**sprat-gui** is a Qt6-based desktop and web application (via WebAssembly) for sprite sheet and animation asset management. It provides visual tools for game developers and UI artists to:

- Detect and extract individual frames from sprite sheets automatically
- Generate optimized sprite layouts from folders of images
- Edit sprite properties (pivots, markers for collision/spawn points)
- Create and test animations with timeline authoring
- Export animations as GIF or video for validation
- Manage complete project state (save/load as JSON or ZIP)

**Version**: v0.3.0 (current development)
**Language**: C++17
**Framework**: Qt6 (Core, Gui, Widgets, Concurrent, Network)
**Dependencies**: LibArchive, ZLIB, sprat-cli (latest version)
**Platforms**: Linux, Windows, macOS, WebAssembly

---

## Quality & Standards

This section outlines the standards and best practices all code must follow to maintain application quality, usability, and reliability.

### Code Quality Standards

#### General Principles
- **Single Responsibility Principle**: Each class/function should have one clear purpose
- **Code Reusability**: Avoid code duplication; extract shared functionality to utilities
- **Readability First**: Code should be self-documenting with clear naming
  - Use descriptive variable/function names: `loadSpritesFromFolder()` not `load()`
  - Avoid cryptic abbreviations: `spriteIndex` not `spIdx`
  - Use meaningful constants instead of magic numbers

#### C++17 Best Practices
- Use `auto` where type is obvious from context
- Prefer smart pointers (`std::shared_ptr`, `std::unique_ptr`) over raw pointers
- Use `const` correctly (const references, const methods)
- Avoid naked new/delete; use RAII
- Use range-based for loops: `for (auto& sprite : sprites)`
- Leverage `std::optional` for nullable values

#### Qt Conventions
- Follow Qt naming conventions consistently:
  - Slots: `on<Signal>()` or `handle<Event>()` pattern
  - Signals: `<property>Changed()`, `<action>Started()`, etc.
  - Private members: `m_` prefix (e.g., `m_projectSession`)
  - Static constants: `CONST_NAME` uppercase
- Use Qt containers (`QVector`, `QMap`, `QString`) for compatibility
- Properly connect/disconnect signals to avoid memory leaks
- Use Q_ASSERT for preconditions (development time)
- Emit signals before state changes complete when appropriate

#### Documentation
- **Header Comments**: Explain what, not how
  ```cpp
  /// Loads sprites from folder and generates layout
  /// @param folderPath Path to folder containing sprite images
  /// @return True if successful, false otherwise
  bool loadSpritesAndLayout(const QString& folderPath);
  ```
- **Inline Comments**: Only for non-obvious logic
- **No Obvious Comments**: Avoid `// increment i` for `i++`
- **Prefer Clear Code**: Good code doesn't need comments

### Testing Requirements

#### Before Committing
1. **Run All Tests**: `ctest --output-on-failure` must pass
2. **Build All Platforms**: Verify builds on Linux, Windows, macOS (or in CI)
3. **Test Your Changes**: Not just the happy path
   - Test error conditions
   - Test boundary cases
   - Test with various input sizes

#### Writing Tests
- Add tests for all new public functionality
- Test critical business logic (sprite operations, animation export, project save/load)
- Use descriptive test names: `testSpriteLoadingWithInvalidPath()` not `test1()`
- Tests should be independent and repeatable
- Mock external dependencies (file system, CLI tools)

#### Test Structure
```cpp
class TestFeatureName : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();      // Run once before tests
    void testSuccessCase();   // Test normal operation
    void testErrorHandling(); // Test error paths
    void cleanupTestCase();   // Run once after tests
};
```

#### Integration Testing
- Test complete workflows (load → edit → save)
- Test CLI tool integration with actual outputs
- Test cross-platform file paths and encoding

### UI/UX Standards

The UI must be **usable, performant, and useful** for game developers and UI artists, not just functional.

#### Usability
- **Clear Visual Hierarchy**: Important actions prominent, secondary actions discoverable
- **Consistent Interaction Model**: Similar operations work the same way everywhere
- **Keyboard Shortcuts**: Essential workflows must have keyboard support
  - `Ctrl+O` - Open project
  - `Ctrl+S` - Save project
  - `Ctrl+Z` - Undo (if applicable)
  - `Delete` - Delete selected sprite/marker
- **Tooltips & Help**: Explain non-obvious features on hover
- **Status Feedback**: User always knows what's happening
  - Progress bars for long operations
  - Status messages for errors/warnings
  - Animation loading spinner during async tasks
- **Undo/Redo**: Major operations should be undoable (if feasible)

#### Performance
- **Canvas Rendering**: Smooth 60 FPS when scrolling/zooming/panning
  - Optimize graphics item rendering
  - Debounce frequent updates
  - Use double-buffering
- **Responsiveness**:
  - Long operations (layout generation, image discovery) must be async
  - Never block the UI thread
  - Show progress and allow cancellation
- **Memory Efficiency**:
  - Don't load entire large images into memory unnecessarily
  - Cache sprites intelligently
  - Clean up temporary resources

#### Visual Design for Game Developers
- **Sprites**: Show actual sprite content (not placeholder boxes)
- **Timeline Preview**: Real-time preview as user builds animations
- **Grid/Snap**: Helpful alignment tools for precise positioning
- **Color Coding**: Different visual states for selected/hovered/edited items
- **Context Menus**: Right-click actions for common operations
- **Viewport Control**: Zoom, pan, fit-to-view for working with various sprite sizes

#### Error Handling
- **Graceful Failures**: Never crash on user input
- **Clear Error Messages**: Explain what went wrong and how to fix it
  - Bad: "Error: JSON parse failed"
  - Good: "Failed to load project: File is not a valid sprat-gui project (corrupted or wrong version)"
- **Recovery Options**: Provide ways to recover or retry
- **User Guidance**: Suggest next steps after errors

### Dependency Management

#### Version Pinning
- **sprat-cli**: Always use the **latest released version**
  - Check GitHub releases: https://github.com/LiquidityC/sprat-cli/releases
  - Update `CMakeLists.txt` when new versions are available
  - Test thoroughly when updating major/minor versions
  - Document breaking changes in CHANGELOG.md

- **Qt6**: Target stable versions (6.8, 6.9, etc.)
  - Avoid pre-release builds for stable releases
  - Test on minimum supported Qt6 version

- **Other Dependencies** (LibArchive, ZLIB):
  - Use system-provided versions when possible
  - Fetch from reliable sources (FetchContent for safety)
  - Document minimum versions in README.md

#### Updating Dependencies
1. Check release notes for breaking changes
2. Update version in `CMakeLists.txt`
3. Run full test suite
4. Test on all platforms (Linux, macOS, Windows)
5. Test WASM build
6. Update documentation if needed
7. Update CHANGELOG.md with version bump

#### Avoiding Dependency Hell
- Keep dependency count low (use what we have)
- Prefer header-only libraries for utilities
- Avoid dependencies with complex transitive dependencies
- Document why each dependency is needed

### Versioning Strategy (Semantic Versioning)

sprat-gui follows **Semantic Versioning 2.0.0** (https://semver.org/)

Format: `MAJOR.MINOR.PATCH`

**MAJOR** (X.0.0): Breaking changes
- User projects from previous version won't load
- Major feature removal
- Example: v1.0.0, v2.0.0

**MINOR** (X.Y.0): New features, backward compatible
- New user-facing features (new export format, new UI element)
- sprat-cli version bump (if adding features)
- Example: v0.3.0 (source folder sync), v0.4.0 (new feature)

**PATCH** (X.Y.Z): Bug fixes, backward compatible
- Bug fixes, performance improvements
- Minor UI tweaks
- Dependency patches
- Example: v0.3.1, v0.3.2

#### Version Update Process
1. Determine version bump based on changes (MAJOR/MINOR/PATCH)
2. Update version in:
   - `CMakeLists.txt` (set VERSION)
   - `src/App/main.cpp` (app version string, if any)
3. Update `CHANGELOG.md` with:
   - New version number and date
   - All user-facing changes (features, fixes)
   - Breaking changes clearly marked
   - sprat-cli version dependency noted
4. Commit with message: `chore: bump version to X.Y.Z`
5. Tag commit: `git tag vX.Y.Z`
6. Deploy release

### Cross-Platform Compliance

**Supported Platforms**:
- Linux (Ubuntu 20.04+, other distributions)
- macOS (10.13+, both Intel and Apple Silicon)
- Windows (Windows 10+, x64)
- WebAssembly (Browser deployment)

#### Platform-Specific Guidelines

**File Paths**:
```cpp
// WRONG: Uses Windows backslashes
QString path = "C:\\Users\\...";

// RIGHT: Use Qt file handling
QString path = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
path = QDir(path).filePath(".local/bin/spratlayout");  // Works on all platforms
```

**Line Endings**:
- Keep source code with Unix line endings (LF)
- Qt handles text encoding differences

**Graphics Rendering**:
- Test OpenGL rendering on different hardware
- WASM uses WebGL subset
- Handle high DPI screens (macOS Retina)

**Resource Paths**:
```cpp
// Resources are embedded during build
QResource::registerResource(":/resources.qrc");
QPixmap icon(":/images/icon.png");
```

**Executable Discovery**:
```cpp
// Check system PATH, fall back to local
QString findTool(const QString& toolName) {
    QFileInfo tool(QStandardPaths::findExecutable(toolName));
    if (!tool.exists()) {
        tool = QFileInfo(QDir(".").absoluteFilePath("../sprat-cli/bin/" + toolName));
    }
    return tool.filePath();
}
```

**Building**:
- Linux: `./build.sh` uses system compiler
- Windows: `build_windows.bat` uses Visual Studio or Ninja
- macOS: `./build.sh` with Clang
- WASM: `./build.sh wasm` with Emscripten

#### Testing on All Platforms
- **Must test before release**:
  - File loading/saving with paths containing spaces
  - Unicode filenames
  - Large sprite collections
  - Long project paths
  - Different screen DPI/scaling

---



### Design Patterns

#### Signal/Slot Architecture
Qt's event-driven model for loose coupling between components. Signals are emitted when state changes, and slots respond to those signals.

**Key Usage**:
- UI updates when project state changes
- Image loading completion triggers layout generation
- Animation playback timing synchronization
- File system changes trigger sync operations

#### Service Layer
Separate services encapsulate functionality and provide clear interfaces:
- `CliToolInstaller` - CLI tool installation and management
- `AnimationExportService` - GIF/video export via external tools
- `FolderSyncService` - Intelligent sprite merging from source folders
- `ProjectSaveService` - Project persistence
- `LayoutRunner` - Layout generation execution
- `SourceFolderWatcher` - File system monitoring

#### Model-View Separation
Core data structures in `src/Core/models.h` are separate from UI implementation:
- `LayoutModel` - Sprite layout data
- `SpritePtr` - Sprite reference wrapper
- `NamedPoint` - Point with metadata (pivots, markers)
- `MarkerKind` - Marker types (point, circle, rectangle, polygon)
- `AnimationFrame` - Animation timeline frame

#### State Management
`ProjectSession` maintains the application's in-memory state:
- Current layout and sprite data
- Loaded timelines and animations
- Project metadata and settings
- Unsaved changes tracking

#### Async Processing
`Qt::Concurrent` for non-blocking operations:
- Image discovery (folder scanning)
- Layout generation execution
- Frame detection and analysis
- Animation export

### Architecture Diagram

```
┌─────────────────────────────────────────┐
│         MainWindow (Coordinator)        │
│  Orchestrates all features and state    │
└──────────┬──────────────────────────────┘
           │
      ┌────┴────┬──────────┬──────────┬──────────┐
      │          │          │          │          │
  ┌───▼──┐  ┌───▼──┐  ┌───▼──┐  ┌───▼──┐  ┌───▼──┐
  │Layout│  │Anim. │  │Sprite│  │CLI   │  │Proj. │
  │Module│  │Module│  │Editor│  │Tools │  │Module│
  └──────┘  └──────┘  └──────┘  └──────┘  └──────┘
      │          │          │          │          │
      └────┬─────┴──────┬───┴──────┬───┴──────┬───┘
           │            │          │          │
      ┌────▼────┬───────▼──┬───────▼──┬──────▼──┐
      │ Core    │ Settings │ Profiles │ Project │
      │ Models  │ Dialog   │ Config   │ Session │
      └─────────┴──────────┴──────────┴─────────┘
           │
           └─────────────────────────────────────┐
                                                  │
                    ┌─────────────────────────────▼──┐
                    │  Qt Framework (Core, Gui, etc) │
                    └────────────────────────────────┘
```

---

## Component Reference

### 1. MainWindow (`src/App/MainWindow/`)

The central orchestrator coordinating all application features. Implements most workflows through signal/slot connections.

**Files**:
- `MainWindow.h/cpp` - Main window class, lifecycle management
- `MainWindow.Ui.cpp` - UI layout and widget composition
- `MainWindow.LayoutFlow.cpp` - Sprite layout canvas management
- `MainWindow.Animation.cpp` - Animation panel, timeline controls
- `MainWindow.Project.cpp` - Project loading/saving/state
- `MainWindow.Cli.cpp` - CLI tool discovery and verification
- `MainWindow.Marker.cpp` - Pivot and marker editing
- `MainWindow.Events.cpp` - Event handling (drag/drop, keyboard)
- `MainWindow.ImageLoading.cpp` - Image and folder loading workflows
- `MainWindow.TimelineEditor.cpp` - Timeline creation/editing UI

**Key Responsibilities**:
- Maintain `ProjectSession` (application state)
- Delegate to services for heavy lifting
- Coordinate UI updates across modules
- Handle user interactions and state transitions

**Entry Point**: `MainWindow::MainWindow()` constructor initializes all components

### 2. SpriteSheetLayout (`src/SpriteSheetLayout/`)

Manages visual sprite arrangement and interaction on the layout canvas.

**Files**:
- `LayoutCanvas.h/cpp` - Main graphics view for sprite positioning
- `SpriteItem.h/cpp` - Visual representation of individual sprites
- `LayoutParser.h/cpp` - Parse layout metadata and sprite data

**Key Classes**:
- `LayoutCanvas` - Custom graphics view with:
  - Multi-sprite selection
  - Drag/drop repositioning
  - Zoom and pan controls
  - Context menus for sprite operations
- `SpriteItem` - QGraphicsItem derived sprite visual with:
  - Hover and selection states
  - Tooltip display
  - Click/drag handling

**Responsibilities**:
- Render layout visually
- Handle sprite selection and manipulation
- Parse and display layout metadata
- Synchronize with project session

### 3. SelectedSpriteFrame (`src/SelectedSpriteFrame/`)

Detailed sprite property editor for pivots, markers, and transformation.

**Files**:
- `PreviewCanvas.h/cpp` - High-resolution sprite preview
- `EditorOverlayItem.h/cpp` - Interactive pivot and marker editing
- `SpriteSelectionPresenter.h/cpp` - Selection state management
- `Markers/` - Marker dialog and operation handlers

**Key Concepts**:
- **Pivots** - Origin point for sprite rotation (relative to sprite bounds)
- **Markers** - Named points for collision, spawn points, etc.
  - Point markers (single coordinate)
  - Circle markers (center + radius)
  - Rectangle markers (bounds)
  - Polygon markers (vertex list)

**Responsibilities**:
- Display selected sprite in detail
- Edit pivot position interactively
- Manage markers (add, edit, delete)
- Real-time preview of changes

### 4. Animation (`src/Animation/`)

Complete animation workflow from creation to export.

**Submodules**:

#### AnimationCanvas
- Real-time preview of sprite animations
- Frame-by-frame stepping
- Playback controls and timing

#### AnimationPlaybackService
- Timing and frame sequencing
- Playback state (playing, paused, stopped)
- Speed control and looping

#### AnimationPreviewService
- Generate frame sequences for preview
- Cache rendered frames
- Handle sprite transformations during animation

#### AnimationExportService
- Export to GIF via ImageMagick
- Export to video via FFmpeg
- Resolution and speed configuration
- Error handling for missing tools

#### Timelines (`src/Animation/Timelines/`)
- `Timeline.h` - Core timeline data structure
- `TimelineGenerator.h/cpp` - Generate timelines from sprite naming patterns
- `TimelineOperations.h/cpp` - Merge, copy, transform timelines

**Timeline Naming Patterns**:
```
sprite_walk_0.png → Timeline "walk" with frames [0, 1, 2, ...]
sprite_jump_start_0.png → Complex naming with metadata
```

### 5. CLITools (`src/CLITools/`)

Integration with sprat-cli toolchain (see [Integration with sprat-cli](#integration-with-sprat-cli)).

**Key Services**:

#### CliToolInstaller
- Discover sprat-cli installation
- Download from GitHub if needed
- Install to `~/.local/bin/`
- Persist version information

#### SpratCliLocator
- Find CLI binary in system PATH
- Check local installation
- Fall back to embedded (WASM)

#### LayoutRunner
- Execute `spratlayout` for sprite packing
- Parse output metadata
- Handle errors and logging

#### SourceFolderWatcher
- Monitor folder for file changes
- Detect new/deleted/modified images
- Debounce rapid changes

#### FolderSyncService
- Intelligent merge of new sprites
- Preserve existing sprite metadata
- Handle sprite deletion scenarios

### 6. Project (`src/Project/`)

Save/load project data with multiple formats.

**Key Classes**:

#### ProjectSession
- In-memory project state wrapper
- Encapsulates layout, timelines, metadata
- Signals for state changes

#### ProjectPayloadCodec
- JSON serialization/deserialization
- Selective block persistence
- Version compatibility

#### ProjectFileLoader
- Load from `.json` or `.zip` files
- Archive extraction
- Validation and error handling

#### ProjectSaveService
- Save projects with persistence options
- Autosave with crash recovery
- ZIP compression support

#### ImageDiscoveryService
- Recursively find images in folders
- Filter by supported formats (PNG, JPG)
- Sort results

**Project Format**:
```json
{
  "metadata": { /* project info */ },
  "layout": { /* sprite layout data */ },
  "timelines": [ /* animation timelines */ ],
  "sprites": [ /* sprite metadata */ ]
}
```

### 7. Profiles (`src/Profiles/`)

Per-profile output processing configuration.

**Key Classes**:

#### SpratProfilesConfig
- Load profile definitions from `spratprofiles.cfg`
- Parse GPU compression modes (DXT1, DXT5)
- Parse dilation settings (0-16 passes)
- Resolution management

#### ProfilesDialog
- Create/edit profiles
- Configure compression and dilation
- Apply profiles to output

**Profile Structure**:
```cfg
[profile_name]
compression=dxt5
dilation=2
resolution=256
```

### 8. Core (`src/Core/`)

Shared data structures and utilities.

**Key Files**:

#### models.h
Central data structures:
- `LayoutModel` - Complete sprite layout
- `SpritePtr` - Smart pointer to sprite with frame tracking
- `NamedPoint` - Point with name/metadata
- `MarkerKind` - Enum for marker types
- `SyncMode` - Watch vs Manual folder sync

#### Utilities
- `ArchiveExtractor` - ZIP file handling
- `ZoomableGraphicsView` - Reusable zoomable canvas
- `ViewUtils` - Graphics utilities (transformations, scaling)
- `MarkerUtils` - Marker rendering and manipulation
- `ResolutionUtils` - Resolution calculations
- `WasmResizeDebounce` - WASM-specific optimization

### 9. Settings (`src/Settings/`)

Application preferences and configuration.

**Key Features**:
- CLI tool paths
- Canvas colors and display settings
- Sprite deduplication mode (hash-based detection)
- Autosave frequency
- Export tool configuration

### 10. App (`src/App/`)

Application entry point and lifecycle.

**Files**:
- `main.cpp` - Entry point, application initialization
- Resource initialization and asset loading

---

## Integration with sprat-cli

sprat-cli is the backend command-line toolchain that performs the actual sprite processing. sprat-gui provides the visual interface to sprat-cli.

### sprat-cli Tools

| Tool | Purpose | Invoked From |
|------|---------|--------------|
| `spratframes` | Detect individual frames in sprite sheets | Frame Detection Dialog |
| `spratlayout` | Generate optimal sprite packing layout | LayoutRunner service |
| `spratpack` | Apply compression and transformations | Animation export |
| `spratconvert` | Format conversion and resizing | Profile application |

### CLI Integration Flow

```
User Action (e.g., "Generate Layout")
         │
         ▼
MainWindow.LayoutFlow.cpp
         │
         ▼
LayoutRunner.execute()
         │
         ├─ Validate CLI tool exists
         │
         ├─ Build command: spratlayout input.json -o output.json
         │
         ├─ Execute via QProcess
         │
         └─ Parse JSON output
              │
              ▼
         Update ProjectSession
              │
              ▼
         Update UI (LayoutCanvas refresh)
```

### CLI Installation

**On First Use**:
1. Check `~/.local/bin/spratlayout` exists
2. If not, check sibling `../sprat-cli` directory
3. If not, download from GitHub (if network available)
4. Install to `~/.local/bin/`

**Configuration**: `CliToolsConfig` manages paths and versions

**WASM Builds**: CLI tools are embedded directly in the binary (no external installation needed)

### Execution Model

```cpp
// In LayoutRunner::execute()
QProcess process;
process.start("spratlayout", arguments);
process.waitForFinished();

// Parse JSON output
QJsonDocument output = QJsonDocument::fromJson(process.readAllStandardOutput());
```

**Error Handling**:
- Missing tool → auto-install attempt
- Invalid arguments → user error message
- Tool crash → logged, UI feedback

---

## UI Architecture

### Main Window Layout

```
┌─────────────────────────────────────────────────────┐
│ Menu Bar (File, Edit, View, Help)                   │
├─────────────────────────────────────────────────────┤
│ Tool Bar (Open, Save, Generate, Export buttons)     │
├────────────────────┬────────────────────────────────┤
│                    │                                │
│  Left Sidebar      │   Central Widget               │
│  ┌──────────────┐  │  ┌──────────────────────────┐ │
│  │ Project Tree │  │  │  LayoutCanvas (Default)  │ │
│  │ Timelines    │  │  │  AnimationCanvas (Anim)  │ │
│  │ Sprites List │  │  │                          │ │
│  └──────────────┘  │  └──────────────────────────┘ │
├────────────────────┼────────────────────────────────┤
│  Right Sidebar     │                                │
│  ┌──────────────┐  │                                │
│  │ Sprite Edit  │  │ (MainWindow.Ui.cpp)           │
│  │ Pivot/Marker │  │                                │
│  └──────────────┘  │                                │
├────────────────────┴────────────────────────────────┤
│ Status Bar (Info, progress, hints)                  │
└─────────────────────────────────────────────────────┘
```

### Dialog Flow

**Open Project**:
```
File → Open → QFileDialog (*.json, *.zip)
         │
         ▼
ProjectFileLoader::load()
         │
         ▼
MainWindow::loadProject()
         │
         ▼
UI Updates
```

**Generate Layout**:
```
User: "Generate Layout"
         │
         ▼
MainWindow.LayoutFlow.cpp
         │
         ├─ Validate sprites loaded
         │
         ├─ Show Profile Selection (if needed)
         │
         ▼
LayoutRunner.execute("spratlayout")
         │
         ▼
Update LayoutCanvas
```

**Create Animation**:
```
Timeline → Build from naming pattern
         │   OR manual frame selection
         │
         ▼
MainWindow.TimelineEditor.cpp
         │
         ├─ Name timeline
         │
         ├─ Select frames
         │
         └─ Set timing
              │
              ▼
         Add to ProjectSession
              │
              ▼
         AnimationCanvas preview
```

### Key UI Classes

#### Custom Graphics Items
- `SpriteItem` - Sprite visual in layout
- `EditorOverlayItem` - Interactive pivot/marker editor
- `MarkerItem` - Visual marker representation

#### Custom Widgets
- `LayoutCanvas` - QGraphicsView subclass for layout
- `AnimationCanvas` - Animation preview widget
- `PreviewCanvas` - Sprite detail preview

#### Dialogs
- `SettingsDialog` - Application preferences
- `ProfilesDialog` - Profile management
- `FrameDetectionDialog` - Auto-detect frame boundaries
- `MarkerDialog` - Add/edit markers

---

## Data Flow

### Complete Workflow

```
1. Load Image Folder
   ├─ User selects folder via file dialog
   ├─ ImageDiscoveryService scans recursively
   └─ Load images into memory

2. Generate Layout (Optional: Frame Detection)
   ├─ FrameDetectionDialog (optional auto-detect or manual split)
   ├─ LayoutRunner executes "spratlayout"
   ├─ Parse output JSON
   └─ Create LayoutModel with sprites

3. Edit Sprites
   ├─ User clicks sprite in LayoutCanvas
   ├─ SelectedSpriteFrame updates with details
   ├─ Edit pivot/markers visually
   └─ Changes tracked in sprite data

4. Create Animations
   ├─ TimelineEditor builds animation sequence
   ├─ AnimationCanvas previews in real-time
   └─ Timeline stored in ProjectSession

5. Export Animation
   ├─ User selects format (GIF/video)
   ├─ Choose profile (resolution, compression)
   ├─ AnimationExportService generates frames
   ├─ Call ImageMagick/FFmpeg
   └─ Save output file

6. Save Project
   ├─ ProjectSaveService prepares payload
   ├─ ProjectPayloadCodec serializes to JSON
   ├─ Optional ZIP compression
   └─ Write to disk
```

### State Synchronization

```
ProjectSession (source of truth)
    │
    ├─ Signals sprite added/removed
    │         │
    │         ▼
    │   LayoutCanvas updates
    │
    ├─ Signals sprite pivot changed
    │         │
    │         ▼
    │   PreviewCanvas refreshes
    │
    ├─ Signals timeline added
    │         │
    │         ▼
    │   Timeline list updates
    │
    └─ Signals unsaved changes
              │
              ▼
         Title bar updates (shows *)
```

---

## Common Tasks

### Adding a New Sprite Property

1. **Update Core Model** (`src/Core/models.h`):
   ```cpp
   struct Sprite {
       // ... existing fields ...
       NewPropertyType newProperty;  // Add new field
   };
   ```

2. **Update Serialization** (`src/Project/ProjectPayloadCodec.cpp`):
   ```cpp
   // In toJson() and fromJson()
   spriteJson["newProperty"] = sprite.newProperty;
   ```

3. **Add UI Control** (`src/SelectedSpriteFrame/` or appropriate dialog):
   ```cpp
   // Add QLineEdit, QSpinBox, etc. for user input
   connect(ui->newPropertyInput, &QWidget::valueChanged,
           this, &SpriteEditor::onNewPropertyChanged);
   ```

4. **Update ProjectSession**:
   ```cpp
   projectSession->updateSprite(spriteId, newProperty);
   ```

### Adding a New Export Format

1. **Create Service** in `src/Animation/`:
   ```cpp
   class AnimationExportServiceNewFormat : public AnimationExportService {
       bool export(const Animation&, const QString& path) override;
   };
   ```

2. **Integrate into MainWindow**:
   ```cpp
   // In MainWindow.Animation.cpp
   case ExportFormat::NewFormat:
       service = new AnimationExportServiceNewFormat();
       break;
   ```

3. **Add to UI** (Export dialog)

### Adding a New CLI Tool Integration

1. **Create Runner** in `src/CLITools/`:
   ```cpp
   class NewToolRunner {
       bool execute(const QJsonObject& input, QJsonObject& output);
   };
   ```

2. **Handle CLI Discovery** in `CliToolInstaller`:
   - Add tool name to discovery
   - Add version check if needed

3. **Integrate into MainWindow**:
   ```cpp
   NewToolRunner runner;
   runner.execute(inputData, outputData);
   ```

### Source Folder Sync Integration

The `SourceFolderWatcher` and `FolderSyncService` handle automatic or manual synchronization:

```cpp
// In MainWindow.cpp
SourceFolderWatcher watcher(sourceFolder);
connect(&watcher, &SourceFolderWatcher::folderChanged,
        this, [this](const QStringList& changes) {
    if (syncMode == SyncMode::Watch) {
        FolderSyncService::sync(projectSession, changes);
    }
});
```

---

## Development Workflow

### Building the Project

```bash
# Linux / General
./build.sh

# Windows
build_windows.bat

# Manual (any platform)
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Building for WebAssembly

```bash
# Requires Emscripten SDK
source /path/to/emsdk/emsdk_env.sh

./build.sh wasm
```

### Running Tests

```bash
cd build
ctest --output-on-failure
```

**Test Files** (in `tests/`):
- `TestLayoutCanvas.cpp` - Layout canvas operations
- `TestAnimationGeneration.cpp` - Timeline generation
- `TestAnimationPlayback.cpp` - Playback logic
- `TestProjectPayload.cpp` - Save/load functionality
- `TestCoreModels.cpp` - Data structure operations
- `TestImageDiscovery.cpp` - Image discovery service
- `TestProjectSession.cpp` - Session state management

### Code Organization Tips

**File Naming**:
- Headers: `ClassName.h`
- Implementation: `ClassName.cpp`
- Specialized implementations: `ClassName.Feature.cpp`

**Header Guards**:
```cpp
#ifndef CLASSNAME_H
#define CLASSNAME_H
// ...
#endif
```

**Qt Naming Conventions**:
- Slots: `onEventName()` or `handleEventName()`
- Signals: `nameChanged()`, `nameCreated()`, etc.
- Private members: `m_memberName`

### Key Files to Know

| File | Purpose |
|------|---------|
| `src/App/main.cpp` | Application entry point |
| `src/App/MainWindow/MainWindow.h` | Main window declaration |
| `src/Core/models.h` | Core data structures (REFERENCE THIS OFTEN) |
| `src/Project/ProjectSession.h` | Application state management |
| `CMakeLists.txt` | Build configuration |
| `tests/` | Test suite |

### Debugging Tips

1. **Enable Logging**:
   ```cpp
   qDebug() << "Message:" << variable;
   qWarning() << "Warning:" << variable;
   ```

2. **Qt Creator Debugger**: Set breakpoints and inspect state

3. **Signal/Slot Debugging**: Use `-DQT_DEBUG_PLUGINS` flag

4. **Project Session State**: Inspect `ProjectSession::m_layout`, `m_timelines`, etc.

5. **CLI Tool Output**: Check `LayoutRunner` output for tool errors

---

## Navigation Quick Reference

**Finding Common Things**:

| What | Where |
|------|-------|
| Main window setup | `src/App/MainWindow/MainWindow.h` |
| Data structures | `src/Core/models.h` |
| Application state | `src/Project/ProjectSession.h` |
| Layout rendering | `src/SpriteSheetLayout/LayoutCanvas.h` |
| CLI execution | `src/CLITools/LayoutRunner.h` |
| Animation preview | `src/Animation/AnimationCanvas.h` |
| Project save/load | `src/Project/ProjectSaveService.h` |
| Settings | `src/Settings/SettingsDialog.h` |
| Sprite editing | `src/SelectedSpriteFrame/PreviewCanvas.h` |
| Build config | `CMakeLists.txt` |
| Tests | `tests/` directory |

---

## Deployment & Distribution

sprat-gui is distributed across multiple platforms and delivery channels to reach game developers and UI artists wherever they work.

### Distribution Channels

#### Desktop Builds
**Linux**:
- AppImage package (portable, works on most distributions)
- `.deb` package (for Debian/Ubuntu-based systems)
- Build from source via `./build.sh`

**Windows**:
- Standalone `.exe` with bundled Qt6 DLLs
- Built via `build_windows.bat`
- Installer (NSIS) for easy installation

**macOS**:
- `.app` bundle (universal binary for Intel/Apple Silicon)
- Notarized and code-signed for distribution
- DMG installer package

#### Web (WebAssembly)
- **Primary Deployment**: itch.io
  - URL: https://sprat-gui.itch.io
  - Updated with each release
  - Works in modern browsers (Chrome, Firefox, Safari, Edge)
- **Fallback**: GitHub Pages or project website
- **Browser Support**: ES6 compatible browsers with WebGL

### Release Process

#### Pre-Release Checklist
1. **Code Complete**:
   - All features merged to main branch
   - PR reviews completed
   - All test suites passing

2. **Version Bumped**:
   - Update version numbers
   - Update CHANGELOG.md
   - Run full test suite with new version

3. **Documentation Updated**:
   - README.md reflects new features
   - AGENTS.md updated if architecture changed
   - API docs up-to-date (if applicable)

4. **Cross-Platform Testing**:
   - Linux: Test on Ubuntu 20.04+
   - macOS: Test on Intel and Apple Silicon
   - Windows: Test on Windows 10/11
   - WASM: Test in Chrome and Firefox

5. **Performance Verification**:
   - Canvas rendering smooth (60 FPS)
   - Animation preview responsive
   - Layout generation completes in reasonable time
   - No memory leaks (profile with Valgrind/Instruments)

#### Release Artifacts
- **Linux**: AppImage, .deb package
- **macOS**: Universal .app, DMG
- **Windows**: .exe with DLLs, MSI installer
- **WASM**: Optimized build for itch.io
- **Source**: GitHub release with tagged commit

#### Deployment Steps
1. **Tag Release**: `git tag vX.Y.Z && git push origin vX.Y.Z`
2. **Build Artifacts**:
   ```bash
   # Linux
   ./build.sh && ./package_appimage.sh
   ./package_deb.sh

   # Windows
   build_windows.bat Release

   # macOS
   ./build.sh && ./package_dmg.sh

   # WASM
   ./build.sh wasm && ./deploy_wasm.sh
   ```
3. **Create GitHub Release**: Draft release with artifacts and changelog
4. **Deploy to itch.io**: Upload WASM build
5. **Announce**: Social media, project updates, community channels

### itch.io Deployment

**itch.io Project**:
- Organization: LiquidityC or project owner
- Project Name: sprat-gui
- Game Type: Tools/Utilities
- Platforms: Web (WASM)
- Public: Yes (free)

**Deployment**:
1. Build WASM with optimizations:
   ```bash
   ./build.sh wasm && strip -s build/wasm/*.js
   ```
2. Upload to itch.io:
   - Create `.zip` with HTML, JS, WASM files
   - Upload via itch.io dashboard
   - Set as "Web" platform
   - Test in itch.io embedded viewer

**Analytics**:
- Monitor itch.io download/play statistics
- Track browser compatibility issues
- Gather feedback from community

### Desktop Installer Considerations

**Windows Installer**:
- Bundled Qt6 runtime DLLs
- Visual C++ redistributable check
- Create start menu shortcuts
- Associate `.sprat` files with application
- Optional: Add to PATH

**macOS Installer (DMG)**:
- Code signing and notarization (Apple requirements)
- Universal binary (Intel + Apple Silicon)
- Drag-to-Applications workflow
- Proper app icon and branding

**Linux Packages**:
- Dependencies listed clearly
- AppImage: Single file, no dependencies needed
- .deb: Specify Qt6, LibArchive, ZLIB dependencies
- Post-install script to add CLI tools if needed

---

## Documentation Standards

All documentation must be **accurate, up-to-date, and helpful** for users and developers.

### Files to Maintain

#### README.md (User-Facing)
- Clear project description and feature list
- Installation instructions for all platforms
- Quick start guide with screenshots
- Common use cases and workflows
- Link to itch.io for web version
- Attribution and license information

**Update Trigger**:
- New features added
- Installation process changed
- Major bugs fixed that affected usage

#### CHANGELOG.md (Release Notes)
- Entries for every release (MAJOR.MINOR.PATCH)
- Format:
  ```markdown
  ## [X.Y.Z] - YYYY-MM-DD

  ### Added
  - New feature description
  - Another new feature

  ### Changed
  - Behavior change description

  ### Fixed
  - Bug fix description

  ### Deprecated
  - Deprecated feature

  ### Removed
  - Removed feature

  ### Dependencies
  - Updated sprat-cli to vX.Y.Z
  ```

**Update Trigger**:
- Every commit that affects user (feature, fix, API change)

#### AGENTS.md (Developer Guide)
- Architecture overview
- Component descriptions
- Development workflow
- Common tasks and patterns
- Testing requirements
- Deployment steps

**Update Trigger**:
- Major architecture changes
- New components added
- Process/workflow changes
- Quality standards updated

#### Contributing Guidelines (Optional)
If accepting external contributions:
- Code style requirements
- Pull request process
- Testing requirements
- Commit message format
- Issue templates

### Code Documentation

#### Header Comments
Every public class/method should have a header comment:
```cpp
/// Loads a sprite sheet layout from JSON file
///
/// The layout must contain valid sprite definitions with positions and metadata.
/// Existing sprite data will be overwritten.
///
/// @param layoutPath Path to layout JSON file
/// @return LayoutModel containing parsed sprites, or nullptr on error
/// @note This is an async operation; connect to loadingFinished signal
std::shared_ptr<LayoutModel> loadLayout(const QString& layoutPath);
```

#### Complex Logic
Only document non-obvious logic:
```cpp
// Good: Explains why, not what
// We sort sprites by position to ensure consistent layout generation
std::sort(sprites.begin(), sprites.end(),
          [](const Sprite& a, const Sprite& b) { return a.position.y < b.position.y; });

// Bad: Comments the obvious
// Loop through sprites
for (auto& sprite : sprites) {
    // ...
}
```

#### File Headers
Each source file should have a header:
```cpp
/// @file ClassName.cpp
/// Implementation of sprite layout canvas with drag/drop support
///
/// Handles visual sprite positioning, selection, and interactive editing.
```

### Keeping Documentation Synchronized

#### During Development
- **Small Changes**: Update relevant docs immediately
- **New Features**: Add to README.md and AGENTS.md before PR merge
- **API Changes**: Update AGENTS.md and inline docs
- **Bug Fixes**: Update CHANGELOG.md entry

#### Before Release
1. **Review Documentation**:
   - README.md matches current behavior
   - AGENTS.md reflects actual architecture
   - CHANGELOG.md complete and accurate
   - Code comments still relevant

2. **Check Accuracy**:
   - All platform instructions tested
   - Build steps actually work
   - Screenshots still match current UI
   - Links still valid

3. **Run Spell Check**:
   - No typos in user-facing docs
   - Consistent terminology

#### Documentation Checklist
- [ ] Feature described in README.md
- [ ] CHANGELOG.md updated
- [ ] AGENTS.md updated if architecture affected
- [ ] Code comments clear and accurate
- [ ] Links verified
- [ ] Cross-platform instructions tested
- [ ] No obsolete documentation remains

### Writing Style

**For Users (README.md)**:
- Clear and concise
- Focus on "what" and "why"
- Use examples and screenshots
- Assume non-technical users might read this
- Active voice

**For Developers (AGENTS.md)**:
- Technical and specific
- Explain architecture decisions
- Reference exact file locations and line numbers
- Assume C++ and Qt knowledge
- Code examples encouraged

**For Release Notes (CHANGELOG.md)**:
- User-focused language (not "refactored FooService")
- Group by impact: Added, Changed, Fixed, etc.
- Highlight breaking changes
- Keep entries concise

---

## Summary

sprat-gui is a well-architected Qt6 application with clear separation of concerns:

- **MainWindow** orchestrates features and coordinates state
- **Services** encapsulate functionality (CLI, animation, export, sync)
- **Models** in Core hold data structures
- **UI Components** provide visual interfaces
- **ProjectSession** maintains state with signal/slot updates
- **sprat-cli** handles heavy sprite processing via QProcess execution

The codebase follows Qt conventions with proper signal/slot usage, async operations via Qt::Concurrent, and clean separation between UI and business logic.

### Commitment to Quality

Every change to sprat-gui must uphold these standards:

1. **Code Quality**: Readable, reusable, well-documented code following C++17 and Qt conventions
2. **Testing**: All tests passing before commit; new tests for new functionality
3. **UI/UX**: Smooth, responsive, useful for game developers and designers
4. **Cross-Platform**: Works on Linux, macOS, Windows, and WebAssembly
5. **Dependencies**: Using latest stable sprat-cli; minimal, well-managed dependencies
6. **Versioning**: Semantic versioning for clear version communication
7. **Documentation**: README, CHANGELOG, and code comments kept accurate and up-to-date
8. **Performance**: Smooth rendering (60 FPS), responsive UI, efficient memory usage
9. **Deployment**: Regular releases to desktop (all platforms) and web (itch.io)

### For AI Agents

Use this guide to understand where to make changes and how components interact:

1. **Understand Architecture**: Review the Architecture section and component descriptions
2. **Find Code**: Use Navigation Quick Reference to locate relevant files
3. **Maintain Quality**: Follow Code Quality Standards and Testing Requirements
4. **Test Changes**: Run full test suite before declaring work done
5. **Update Documentation**: Keep README.md, AGENTS.md, and CHANGELOG.md in sync
6. **Check Platforms**: Verify changes don't break cross-platform compatibility
7. **Consider Performance**: Use Qt::Concurrent for long operations; profile rendering

Always reference `src/Core/models.h` for data structures and `src/App/MainWindow/MainWindow.h` for the application coordinator. Make sure the app still works after your changes, and leave documentation better than you found it.
