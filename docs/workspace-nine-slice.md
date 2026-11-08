# Nine-Slice Workspace

Define stretchable regions for UI elements that scale without distortion.

**Keyboard**: Alt+N

## What It Does

Nine-slices let you create **buttons, panels, and borders that stretch** without blurring or distorting.

Think of it like cutting a sprite into a 3×3 grid:
- **Corners** stay the same size (no stretch)
- **Edges** stretch in one direction
- **Center** stretches in both directions

## When Do You Need Nine-Slices?

You have a **button sprite** that needs to be different sizes:
- 100×50 pixels
- 200×80 pixels
- Any size

Without nine-slice: It stretches unevenly, looks bad.

With nine-slice: Corners stay crisp, edges and center stretch smoothly.

## Add a Sprite

1. Click **Add** (bottom-left)
2. A picker opens
3. Tick a sprite
4. Click **Add**

The sprite appears in the left list with default slice lines at image thirds.

## Set the Slice Lines

Use the **Insets** spinboxes (right panel):
- **L** (Left) — pixels from the left edge
- **T** (Top) — pixels from the top edge
- **R** (Right) — pixels from the right edge
- **B** (Bottom) — pixels from the bottom edge

Or **drag the handles** on the canvas:
- Four edges around the sprite
- Four corners in the middle (pull diagonally to resize both at once)

## Fill Modes

For each direction (horizontal and vertical), choose how it stretches:

**Stretch** — Smooth stretch (most common)

**Repeat** — Tile the edge (repeating pattern)

**Mirror** — Tile in reverse (good for symmetrical patterns)

**Fixed** — Don't stretch (keeps original size)

## Preview

The **Target Size** spinboxes (right panel) let you see how it looks at different sizes:
- Change **W** (width) and **H** (height)
- The preview updates on the canvas
- See if it stretches nicely

Or **drag the handles** on the canvas to resize interactively.

## Grid and Colorize

**Grid** — Shows pixel grid (helps with exact placement)

**Colorize** — Shows regions with different colors:
- Green corners (don't stretch)
- Blue edges (stretch in one direction)
- Yellow center (stretches both ways)

## Remove from Nine-Slice

1. Click the sprite in the left list
2. Click **Remove** (or press Delete)
3. The sprite is no longer nine-sliced (but insets are saved, so you can add it back later)

## Export

The nine-slice definitions are **automatically included** in your export. Your game engine reads them and stretches the sprites correctly.

## Tips

- **Don't overlap**: Make sure L + R doesn't exceed the sprite width, and T + B doesn't exceed the height
- **Preview first**: Always test at a few different sizes before exporting
- **Symmetric**: For buttons, use the same insets on opposite sides (L = R, T = B)
- **Undo**: Changes are undoable

## Next Steps

- **Done with nine-slices?** Go to [Export](workspace-export.md)

---

**See also**: [Shortcuts](shortcuts.md)
