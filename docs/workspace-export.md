# Export Workspace

Configure and export your atlases and animations.

**Keyboard**: Alt+X  
**Quick export**: Ctrl+E (re-exports to last location)

## What It Does

- See a preview of your packed atlases
- Choose output format and compression
- Export all data at once

## Open Export

1. Press **Alt+X**, or
2. Go **File → Export**

The workspace opens full-screen showing a preview (left) and settings (right).

## Preview Your Atlases

**Left panel** shows how your atlases will be packed:
- Sprites arranged in texture images
- **Zoom**: Ctrl+Scroll
- **Pan**: Space+Drag or middle-click drag
- **Atlas selector** (top): Switch between atlases

Check that nothing is overlapping and everything fits.

## Choose Output Folder

**Right panel**:
1. Click **Select...** to choose where to save
2. Pick a folder (creates a new one if needed)

The app will create:
- Atlas images (`atlas_0.png`, `atlas_1.png`, etc.)
- Metadata files for your engine (`atlas.json`, `atlas.godot`, etc.)
- Animation data

## Choose Output Format

**Metadata Format** (Tranform) — what file types to create:

- **json** — Standard JSON (most compatible)
- **raw** — Sprat's native text format
- **godot** — Godot Engine format
- **unity** — Unity format
- **aseprite** — Aseprite format
- Others (depending on sprat-cli)

Pick the one your game engine uses. If unsure, try **json**.

## Scale Filter

For **scaling atlases down** (if they're too big):

**None** — Export at original size (usually this)

**2x** — Scale to 1/2 size (smallest file)

**3x** — Scale to 1/3 size

Usually leave this at **None**.

## Export

1. Settings look good?
2. Click **Export**

The app runs the export pipeline. You'll see:
- Progress bar (exporting sprites)
- Status message when done

The exported files are in your output folder.

## Open Exported Files

- **Atlas images**: Open in any image viewer
- **Metadata**: Open in a text editor to verify format
- **Animations**: Check that frame lists are correct

## Re-Export

To export again to the **same folder**:
- Press **Ctrl+E**, or
- Go **File → Export**
- The app remembers your last location

## Tips

- **Always preview**: Check the preview before exporting
- **Test in your engine**: Open the exported files in your game engine to verify they work
- **Atlases too big?** Use [Profiles](profiles.md) to reduce resolution or enable compression
- **Output format wrong?** Pick a different metadata format and re-export

## Troubleshooting

**"Export failed"** — Check:
- Output folder is writable
- Disk has free space
- No sprites are excluded unintentionally

**Animations not showing up** — Make sure they're in the same atlas as the sprites they use

**Puppets not exporting** — Puppets are part of the atlas; make sure the atlas is set to export

## Next Steps

- **Tweaking compression?** See [Profiles](profiles.md)
- **Need a different format?** See [Profiles](profiles.md)

---

**See also**: [Getting Started](getting-started.md) | [Profiles](profiles.md)
