# Puppet Workspace Plan

Plan for authoring sprat-cli puppets (cutout rigs) in sprat-gui. Decided 2026-11-04, scope
narrowed 2026-11-06 to rigs first; not implemented yet.

The format and its meaning are defined by sprat-cli: see `../sprat-cli/README.md`, sections
"Puppets" and "Playing a puppet", and the `puppet`/`part`/`joint`/`pose`/`set`/`sequence`/`step`
records of the raw format (version 8). sprat-cli only describes puppets; the GUI authors them.

Puppets need sprat-cli v0.32.x; `DEPENDENCIES` pins v0.32.4.

## Decisions

1. **One Puppet workspace** (a tab between Frame Animation and Exportation, `Alt+P`, and a
   Workspace menu entry), laid out like the other workspaces: puppets tree on the left, canvas
   in the middle, the selected item's properties on the right.
2. **v1 authors rigs only**: puppets, parts, variants, attachments and positions. Poses and
   sequences come later, and so does how they fit in the workspace (mode buttons or tree
   folders). Until then they are imported, kept and exported unchanged, so a hand-written
   `puppets.txt` never loses them.
3. **Both import and export of `puppets.txt`.** People who write it by hand or with scripts must
   be able to open and edit it in the GUI.
4. **No constraint solving.** A part's first `attach` is its parent; further `attach` lines
   (constraints, closed loops) are imported, kept, shown and exported, but not enforced.
5. **Parts are listed in draw order**, not nested by attachment. Draw order and attachment are
   independent in sprat-cli (a parent may be drawn in front of its child), so dragging a part
   row only ever changes draw order; the attachment shows as a label on the row.
6. **Dropping a group of similar sprites on a part always shows a menu** ("As one animation" /
   "As N sprite variants") with a **"Remember my choice"** checkbox.

## Layout

```
┌───────────────────────────┬──────────────────────────────┬──────────────────────┐
│ Puppets             [+]   │                              │ Selected item        │
│ ▾ pinocchio   (Main)      │        Puppet canvas         │ (puppet, part or     │
│    ▸ face     at 0,-40    │                              │  variant properties) │
│    ▾ mouth    ↳ face:nose │             ┼  ← puppet      │                      │
│       ★ smile  sprite     │                 origin       │                      │
│         angry  sprite     │                              │                      │
│         talk   animation  │                              │                      │
│    ▸ eye      ↳ face:eye  │                              │                      │
│    ▸ pupil    ↳ eye:socket│                              │                      │
│    ▸ fly      at 24,-70   │                              │                      │
├───────────────────────────┴──────────────────────────────┴──────────────────────┤
│ Problems (spratconvert's checks)                                                │
└─────────────────────────────────────────────────────────────────────────────────┘
```

- **Tree**: puppet › part › variant. A puppet row shows its atlas; parts are in draw order
  (first = back); a part row shows `↳ PARENT:MARKER` when attached or `at X,Y` when positioned;
  ★ marks the rest variant (the first). Drag parts to reorder the drawing, drag variants to
  reorder them (the first is the rest variant).
