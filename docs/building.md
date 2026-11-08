# Building Sprat GUI

How to build Sprat GUI from source and how the code is organized. Pre-built downloads are listed in the [README](../README.md#download-and-install). For the architecture in depth, see [AGENTS.md](../AGENTS.md).

On Linux and macOS, `deploy/build-from-source.sh` checks the dependencies, clones sprat-cli and sprat-gui, and builds both.

## Requirements

- Qt 6 (Core, Gui, Widgets, Concurrent, Network)
- LibArchive and ZLIB
- CMake >= 3.16
- C++17 compiler
- Sprat CLI tools:
  - `spratframes`
  - `spratlayout`
  - `spratpack`
  - `spratconvert` (optional for format transforms)
  *(If the app detects them missing it offers to download and build them for you.)*
  *(libsquish is bundled in sprat-cli and built alongside it for DXT texture compression.)*
  - If you already have the `sprat-cli` repository checked out as a sibling to this project (for example `../sprat-cli`), build that copy and the GUI will automatically pick up `spratframes`, `spratunpack`, `spratlayout`, `spratpack`, and `spratconvert` from it, or let you point Settings directly at those binaries.
- Optional export tools:
  - ImageMagick (`magick` or `convert`) for GIF
  - FFmpeg (`ffmpeg`) for video
When the installer builds the CLI tools for you (Linux), it clones the `sprat-cli` release tag pinned in `DEPENDENCIES` (`git clone --depth 1 --branch <tag> https://github.com/pedroac/sprat-cli.git`), or uses a `../sprat-cli` checkout of that same version, and checks the built tools' versions before installing them to `~/.local/bin`.

## Local CLI development

Place the `sprat-cli` repository beside this project (for example, at `../sprat-cli`) so the GUI can work with the same source tree. After building that repo (for example `cmake -S ../sprat-cli -B ../sprat-cli/build && cmake --build ../sprat-cli/build`), Sprat GUI auto-detects the generated `spratframes`, `spratlayout`, `spratpack`, and `spratconvert` binaries, or you can point the Settings dialog at the directory.

## Build

### Linux / WASM

Default build (updates `DEPENDENCIES` from remote tags, configures, builds, then runs tests):

```bash
sh build.sh
```

`build.sh` behavior:
- Updates `DEPENDENCIES` to the `../sprat-cli` checkout's version if that version is a published tag, otherwise to the newest published `sprat-cli` release tag, if network access is available. An untagged local version is never pinned, since CI clones the pinned tag.
- Configures with `cmake -B build -S .`.
- Builds with `cmake --build build`.
- Runs tests with `ctest --test-dir build --output-on-failure`.

### Windows

A dedicated batch file is provided for Windows builds using Visual Studio or Ninja:

```cmd
build_windows.bat
```

This script:
- Configures the project with `SPRAT_EMBEDDED_CLI=ON` (the default for Windows).
- Automatically downloads `sprat-cli` source if not found locally.
- Builds the project in Release mode.
- Deploys Qt dependencies using `windeployqt`.
- Copies required assets (`spratprofiles.cfg` and `transforms/`).

On Windows, the application and its dependencies are placed in a `build/bin` directory. A `sprat.bat` launcher is created in the `build` root for convenience.

### Manual build

Manual build (does not modify `DEPENDENCIES`):

```bash
cmake -S . -B build -DSPRAT_EMBEDDED_CLI=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Run:

```bash
./build/sprat-gui
```

## Translations

User-facing GUI strings use Qt translation APIs (`tr(...)` in `QObject` classes, `QCoreApplication::translate(...)` in non-`QObject` code). Translation catalogs live in `i18n/` (`i18n/sprat-gui_en.ts`).

1. Update code strings using `tr`/`translate`.
2. Run `lupdate` to refresh `.ts` entries.
3. Translate in Qt Linguist.
4. Run `lrelease` to generate `.qm` files.
5. Verify by launching with a target locale (the app auto-loads `sprat-gui_<locale>.qm` from `./i18n` or the app directory).

If `Qt6LinguistTools` is not installed, CMake still builds the app but skips automatic `.ts/.qm` generation.

## Project structure (high-level)

- `src/App/MainWindow/` — UI composition and event delegation
- `src/SpriteSheetLayout/` — layout canvas/parser/sprite model UI item
- `src/SelectedSpriteFrame/` — selected-frame preview/overlay/markers flow
- `src/Animation/` — animation playback/export logic
- `src/Animation/Timelines/` — timeline building and operations
- `src/Animation/Test/` — animation preview test operations
- `src/NineSlice/` — nine-slice editor canvas and preview widget
- `src/App/Workspaces/` — the workspaces' UI (Atlas, AtlasesManagement, NineSlice, FrameAnimation, Puppet, Export) and widgets they share
- `assets/icons/` — the app's icons, one SVG each (see AGENTS.md for their style)
- `src/Project/` — project load/save/payload/autosave
- `src/CLITools/` — CLI discovery/config/install helpers
- `src/Settings/` — settings dialog/coordinator
- `src/Core/` — shared models
