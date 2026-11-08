# Sources: Loading and Managing Sprites

Understand the different ways to load sprites and keep them in sync.

## What Is a Source?

A **source** is where your sprites come from:
- **Folder** (most common) — Load from `C:\MySprites\` or `~/sprites/`
- **Archive** — ZIP or TAR file
- **Image file** — Single sprite sheet or GIF
- **URL** — Image from the web

## Load a Source

### Folder
**File → Add Source Folder...**
- Pick a folder with PNG/JPG files
- All images in that folder and subfolders load

### Sprite Sheet (with auto-detection)
**Drag and drop** a single image file
- App auto-detects individual frames
- A dialog opens showing detected frames
- Accept, or manually split frames

### GIF File
**File → Add Source File... → pick a GIF**
- All frames extract automatically

### URL
**File → Add Source URL...**
- Paste the URL to an image
- App fetches it

### ZIP/TAR Archive
**File → Add Source Folder... → pick a ZIP/TAR**
- If multiple image folders inside, you pick which one
- App extracts and loads them

## Keep in Sync

You modify your source folder (add/remove images). Should the project update automatically or manually?

**Settings → Atlas Sprites**:

**None** — Don't sync (manual only)

**Manual** — Click **Sync Now** when you want to update

**Watch** — Follow changes automatically (folder only)

Use **Watch** for active development. Use **Manual** or **None** when you're done editing.

## Sync Source to Layout

Source has changed. Update your project from the source.

1. Right-click the source (in Sprites workspace)
2. Choose **Sync Source to Layout**
3. Missing files are restored, new files are added

## Sync Layout to Source

Save your current sprites back to the source file (ZIP/TAR/GIF only).

1. Right-click the source
2. Choose **Sync Layout to Source...**
3. Review the changes (added/updated/deleted files)
4. Click **Confirm**

> Repacks ZIP/TAR files or re-renders GIF files.

## Exclude Sprites from a Source

Don't want a sprite in your atlas?

1. Right-click it in the Sprites workspace
2. Choose **Exclude**
3. It moves to the **Excluded** folder

Right-click to **Include** it again.

## Remove a Source

1. Right-click the source
2. Choose **Remove Source**

Sprites from that source disappear (but are still in the project file if you save, so you can add the source back).

## If the Source File Moves or Deletes

If you saved the project and then moved/deleted the source file:

1. App detects the missing file
2. A dialog offers options:
   - **Recreate** from cached data
   - **Embed** into the project
   - **Locate** (find the file elsewhere)
   - **Remove** (delete the source)

## Tips

- **Keep organized**: Use subfolders in your source folder to group sprites
- **Naming matters**: Names like `character_walk_0`, `character_walk_1` help auto-create animations
- **Backup originals**: Keep source files somewhere safe outside the project
- **Sync carefully**: Be careful when syncing back to avoid overwriting work

## Next Steps

- **Loading sprites?** See [Sprites Workspace](workspace-sprites.md)
- **Save your work?** See [Projects](projects.md)

---

**See also**: [Getting Started](getting-started.md)
