# Installation Guide - sprat-gui v0.2.5

Choose your platform below for step-by-step installation instructions.

---

## Windows

### Quick Start
1. **Download** `sprat-gui-v0.2.5-windows-x86_64.zip`
2. **Extract** to your desired location (e.g., `C:\Program Files\sprat-gui`)
3. **Run** `sprat.bat` from the extracted folder
   - Alternatively, navigate to `bin/` and double-click `sprat-gui.exe`

### System Requirements
- **OS**: Windows 10 or Windows 11
- **CPU**: x86_64 processor
- **RAM**: 2 GB minimum (4 GB recommended)
- **Storage**: ~50 MB available
- **Notes**: All required dependencies (Qt6, VC++ runtime) are included

### Troubleshooting Windows
- **"DLL not found" error**: Ensure all files from the ZIP are extracted
- **Application won't start**: Try running as Administrator
- **CLI tools missing**: Use Settings → Install CLI Tools

---

## macOS

### Quick Start - Intel (x86_64)
1. **Download** `sprat-gui-v0.2.5-macos-intel-x86_64.dmg`
2. **Open** the DMG file (double-click)
3. **Drag** `sprat-gui` to the `Applications` folder
4. **Launch** from Applications folder or Launchpad

### Quick Start - Apple Silicon (ARM64)
1. **Download** `sprat-gui-v0.2.5-macos-arm64.dmg`
2. **Open** the DMG file (double-click)
3. **Drag** `sprat-gui` to the `Applications` folder
4. **Launch** from Applications folder or Launchpad

### System Requirements
- **OS**: macOS 10.15 Catalina or later
- **CPU**: Intel x86_64 or Apple Silicon (M1/M2/M3)
- **RAM**: 2 GB minimum (4 GB recommended)
- **Storage**: ~50 MB available
- **Notes**: First launch may show security prompt; click "Open" to proceed

### Troubleshooting macOS
- **"Cannot verify developer" warning**: Right-click the app → Open
- **Application won't start**: Try running from Terminal for error messages
- **CLI tools not found**: Use Settings → Install CLI Tools → Choose Install

---

## Linux

### Quick Start
1. **Download** `sprat-gui-v0.2.5-linux-x86_64.tar.gz`
2. **Extract** the archive:
   ```bash
   tar xzf sprat-gui-v0.2.5-linux-x86_64.tar.gz
   cd sprat-gui
   ```
3. **Run** the application:
   ```bash
   ./sprat-gui
   ```

### System Requirements
- **OS**: Ubuntu 20.04 LTS, Fedora 35+, Arch, or compatible
- **CPU**: x86_64 processor
- **RAM**: 2 GB minimum (4 GB recommended)
- **Storage**: ~20 MB available
- **Dependencies**: Qt6 libraries (usually pre-installed)

### Installing Qt6 (if needed)
If you encounter "libQt6Core.so not found" errors:

**Ubuntu/Debian**:
```bash
sudo apt-get install qt6-base libqt6core6 libqt6gui6 libqt6widgets6
```

**Fedora**:
```bash
sudo dnf install qt6-qtbase qt6-qtbase-gui
```

**Arch**:
```bash
sudo pacman -S qt6-base
```

### Desktop Integration (Optional)
Create a desktop shortcut for easier launching:

```bash
# Create a .desktop file
cat > ~/.local/share/applications/sprat-gui.desktop << 'EOF'
[Desktop Entry]
Type=Application
Name=Sprat GUI
Exec=/path/to/sprat-gui/sprat-gui
Icon=sprat-gui
Categories=Graphics;Utility;
EOF

# Update desktop database
update-desktop-database ~/.local/share/applications/
```

### Troubleshooting Linux
- **"sprat-gui: command not found"**: Run with `./sprat-gui` from the directory
- **Qt libraries missing**: Install Qt6 packages for your distribution (see above)
- **File permissions**: Ensure the binary is executable: `chmod +x sprat-gui`
- **CLI tools missing**: Use Settings → Install CLI Tools

---

## Web Demo (No Installation)

