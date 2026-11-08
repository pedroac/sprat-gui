# Troubleshooting

Common issues and how to fix them.

## CLI Tools Missing

**Problem**: App shows "CLI missing" when you launch it.

**Solution**:
1. App will offer to install automatically — click **Install**
2. Wait a few seconds while it downloads and sets up
3. Done!

**Manual fix**:
- Go **Settings → CLI Tools**
- Point to your own sprat-cli binaries if you have them

## Can't Load a Source Folder

**Problem**: Folder doesn't load, shows error.

**Solution**:
1. Make sure the folder has image files (PNG/JPG)
2. Check that the path doesn't have special characters (use numbers and letters)
3. Right-click the source → **Retry**

**Still stuck?**:
- Try loading a different folder to test
- Restart the app

## Sprites Look Pixelated or Blurry

**Problem**: Imported images look wrong in the editor.

**Likely cause**: The app is scaling them for preview. This is **normal** in the editor.

**When exported**: They'll be fine. Check your final exported image.

## Can't Detect Frames in Sprite Sheet

**Problem**: Auto-detection shows wrong frame boundaries.

**Solution**:
1. After detection dialog opens, right-click a frame
2. Choose **Split Mode**
3. Click on the image to draw splitting lines where you want them
4. Click **Accept**

**Better approach**:
- Use image editing software (Aseprite, Photoshop) to cut the sprites first
- Load each sprite individually

## Animation Preview Is Jerky

**Problem**: Animation stutters or plays too slow/fast.

**Solution**:
1. Check **FPS** setting (right panel, Frame Animation workspace)
2. Try `12` or `15` FPS for typical sprite animation
3. Try `24` for smoother motion
4. Lower numbers = slower, jerky; higher = smoother but faster

**Test in your engine**:
- Export and check how it looks in your actual game
- The preview might differ from engine behavior

## Can't Export to GIF or Video

**Problem**: "Save Animation" button is disabled or export fails.

**Solution**: Install the required tools:

**For GIF export** (ImageMagick):
- Linux: `sudo apt install imagemagick`
- macOS: `brew install imagemagick` (or download from [imagemagick.org](https://imagemagick.org))
- Windows: Download from [imagemagick.org](https://imagemagick.org)

**For Video export** (FFmpeg):
- Linux: `sudo apt install ffmpeg`
- macOS: `brew install ffmpeg` (or download from [ffmpeg.org](https://ffmpeg.org))
- Windows: Download from [ffmpeg.org](https://ffmpeg.org)

After installing, **restart the app**.

## Crash or App Won't Open

**Problem**: App crashes on launch or while working.

**Solution**:
1. Check the autosaved project:
   - Next launch, click **Restore**
   - This loads your last auto-saved work
2. If still crashing, restart your computer
3. Update your graphics drivers (especially if on Windows)

**Report it**: If crashes persist, save a copy of your project and try with a different folder.

## Can't Save Project

**Problem**: "Failed to save" error.

**Likely causes**:
- Destination folder is read-only (e.g., inside Program Files)
- Disk is full
- Network issue (if saving to cloud storage)

**Solution**:
1. Pick a different folder (e.g., Documents, Desktop)
2. Check that disk has free space: `100 GB` or more
3. If using cloud storage, disable sync temporarily

## Project Won't Open

**Problem**: Saved project won't load.

**Check**:
1. You're opening the **project folder** (with `project.spart.json` inside), not a file
2. The folder hasn't moved or been renamed
3. Disk isn't full

**Recover**:
- A backup is auto-saved every 5 minutes
- On next launch, click **Restore** to recover

## Markers or Pivots Disappeared

**Problem**: Pivots or markers vanish from a sprite.

**Likely cause**: You **excluded the sprite** by accident.

**Solution**:
1. Go **Sprites workspace** (Alt+S)
2. Look for **Excluded** node in the sprite tree
3. Right-click the sprite → **Include**
4. It comes back with pivots/markers intact

## Undo Doesn't Work

**Problem**: Ctrl+Z doesn't undo the last change.

**Cause**: Undo only works within the app session. Once you close the app, undo history is lost.

**Solution**:
- Always save your project (**Ctrl+S**) after important changes
- Use autosave (5-minute intervals)

## The App Looks Wrong (UI Layout Broken)

**Problem**: Buttons, text, or windows are misaligned.

**Likely cause**: Your screen scaling is set unusually (e.g., 125% instead of 100%).

**Solution**:
1. Resize the window or use **View** menu to reset layout
2. Restart the app
3. Windows users: Right-click the `.exe` → **Properties** → **Compatibility** → **Disable fullscreen optimizations**

## Performance Issues (Slow, Laggy)

**Problem**: App runs slowly, especially with many sprites.

**Optimizations**:
1. Reduce the number of sprites (exclude unnecessary ones)
2. Scale down large images before loading
3. Close other apps to free RAM
4. Update graphics drivers

**For large projects**:
- Split into multiple smaller projects
- Use multiple atlases

## Web Version Limitations

**Can't use**:
- Frame detection (auto-detect sprite sheet frames)
- GIF/video export (no ImageMagick/FFmpeg in browser)
- Full file access (browser sandbox)

**Solution**: Use the **desktop version** for these features.

## Something Else?

Check:
1. **Help → Hotkeys** — Key reference
2. **Help → Quick Start** — Guided introduction
3. **[Shortcuts](shortcuts.md)** — Full control reference
4. Re-read the relevant [workspace page](getting-started.md)

If you're still stuck, your project is likely autosaved. Save a copy and restart fresh.

---

**See also**: [Getting Started](getting-started.md) | [Projects](projects.md)
