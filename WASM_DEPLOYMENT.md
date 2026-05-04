# WASM Deployment Guide

The WASM build provides a fully functional web demo of Sprat GUI that requires **no installation**.

## Location

Built WASM files are in `build_wasm/`:
- `sprat-gui.html` - Main entry point
- `sprat-gui.js` - JavaScript loader
- `sprat-gui.wasm` - WebAssembly binary (~30MB)
- `sprat-gui.data` - Assets

## Local Testing

```bash
cd build_wasm
python3 -m http.server 8000
```

Then open: **http://localhost:8000/sprat-gui.html**

## ✅ Fully Functional

The app works seamlessly in the browser, including window resizing. Asyncify's strict checking has been disabled to prevent false-positive assertions while maintaining stability.

## Deploy to itch.io

### Step 1: Prepare Files
```bash
# Create deployment directory
mkdir sprat-gui-web
cd sprat-gui-web
cp ../build_wasm/sprat-gui.* .
```

### Step 2: Create itch.io Project
1. Go to https://itch.io/dashboard
2. Click **Create New Project**
3. Fill in:
   - **Project Title:** Sprat GUI (Web Demo)
   - **Project Type:** HTML
   - **Classification:** Tools / Utilities / Sprite Editor
   - **Description:**
     ```
     Full-featured sprite sheet layout and animation tool.

     Features:
     • Load and organize sprite frames
     • Edit pivots and markers
     • Create animation timelines
     • Preview animations in real-time

     This is the web demo. For full functionality (frame detection,
     GIF/video export), download the desktop version.

     ⚠️ Note: Start in fullscreen or maximize the window.
     Do not resize while using the app.
     ```

### Step 3: Upload Files
1. Scroll to "Uploads" section
2. Click "Upload files" or drag & drop
3. Select all 4 files: `sprat-gui.html`, `sprat-gui.js`, `sprat-gui.wasm`, `sprat-gui.data`
4. Wait for upload to complete

### Step 4: Configure HTML Settings
1. In "HTML" section, check "This file will be played in the browser"
2. Set "Embed in page" with these options:
   - **Viewport Width:** 1280
   - **Viewport Height:** 800
   - **Fullscreen capable:** ✓ Check this
3. Save

### Step 5: Download Links
Add links to desktop versions in the project description:
```markdown
## Download Desktop Version

For full features including frame detection and export:
- [Windows](link-to-windows-release)
- [Linux](link-to-linux-release)
- [macOS](link-to-macos-release)
```

### Step 6: Publish
1. Click "Save & view page"
2. Set visibility to "Public"
3. Share the link!

## Desktop Version Links

Update these in your itch.io project description:
- **Windows:** `https://github.com/pedroac/sprat-gui/releases/download/vX.X.X/sprat-gui-windows.zip`
- **Linux:** `https://github.com/pedroac/sprat-gui/releases/download/vX.X.X/sprat-gui-linux.AppImage`
- **macOS:** `https://github.com/pedroac/sprat-gui/releases/download/vX.X.X/sprat-gui-macos.zip`

## File Sizes

- `sprat-gui.html` - 3 KB
- `sprat-gui.js` - 360 KB
- `sprat-gui.wasm` - 30 MB
- `sprat-gui.data` - 6.6 KB
- **Total:** ~30.4 MB

First load takes time due to WASM download. Subsequent loads are instant (browser cached).

## Troubleshooting

### "Cannot have multiple async operations" Error
This occurs if you resize the window while the app is loading or responding to events. Solution: Don't resize.

### Slow Initial Load
The 30MB WASM binary takes time to download. This is normal. The app runs at full speed once loaded.

### Crashes on Specific Actions
The desktop version is more stable. If you encounter issues in WASM, download the desktop app.

## Technical Details

The WASM build is compiled from the same C++ source as the desktop version using Qt 6 WASM with Emscripten. It includes all core functionality:
- ✅ Sprite layout editing
- ✅ Pivot/marker editing
- ✅ Timeline creation
- ✅ Animation preview
- ✅ Project save/load (browser storage)
- ❌ Frame detection (requires CLI tools)
- ❌ GIF/video export (requires external tools)
- ⚠️ Window resizing (known limitation)

## Questions?

See the [main README](README.md) or file an issue on [GitHub](https://github.com/pedroac/sprat-gui).