- **Canvas**: the puppet drawn in its rest pose (every part's first variant, angle 0, offset 0),
  zoomable like the other canvases.
  - A large cross at the **puppet origin** (0,0, the puppet's pivot), always shown.
  - A small cross at the **selected part's pivot**: its joint point when attached, its sprite
    pivot (the `at:` handle) when positioned.
  - **Attachments**: a thin line from each parent marker to its child's joint, faint for all
    parts and strong for the selected one. For the selected part: the parent marker's shape
    (point, circle, rectangle or polygon), the leeway ring, and an arc for `angle:MIN..MAX`.
    Constraints are drawn dashed.
  - Clicking a part selects it in the tree, and the other way round.
- **Right panel**, by selection:
  - Puppet: name, atlas.
  - Part: name; placement, either
    ```
    Attached to [face ▾] at marker [nose ▾]   using [pivot ▾]
    Leeway [2] px    Angle limits [-10]..[10]  [ ] free
    [Detach → position here]
    ```
    or `at: X, Y` with [Attach to…]; its constraints, each removable; [Add constraint…].
    Marker combos only offer markers every variant has, with ⚠ on ones some variants miss.
  - Variant: name (written as `-> "NAME"` when it differs from the default), thumbnail
    (animations play), [Replace…] (keeps the name), "Show in Sprites workspace".

The workspace also has its own `NavigatorPanel` (like Frame Animation) plus an Animations tab
listing the atlas's timelines, as the source for variants.

## Rules

- **A puppet belongs to one atlas** (`AtlasEntry`): sprites, timelines and the exported layout
  are per atlas. The picker shows only that atlas's sprites and timelines, never hidden or
  excluded ones. A drop from another atlas is refused with a message naming the puppet's atlas.
- **Variants reference sprites and timelines, they don't copy them**: store the sprite path and
  the timeline identity, so renames carry over. Removing a sprite or timeline a variant uses
  asks first and is part of the existing source-removal undo.
- Every add, remove, reorder or replace of a puppet, part, variant or attachment is one undo
  command.
- Names follow sprat-cli: unique puppet names, part names per puppet, variant names per part;
  no empty names.

## Rig authoring

- **Picking sprites/animations for a part:**
  - Drag and drop from the navigator:
    | Drop | On | Result |
    |---|---|---|
    | Sprite(s) | a part (tree row or canvas) | sprite variants |
    | Sprite(s) | empty tree/canvas | new part, named from the common prefix (`mouth-smile`, `mouth-angry` → `mouth`) |
    | Timeline | a part | animation variant |
    | Similar-sprite group (`walk_0..7`) | a part | the group menu (below) |
  - Sprites already drag as `application/x-sprat-sprite` and groups as
    `application/x-sprat-group-name` (`NavigatorTreeWidget`). Timelines need a new
    `application/x-sprat-timeline` drag (`TimelineTreeWidget` only drags internally today).
  - Part context menu and right panel: **Add variants…** uses `SpritePickerDialog`
    (src/App/Workspaces/Shared), extended with an Animations tab rather than a second picker.
    **Marker check**: it knows the markers this part's joints need (as child, e.g. `pivot`; as
    parent, e.g. `nose`, `mouth`, `eye`) and marks candidates ✓ (has all, in every frame for
    animations) or ⚠ (lists missing ones), with a "compatible only" filter.
  - **New puppet from selection**: groups the selected sprites/folder into parts by name prefix
    or subfolder, adds them as variants, and uses existing timelines for numbered sequences.
- **Group drop menu:**
  ```
  Add "walk_0 … walk_7" to part "legs" as:
    ( ) One animation      reuses timeline "walk" (same frames)
    ( ) 8 sprite variants
    [ ] Remember my choice
  ```
  - The remembered choice is an app setting (`sprat.conf`), not project data.
  - Shift+drop always shows the menu; Settings gets "Reset remembered choices".
  - "One animation" with no timeline holding exactly those frames creates one, named after the
    group; creating it and adding the variant are one undo step.
- **Attaching**: drag from the selected part's pivot handle onto another part's marker (markers
  highlight while dragging, the joint snaps on drop), or use the right panel. The child's marker
  must be a point; the parent's may be any shape. A new part with no attachment is positioned
  at the puppet origin.
- **Positioned parts** are dragged on the canvas, or nudged with the arrow keys (done in phase 2).
- **Missing joint markers** are listed in Problems with a jump to that sprite in the Sprites
  workspace (which has the marker editor). Later: "copy this marker to all variants".

## Core pieces

- **Model**: `Puppet` (parts in draw order, variants, attachments incl. constraints, plus poses
  and sequences kept as data) in `models.h`, stored per `AtlasEntry`. Project schema 6;
  `ProjectPayloadCodec` keeps loading older schemas.
- **`PuppetEvaluator`** (`src/Core`, pure function, fully unit-tested):
  `evaluate(puppet) → part transforms` for the rest pose, following "Playing a puppet":
  first variant, angle 0, offset 0, positioned parts at `at:`; an attached part's child marker
  goes on its parent's marker under the parent's transform; a parent is placed before its child
  regardless of draw order. Written so poses can be applied over it later. The canvas uses it.
- **`PuppetTextCodec`** (`src/Project/`): reads and writes hand-written `puppets.txt`.
  sprat-cli's parser lives in `spratconvert_command.cpp` (not its shared `core/`), and desktop
  builds run sprat-cli as executables, so the GUI needs its own. `raw` output can't be imported
  (indexes instead of names, atlas-scaled pixels). Longer term: move the parser into sprat-cli's
  `core/` so the GUI can link it.
- **Problems panel**: runs spratconvert's checks in the background after edits and shows its
  messages, so the GUI never disagrees with the CLI.

## Import

- Entry points: File > Import > Puppets…, dropping a `puppets.txt` on the workspace, and the
  `puppets.txt` in a folder exported as Layout text.
- References are resolved in the puppet's atlas: sprite path (against `root`), name or index;
  `- animation` against its timelines. Unresolved references are kept as ⚠ variants, never
  dropped, so a later export doesn't lose them.
- A puppet name that already exists: ask to replace or rename.
- Comments, blank lines and line order are not kept; the import notice says so.
- Poses, sequences and constraints are kept and exported (decisions 2 and 4).

## Export

- `ProjectSaveService` writes `puppets.txt` and passes `--puppets` to spratconvert.
- The Layout text export (`layout-text`) also writes `puppets.txt`.
- Only the `raw` transform outputs puppets for now: the Export workspace warns when a project
  has puppets and another format is selected.

## Phases

1. **Foundation, no UI** (done): model + schema 6, `PuppetTextCodec` (import/export, round-trip
   test including poses and sequences), export through `ProjectSaveService` and Layout text,
   rest-pose `PuppetEvaluator`, unit tests, an e2e test through sprat-cli.
2. **Workspace shell** (done; the tree lists the active atlas's puppets with an Atlas combo, as
   Frame Animation does, and **Add Sprites...** uses the existing picker until phase 3): tab, `Alt+P`, menu entry; puppets tree (draw order, labels, reorder);
   canvas with origin and pivot crosses; right panel for puppet, part, variant; add/rename/remove
   with undo.
3. **Variants**: drag and drop, group menu, picker with Animations tab and marker check,
   New puppet from selection.
4. **Attachments** (attach by dragging the pivot and in the panel, joint lines, leeway and angle
   limits: done after phase 2; still to do: marker shapes and arcs for the selected part, constraints): attach by drag and in the panel, attachment lines, marker shapes, leeway
   and angle arcs, constraints, positioned parts' `at:` handle; Problems panel; Import.
5. **Poses**: done. A strip under the canvas holds Rest (the rig) and the poses; editing on a
   pose frame changes that pose directly, and a list under the tree ticks the parts it sets
   (others are faint), so poses combine.
6. **Animations**: done. An Animations tab lists the sequences; each shows its steps as
   blocks sized by frames, poses added from its context menu, and plays on the canvas, others
   playing along.
7. **Later**: onion skin of the previous pose, then constraint solving while posing.

Each phase updates README.md (workspace section and hotkeys), CHANGELOG.md and AGENTS.md.
