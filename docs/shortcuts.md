# Keyboard and pointer controls

In the app, **Help → Hotkeys** lists every shortcut as your platform shows it (`⌘` on macOS). See also the [user guide](user-guide.md).

## General Application
- `Ctrl + S`: Save project.
- `Ctrl + Z`: Undo (layout changes, pivot/marker edits, project state, timelines, groups, source management).
- `Ctrl + Y` (Windows) or `Ctrl + Shift + Z` (Linux, macOS): Redo.
- `Ctrl + O`: Open project. `Ctrl + E`: Export again.
- `Alt + A` / `Alt + S` / `Alt + N` / `Alt + F` / `Alt + P` / `Alt + X`: Atlases, Sprites, Nine-Slice, Frame Animation, Puppets and Exportation workspaces.
- `Ctrl + V`: Paste / import image from clipboard.
- `Ctrl + +` / `Ctrl + =`: Zoom In.
- `Ctrl + -`: Zoom Out.
- `Ctrl + 1`: Reset zoom to 100%.
- `Ctrl + 0`: Fit to screen.

## Navigator (Sprite Navigator)
- `Delete`: Remove/exclude selected sprites.
- `Ctrl+Click`: Toggle a sprite's checkbox.
- `Shift+Click`: Check a range of sprites from the last clicked item.
- **Right-click** a folder/group in the Atlases workspace: context menu for creating or moving atlases.

## Navigation & Selection (Canvases / Lists)
- `Space (Hold) + Mouse Drag`: Pan (move) the view.
- `Ctrl + A`: Select all items.
- `Arrow Keys`: Navigate selection.
- `Shift + Arrow Keys`: Extend selection while navigating.
- `Delete` / `Backspace`: Remove selected items.

## Layout Canvas
- `Alt (Hold)`: Temporarily enable **Split Mode**.
- `Home` / `End`: Jump to the first / last frame in the current row.

## Selected Frame Editor (Preview)
- `Right Click` on pivot/marker: Opens context actions.
- `Delete` / `Backspace`: Remove selected polygon vertex or marker.
- `Ctrl + Wheel`: Zoom preview.
- `Space + Drag` or `Middle Mouse Drag`: Pan preview.

## Nine-Slice Editor Canvas
- `Ctrl + Wheel`: Zoom canvas.
- `Space + Drag` or `Middle Mouse Drag`: Pan canvas.
- Drag a slice line: adjust the corresponding inset.
- Drag the right/bottom/corner resize handle: change the target preview size.

## Puppet Workspace
- `F2`: Rename the selected puppet, part or variant.
- `Delete`: Remove the selected puppet, part or variant.
- Drag a part on the canvas: move it (one undo step per drag).
- Drag the selected part's round handle: turn it. Drag an attached part: pull it, turning the parts it hangs from (`Shift`+drag: move its joint). Only on a pose frame; saved in that pose.
- Drag the selected part's pivot ring: attach it to the marker you drop it on, or to a new marker where you drop it on a part, or place it where you drop it (`Shift`: always place it). `Escape` cancels. Double-click it: hang the part where it is from a new marker on the part under its pivot (a menu asks which when several or none are).
- `Arrow Keys` on the canvas: move the selected part 1 px (`Shift`: 10 px).
- `Delete` / `Backspace` in the pose strip or an animation's steps: delete the selected pose or step. `Left` / `Right` in the steps: select the previous or next step.
- `Ctrl + Wheel`: Zoom canvas.
- `Space + Drag` or `Middle Mouse Drag`: Pan canvas.

## Animation Test Preview
- `Wheel`: Scroll preview viewport.
- `Ctrl + Wheel`: Change animation preview zoom.
- `Space + Left Drag` or `Middle Mouse Drag`: Pan preview.
- `Right Click`: Export/copy frame menu.

## Frame Detection Dialog
- `Alt (Hold)`: Temporarily enable **Split Mode**.
- `Delete` / `Backspace`: Delete selected detected frames.
- `Ctrl + 1`: Zoom 100%.
- `Ctrl + 0`: Zoom Fit.
- `Ctrl + Mouse Wheel`: Zoom at mouse position.

## Animation Timeline List
- `Delete` / `Backspace`: Remove selected timeline(s).
- `Ctrl + A`: Select all timelines.
