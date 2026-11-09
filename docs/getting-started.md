# Getting Started with Sprat GUI

Build a sprite sheet and animation in 5 minutes.

> **Prefer pictures?** See the [Visual Guide](visual-guide.md) for a step-by-step walkthrough with screenshots.

## 1. Install and Launch

**Download** from [itch.io](https://pedroac.itch.io/sprat) (or try the [web version](https://sprat-gui.itch.io)):

- **Linux**: Make the AppImage executable and run it
- **Windows**: Extract the ZIP and run `sprat-gui.exe`
- **macOS**: Drag the app to Applications

On first launch, the app will ask to install or locate the sprat-cli tools. Click **Install** and wait a few seconds.

## 2. Load Your Sprites

**Option A: Folder of images**
- Drag a folder of PNG/JPG files onto the window, or use **File → Add Source Folder...**
- They appear in the Sprites list (left side)

**Option B: Single sprite sheet**
- Drag a sprite sheet onto the window
- A dialog will auto-detect the frames
- Click **Accept** to load them

## 3. Place Pivots (Origin Points)

The **Sprites** workspace (Alt+S) is open by default.

1. Click a sprite in the list (left side)
2. Its image appears on the right
3. **Click where you want the origin point** (the pivot) — usually the center or bottom of the sprite

That's it! The pivot is placed. You can adjust it later.

## 4. Pack Into an Atlas

Switch to **Atlases** (Alt+A). Your sprites are already packed and ready to export. If you have many sprites, you can split them into separate atlases here (not needed for most projects).

## 5. Build an Animation (Optional)

Switch to **Frame Animation** (Alt+F).

1. Click **Add Timeline**
2. A picker opens — tick the sprites you want to animate (in order)
3. Click **Create**
4. In the preview, click **Play** to see it animate

Adjust the **FPS** if it's too fast or slow.

## 6. Export

Press **Alt+X** or go **File → Export**.

1. Pick an **Output Folder** (where to save the files)
2. The app shows a preview of how it will pack
3. Click **Export**

Done! Your atlas image and metadata are ready for your game engine.

---

## Next Steps

- **Place markers** on sprites for collision zones: see [Sprites Workspace](workspace-sprites.md)
- **Create more animations** with auto-naming: see [Animations](workspace-animations.md)
- **Build a cutout puppet** (for character rigs): see [Puppets](workspace-puppets.md)
- **Save your work**: see [Projects](projects.md)

## Need Help?

- **See all keyboard shortcuts**: [Shortcuts](shortcuts.md)
- **Something not working?**: [Troubleshooting](troubleshooting.md)
- Inside the app: **Help → Hotkeys** and **Help → Quick Start**
