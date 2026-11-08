# Sprites Workspace

Import sprites and set up their pivots and markers.

**Keyboard**: Alt+S

## What It Does

- Import sprites from folders, archives, images, or GIFs
- Place **pivots** (origin/anchor points) on each sprite
- Add **markers** (collision zones, spawn points, etc.)
- See all your sprites in one place

## Import Sprites

**Drag and drop**:
- A folder onto the window → all images load
- A sprite sheet → auto-detect frames
- A GIF file → extract all frames

**From menu**:
- **File → Add Source Folder...** for a folder
- **File → Add Source File...** for an image or GIF
- **File → Add Source URL...** for an image on the web

## Set the Pivot

The pivot is the origin point. Rotation and animation position from here.

1. Click a sprite in the list (left side)
2. The image appears on the right
3. **Click on the image where you want the pivot** — usually center or bottom
4. Done!

**Adjust it**: Click the pivot's crosshair, then click elsewhere on the image.

## Add Markers

Markers are **named points** for game logic: collision zones, spawn points, attachments, etc.

### Add a Marker

1. Right-click on the image
2. Choose **New Marker**
3. Pick a name and type:
   - **Point** — single coordinate
   - **Circle** — center + radius (for collision)
   - **Rectangle** — bounding box
   - **Polygon** — custom shape (for pixel-perfect collision)
4. Place it on the image

### Edit a Marker

- **Move**: Click the marker to select it, then click elsewhere
- **Rename**: Double-click it in the Markers list
- **Delete**: Select it and press Delete

## Visual Helpers

**Onion Skin** — See semi-transparent ghosts of neighboring frames
- Useful for aligning pivots across animation frames
- Toggle in **Settings → Frames Editor**

**Flipbook Mode** — Keeps the pivot stationary while you step through frames
- Makes it obvious if the pivot drifts
- Toggle in **Settings → Frames Editor**

## Exclude Sprites

Don't want a sprite in your final atlas?

1. Right-click it
2. Choose **Exclude**
3. It moves to the **Excluded** folder
4. Right-click and **Include** to bring it back

## Detect Frames Automatically

Dragging a sprite sheet opens a frame detector dialog:

1. The app shows detected frames
2. **Accept** to import them all, or
3. **Right-click a frame** → **Split Mode** to manually divide frames
4. **Click on the image** to draw a splitting line
5. Click **Accept** when done

## Tips

- **Thumbnails**: Images load in the background, so your project opens quickly even if you have hundreds of sprites
- **Search**: Use the search box to filter by name
- **Undo**: Every change can be undone (Ctrl+Z)
- **Next**: Once your sprites are set up, go to [Atlases](workspace-atlases.md) to pack them

## Next: Markers in Puppets?

If you're building a **puppet** (character rig), markers are where parts attach. The Puppet workspace will guide you. See [Puppets](workspace-puppets.md).

---

**See also**: [Shortcuts](shortcuts.md) | [Troubleshooting](troubleshooting.md)