### Access the Web Version
Simply visit: **https://sprat-gui.itch.io**

No installation or downloads required. Runs directly in your browser.

### Supported Browsers
- Chrome/Chromium (recommended)
- Firefox
- Safari
- Edge

### Features Available in Web Demo
- ✅ Full sprite sheet layout editing
- ✅ Pivot and marker editing
- ✅ Timeline creation and preview
- ✅ Project save/load
- ⚠️ Frame detection (limited by browser permissions)
- ⚠️ GIF/video export (best on desktop version)

### Limitations
- File uploads limited to browser memory
- No external tool support (ImageMagick, FFmpeg)
- Best experience with desktop version for heavy projects

---

## Optional Dependencies

These tools add features but are not required for basic functionality:

### ImageMagick (for GIF export)
**Linux**:
```bash
sudo apt-get install imagemagick          # Ubuntu/Debian
sudo dnf install ImageMagick              # Fedora
sudo pacman -S imagemagick                # Arch
```

**macOS**:
```bash
brew install imagemagick
```

**Windows**:
Download from https://imagemagick.org/script/download.php#windows

### FFmpeg (for video export)
**Linux**:
```bash
sudo apt-get install ffmpeg               # Ubuntu/Debian
sudo dnf install ffmpeg                   # Fedora
sudo pacman -S ffmpeg                     # Arch
```

**macOS**:
```bash
brew install ffmpeg
```

**Windows**:
Download from https://ffmpeg.org/download.html

---

## First Launch Setup

### 1. Verify CLI Tools
On first launch, you'll see "CLI missing" in the toolbar if sprat-cli is not found.

**Options**:
- Click **Settings** and manually set paths to sprat-cli binaries
- Click **Install CLI Tools** and let the app download and build them
- Click **Provide Path** and select a directory containing the CLI tools

### 2. Verify Installation
Once CLI tools are ready:
- Toolbar will show "CLI ready (vX.X.X)"
- You can now load image folders and create layouts

### 3. Optional Export Tool Setup
If you see "Export disabled" in the Animation panel:
- Install ImageMagick (for GIF) and/or FFmpeg (for video)
- Restart the app
- Export will become available

---

## Building from Source

If you prefer to build from source instead of using pre-built binaries:

### Requirements
- Git
- CMake 3.16+
- C++17 compiler (GCC, Clang, MSVC)
- Qt6 development libraries
- LibArchive development libraries

### Build Steps
```bash
# Clone the repository
git clone https://github.com/pedroac/sprat-gui.git
cd sprat-gui

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run
./build/sprat-gui
```

See the [README.md](README.md) for detailed build instructions per platform.

---

## Uninstallation

### Windows
1. Navigate to the extracted folder
2. Delete the `sprat-gui` directory
3. No registry entries or system files to clean

### macOS
1. Drag `sprat-gui` from Applications to Trash
2. Empty Trash

### Linux
1. Delete the extracted `sprat-gui` directory
2. Remove desktop shortcut if created:
   ```bash
   rm ~/.local/share/applications/sprat-gui.desktop
   ```

### Web Demo
Nothing to uninstall. Simply stop visiting the website.

---

## Getting Help

### Common Issues

**Q: "CLI tools not found" on startup**
A: See "First Launch Setup" section above.

**Q: Application crashes on load**
A: Check that you have sufficient RAM and disk space. See troubleshooting for your platform above.

**Q: Can't find where I extracted the files**
A: Check your Downloads folder or the location where you extracted the ZIP/TAR.GZ.

**Q: Export features are disabled**
A: Install ImageMagick (GIF) and/or FFmpeg (video) for your platform (see above).

### Additional Resources
- **GitHub Issues**: https://github.com/pedroac/sprat-gui/issues
- **README**: Full feature documentation and keyboard shortcuts
- **In-App Help**: Settings dialog and tooltip information

---

## Next Steps

After installation:
1. Open **Settings** to verify CLI tools are ready
2. Read the [README.md](README.md) **Quick Start** section
3. Load your first image folder and experiment!

Enjoy using sprat-gui! 🎨
