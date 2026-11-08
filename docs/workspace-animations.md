# Frame Animation Workspace

Create, preview, and export sprite animations.

**Keyboard**: Alt+F

## What It Does

- Build **timelines** (animation sequences)
- Preview animations in real-time
- Set speed (FPS) and loop mode
- Export to GIF or video

## Create a Timeline

1. Click **Add Timeline** (button next to the name field)
2. A picker opens
3. **Tick sprites in order** (the order they animate in)
4. Click **Create**

The new timeline appears in the list.

## Auto-Create from Naming

Have sprites named like `walk_0`, `walk_1`, `walk_2`?

1. Right-click the timeline list
2. Choose **Auto-create Timelines...**
3. Tick the folders that hold your sprites
4. Click **Create**

The app creates one timeline per folder, automatically sorted by the frame number.

**Supported patterns**:
- `walk_0`, `walk_01`, `walk_2`
- `walk (0)`, `walk [1]`
- `walk-0`, `walk 1`

## Edit a Timeline

Click a timeline to select it.

**Rename**: Double-click the name or edit in the right panel

**Add frames**:
- Right-click a frame → **Add Frames Before/After**
- Right-click empty space → **Add Frames...** (at the end)

**Reorder frames**: Drag them in the list

**Remove a frame**: Right-click → **Delete**

**Duplicate a frame**: Right-click → **Duplicate**

## Animation Settings

### FPS (Frames Per Second)
How fast the animation plays. Drag the slider or type a number.
- `10 FPS` = slow and choppy
- `12-15 FPS` = typical sprite animation
- `24 FPS` = smoother

### Loop Mode

**Loop** — Repeat forever

**Once** — Play once and stop on the last frame

**Ping-Pong** — Forward, then backward, repeat

**Ping-Pong Once** — Forward, then backward, then stop

## Preview

1. Click **Play** to see it animate
2. Use **Step** to move frame-by-frame
3. **Pause** and **Stop** to control playback
4. Zoom with **Ctrl+Scroll**, pan with **Space+Drag**

## Export to GIF or Video

1. Right-click the preview → **Save Animation...**
2. Choose a tool:
   - **GIF** (ImageMagick) — static image, small file, no sound
   - **WebP** (ImageMagick) — smaller than GIF
   - **Video** (FFmpeg) — larger files, good for testing in engines

3. Set the **output folder** and **filename**
4. Click **Export**

The file saves. Open it to verify.

> **Note**: You need ImageMagick or FFmpeg installed.
> - Linux: `sudo apt install imagemagick ffmpeg`
> - Windows/macOS: Download from [imagemagick.org](https://imagemagick.org) or [ffmpeg.org](https://ffmpeg.org)

## Tips

- **Preview first**: Always preview before exporting
- **Slow it down**: Reduce FPS if it looks jerky
- **Frame order**: Use **Auto-create** if your filenames are consistent
- **Undo**: Changes are undoable
- **Multiple atlases?**: Use the **Atlas** dropdown (top-right) to pick which atlas's timelines to edit

## Aliases (Advanced)

If multiple timelines should play the same way, you can create an **alias**. (See [Shortcuts](shortcuts.md) for the key.) Most projects don't need this.

## Next Steps

- **Puppet animation?** Go to [Puppets](workspace-puppets.md)
- **Ready to export?** Go to [Export](workspace-export.md)

---

**See also**: [Getting Started](getting-started.md) | [Shortcuts](shortcuts.md)
