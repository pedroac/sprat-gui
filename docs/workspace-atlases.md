# Atlases Workspace

Organize sprites into texture atlases and see how they pack.

**Keyboard**: Alt+A

## What It Does

- Group sprites into **atlases** (texture sheets)
- See exactly how they'll pack visually
- Choose export **profiles** (compression, dilation, etc.)
- Manage which sprites go where

## When Do You Need Multiple Atlases?

**One atlas** (the default):
- Good for most small-to-medium projects
- All sprites packed into one or two texture images

**Multiple atlases**:
- Split by purpose: "UI" atlas, "Characters" atlas, "Environment" atlas
- Split by size limits: Some engines have max texture sizes
- Split by loading: Load atlases on-demand

For most projects, you can ignore this and use **Default**.

## Create an Atlas

1. Left side shows your **Atlases** list
2. Click **+ Add** to create a new one
3. Give it a name

## Move Sprites Into an Atlas

1. Middle panel shows the sprite tree
2. Tick sprites (Ctrl+Click for multiple)
3. **Drag them onto an atlas** in the left panel

Or **right-click a folder** in the tree:
- Create an atlas from the folder (names the atlas after it)
- Move it to an atlas of the same name

## See How It Packs

The **Layout** preview (right side) shows:
- How sprites are packed into texture images
- **Zoom**: Ctrl+Scroll
- **Pan**: Space+Drag or middle-click drag
- **Statistics**: Shows texture size, number of images, padding

The layout rebuilds automatically as you add/remove sprites.

## Choose a Profile

A **profile** controls:
- **Compression** (GPU compression, dilation for artifacts)
- **Output format** (PNG, WebP, AVIF, DDS, etc.)
- **Resolution scaling**

1. Look at the **Profiles** section (right side)
2. Pick from the dropdown (usually `fast` or `default`)

For now, just use the default. See [Profiles](profiles.md) to customize.

## Exclude Sprites

Sprites in the **Excluded** folder aren't packed into any atlas.
- Useful for testing or temporarily removing sprites
- Right-click and **Include** to bring them back

## Excluded Sprites

At the bottom of the atlas list is **Excluded**:
- All sprites here won't export
- Useful to temporarily hide sprites during development
- Bring them back later when you need them

## Tips

- **Keep It Simple**: Unless you have a good reason, stick with one atlas
- **Padding**: Adjust in the profile to add space between sprites (prevents bleed-through)
- **Statistics**: If your atlas is too big, delete unnecessary sprites or split into multiple atlases
- **Undo**: Changes to atlas organization are undoable

## Next: Animate?

Ready to animate sprites? Go to [Frame Animation](workspace-animations.md).

Ready to export? Go to [Export](workspace-export.md).

---

**See also**: [Profiles](profiles.md) | [Shortcuts](shortcuts.md)
