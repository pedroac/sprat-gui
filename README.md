# Sprat-gui

GUI frontend for [`sprat-cli`](https://github.com/pedroac/sprat-cli) to build sprite sheets and their metadata (animations, markers, nine-slices, puppets) for games and UIs.

[![Changelog](https://img.shields.io/badge/changelog-v0.8.0-blue)](CHANGELOG.md)

![Sprites workspace](README_assets/sprites_workspace.png)

## Features

- **Sources**: load sprites from folders, archives (ZIP/tar), single images and sprite sheets (with automatic frame detection), animated GIFs or URLs, and keep the project in sync with them.
- **Atlases**: split sprites into atlases and pack them with per-profile options (GPU compression, dilation, deduplication).
- **Sprites**: place pivots and markers (points, circles, rectangles, polygons) on each frame, with onion skinning and flipbook mode.
- **Animations**: build timelines by hand or from frame names (`Run_0`, `Run_1`...), preview them, and export GIF or video.
- **Nine-slices**: define stretchable regions for UI elements such as buttons and panels.
- **Puppets**: build cutout rigs from sprites, pose them and animate the poses.
- **Projects**: save the whole state to a project folder (or open one from a `.zip`), with autosave and undo.
- **Export**: write atlases and metadata for your engine through sprat-cli's transforms.

If your pipeline is already fully scripted, you may not need this. If you want fast visual iteration on game or UI assets, it is likely useful.

## Try online

The **[web version](https://sprat-gui.itch.io)** runs in your browser with no installation. Frame detection and GIF/video export need the desktop version.

## Download and install

Pre-built binaries are on **[itch.io](https://pedroac.itch.io/sprat)**, with the sprat-cli tools included.

- **Linux**: download `sprat-gui-linux.AppImage`, make it executable (`chmod +x`), and run it.
- **Windows**: extract `sprat-gui-windows.zip` and run `sprat-gui.exe`. The executable is unsigned, so SmartScreen may warn the first time: click **More info → Run anyway**.
- **macOS**: open `sprat-gui-macos.dmg` and drag `sprat-gui.app` to **Applications**. The app isn't notarized, so the first launch is blocked: click **Done**, then **System Settings → Privacy & Security → Open Anyway**. Or run `xattr -dr com.apple.quarantine /Applications/sprat-gui.app`.

For GIF export, install ImageMagick (`magick` or `convert`); for video, FFmpeg (`ffmpeg`).

To build from source, see [docs/building.md](docs/building.md).

## Quick start

1. Launch the app. If the sprat-cli tools are missing, it offers to install them (or set their folder in **Settings → CLI Tools**).
2. Drop a folder of sprites on the window, or use **File → Add Source Folder...** (also **Add Source File...** for a sprite sheet or GIF, and **Add Source URL...**).
3. In **Sprites** (`Alt+S`), click a sprite to name it and place its pivot and markers.
4. In **Atlases** (`Alt+A`), split sprites into atlases if you need several, and pick their profiles; **Settings → Manage Profiles** sets each profile's packing, compression and artifact reduction.
5. In **Frame Animation** (`Alt+F`), add timelines, or auto-create them from folders and frame names, and preview them.
6. Optionally, mark stretchable UI sprites in **Nine-Slice** (`Alt+N`) and build puppets in **Puppets** (`Alt+P`).
7. **File → Save As** saves the project folder; **Exportation** (`Alt+X`) writes the atlases and metadata for your engine.

Inside the app, **Help → Quick Start** gives a guided introduction and **Help → Hotkeys** lists every shortcut.

## Documentation

- [User guide](docs/user-guide.md): each workspace, sources, settings, projects, timeline naming, troubleshooting
- [Keyboard and pointer controls](docs/shortcuts.md)
- [Building from source](docs/building.md): requirements, build scripts, translations, code layout
- [Contributing](CONTRIBUTING.md)
- [Changelog](CHANGELOG.md)

## Attribution

- Screenshots use the [“Adventurer Girl – Free Sprite”](https://opengameart.org/content/adventurer-girl-free-sprite) pack by pzUGH from OpenGameArt, reused under the original license.
- Core CLI tooling comes from the [`sprat-cli`](https://github.com/pedroac/sprat-cli) repository.
- Icons adapted with AI (Claude, Anthropic) from icons first taken from [SVG Repo](https://www.svgrepo.com).
- The smoke test's sample sprites (`tests/assets/streets_of_fight_files.zip`) are by Luis Zuno, redistributable per the license file in the archive.

## License

MIT. See [LICENSE](LICENSE).

## Support

[![Buy Me A Coffee](https://img.buymeacoffee.com/button-api/?text=Buy%20me%20a%20coffee&emoji=&slug=pedroac&button_colour=FFDD00&font_colour=000000&font_family=Cookie&outline_colour=000000&coffee_colour=ffffff)](https://buymeacoffee.com/pedroac)
