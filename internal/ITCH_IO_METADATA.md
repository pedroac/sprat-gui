# itch.io Project Setup - Metadata & Description

Use this file as a reference when creating/updating the sprat-gui project page on itch.io.

---

## Project Metadata

**Project Title**: Sprat GUI

**Short Description**:
Visual sprite sheet editor with animation authoring tools

**Long Description**:
```
Sprat GUI is a visual frontend for sprat-cli that helps game developers
and pixel artists streamline sprite sheet creation and animation authoring.

FEATURES:
• Automatic frame detection from sprite sheets
• Visual sprite sheet layout generation from image folders
• Pixel-perfect pivot and marker editing
• Animation timeline authoring with auto-generation
• Real-time animation preview and playback
• Export animations as GIF (ImageMagick) or video (FFmpeg)
• Complete project persistence (JSON/ZIP formats)
• Built-in CLI tool management and installation

PERFECT FOR:
✓ Game developers iterating on sprite assets
✓ Pixel artists automating sprite pipeline tasks
✓ Teams collaborating on animation specifications
✓ Anyone needing fast visual feedback without scripting

AVAILABLE ON:
✓ Linux
✓ macOS (Intel & Apple Silicon)
✓ Windows
✓ Web Demo (browser-based, no installation)

Open source, MIT licensed, and actively maintained.
```

---

## itch.io Settings

**Category**: Game Development Tools / Utilities

**Tags**:
- sprite-sheet
- animation
- game-dev
- graphics
- game-development
- open-source
- tool
- pixel-art
- editor

**Kind of Project**: Tool

**License**: MIT (open source)

**Platforms**:
- ✓ Windows
- ✓ macOS
- ✓ Linux
- ✓ Web (HTML5)

---

## System Requirements

### Windows
- **OS**: Windows 10 or Windows 11
- **CPU**: x86_64 processor
- **RAM**: 2 GB minimum, 4 GB recommended
- **Storage**: ~50 MB for application + dependencies
- **Notes**: VC++ runtime included in installer

### macOS
- **OS**: macOS 10.15 Catalina or later
- **CPU**: Intel x86_64 or Apple Silicon (M1/M2/M3)
- **RAM**: 2 GB minimum, 4 GB recommended
- **Storage**: ~50 MB for application

### Linux
- **OS**: Ubuntu 20.04 LTS, Fedora 35+, Arch, or compatible
- **CPU**: x86_64 processor
- **RAM**: 2 GB minimum, 4 GB recommended
- **Storage**: ~20 MB for application
- **Dependencies**: Qt6 libraries (usually pre-installed)

### Optional Requirements (for enhanced features)
- **ImageMagick**: GIF animation export
- **FFmpeg**: Video animation export
- **zip/unzip**: Project archiving support

---

## Release Information

**Current Version**: 0.2.5

**Release Date**: 2026-05-04

**Release Notes**:
```
v0.2.5 - CLI Integration Stability Improvements

This release improves CLI tool integration reliability and reduces
friction for new users:

✓ Robust version parsing with regex (no more edge cases)
✓ Version persistence for faster startup
✓ Manual CLI path selection when auto-detection fails
✓ Real-time installation progress feedback
✓ Cross-platform stability verified (Linux, macOS, Windows)

Perfect for game developers who want a reliable sprite sheet editor.

See CHANGELOG.md for complete details.
```

---

## Download Links Structure

When uploading builds, use this naming convention:

```
sprat-gui-v0.2.5-linux-x86_64.tar.gz
sprat-gui-v0.2.5-macos-intel-x86_64.dmg
sprat-gui-v0.2.5-macos-arm64.dmg
sprat-gui-v0.2.5-windows-x86_64.zip
sprat-gui-v0.2.5-web.tar.gz (for WASM)
```

---

## Installation Instructions (for itch.io page)

### Windows
1. Download `sprat-gui-v0.2.5-windows-x86_64.zip`
2. Extract to desired location
3. Run `sprat.bat` from the extracted folder
4. Alternatively, run `bin/sprat-gui.exe` directly

### macOS
1. Download `sprat-gui-v0.2.5-macos-*.dmg` (intel or arm64)
2. Open the DMG file
3. Drag **sprat-gui** to the Applications folder
4. Double-click **sprat-gui** from Applications to launch

### Linux
1. Download `sprat-gui-v0.2.5-linux-x86_64.tar.gz`
2. Extract: `tar xzf sprat-gui-v0.2.5-linux-x86_64.tar.gz`
3. Run: `./sprat-gui/sprat-gui`
4. Qt6 libraries are usually pre-installed; if needed, install via package manager

### Web Demo
1. No installation required!
2. Open https://sprat-gui.itch.io in your browser
3. Note: Some features (frame detection, GIF/video export) work best with desktop version

---

## Cover Art / Screenshots

**Current assets** in `README_assets/`:
- `sprat-gui.png` - Main application window
- `frames_detector.png` - Frame detection feature
- `animation.png` - Animation timeline and preview
- `markers_*.png` - Marker editing examples
- `selected_frame_editor*.png` - Pivot and marker editing
- `layout_filter_by_name.png` - Frame search functionality

**Recommended for itch.io**:
1. Use `sprat-gui.png` as main cover image
2. Use 2-3 best screenshots showing:
   - Main interface with loaded sprites
   - Frame detection in action
   - Animation preview panel

---

## Unique Selling Points (for page description)

✨ **Visual-First Approach**
- See exactly what your sprites look like without scripting
- Real-time preview of animations and pivots

⚡ **Time-Saving Workflow**
- Auto-detect frames from sprite sheets
- Generate layouts from image folders
- Create timelines from naming patterns
- Export to GIF/video in seconds

🎨 **Pixel-Perfect Control**
- Edit pivots and markers directly on frames
- Visual feedback with zoom and pan controls
- Keyboard shortcuts for power users

🔧 **Developer-Friendly**
- Built-in CLI tool management
- Manual path selection if needed
- Cross-platform (Linux, macOS, Windows, Web)

📦 **Project Persistence**
- Save as JSON (human-readable) or ZIP (portable)
- Continue work later with full state restoration

---

## Download Button Text

**Primary CTA**: "Download Now" or "Get sprat-gui"

**Secondary CTAs**:
- "Try Web Demo" (points to https://sprat-gui.itch.io)
- "View on GitHub" (points to https://github.com/pedroac/sprat-gui)

---

## Additional Resources to Link

- **GitHub Repository**: https://github.com/pedroac/sprat-gui
- **README**: README.md (includes full feature walkthrough)
- **CHANGELOG**: CHANGELOG.md (version history and updates)
- **LICENSE**: MIT License (open source)
- **sprat-cli**: https://github.com/pedroac/sprat-cli (backend tool)

---

## Content Rating / ESRB

Not applicable (tool/utility, not a game)

---

## Monetization

**Pricing**: Free

**Support/Donations**:
- Link to "Buy Me A Coffee" or Patreon (optional)
- Emphasis on open source and community contributions welcome

---

## Community & Support

**Support Channels**:
- GitHub Issues: bug reports and feature requests
- GitHub Discussions: general questions and feedback
- Email: (add contact info if desired)

**Contributing**:
- PRs welcome for bug fixes and improvements
- Translation contributions appreciated
- Platform-specific packaging contributions needed

---

## Announcement / Launch Text (for social media)

```
📣 Introducing sprat-gui v0.2.5!

A visual sprite sheet editor that makes animation authoring fast and fun.

✨ Automatic frame detection
✨ Visual pivot & marker editing
✨ Timeline generation from frame names
✨ Export to GIF/video
✨ Cross-platform (Windows, macOS, Linux, Web)

Free, open source, and MIT licensed.

🎮 Try now on itch.io: https://sprat-gui.itch.io
🐙 GitHub: https://github.com/pedroac/sprat-gui

Perfect for game developers and pixel artists!
```

---

## Verification Checklist Before Publishing

- [ ] All files included in distribution
- [ ] README.md is current and accurate
- [ ] CHANGELOG.md reflects release version
- [ ] LICENSE file included
- [ ] All links (GitHub, etc.) are correct
- [ ] Version number matches in VERSION file
- [ ] System requirements clearly documented
- [ ] Installation instructions tested
- [ ] Screenshots are up-to-date
- [ ] Project description is compelling
- [ ] Tags are appropriate and helpful
- [ ] Platforms marked correctly in itch.io settings
- [ ] Release notes are complete

---

For questions or changes, refer to the main README.md and CHANGELOG.md files.
