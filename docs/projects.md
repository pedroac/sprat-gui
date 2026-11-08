# Projects: Save, Load, and Manage Your Work

Save your work so you can come back to it later.

## What Gets Saved?

A **project** saves:
- All sprites, pivots, and markers
- Atlases and their organization
- Animations and timelines
- Puppet rigs and poses
- Nine-slice definitions
- All your settings and edits

Source files (original images) are **not** saved by default (they stay where they are).

## Save Your Project

**File → Save As...**

1. Pick a folder (anywhere you want)
2. Enter a project name
3. Check **Copy sprite sources** to include images in the project (optional)
4. Click **Save**

The app creates:
- A project folder with `project.spart.json` inside
- An optional `sprites/` folder if you chose to copy images

## Open Your Project

**File → Open Project** (Ctrl+O)

1. Pick the project folder (the one with `project.spart.json`)
2. Click **Open**

Everything loads back: sprites, atlases, animations, edits.

## Open a ZIP Project

If your project is in a ZIP file:

**File → Open ZIP Project**

1. Pick the `.zip` file
2. App extracts and opens it

(You can ZIP a project folder to share it with others.)

## Autosave

The app **automatically saves** every 5 minutes to a recovery file.

If the app crashes:
- Next launch asks to **Restore** or **Discard** the autosaved version
- Click **Restore** to pick up where you left off

## Save Again Later

**File → Save** (Ctrl+S)

Saves to the same project folder. Autosave still happens every 5 minutes.

## What's in the Project Folder?

```
MyProject/
├── project.spart.json     ← All your edits and metadata
├── sprites/               ← Optional: copy of source images
│   ├── character_idle.png
│   ├── character_run_0.png
│   └── ...
└── ...exported files (if you exported from here)
```

Keep this folder somewhere safe. Don't edit the JSON file directly (it's for the app).

## Share Your Project

To share with a teammate:

1. **Save the project**
2. **ZIP the project folder** (`MyProject.zip`)
3. **Send the ZIP**

They can open it with **File → Open ZIP Project**.

If you included sprites (`Copy sprite sources`), they have everything. If you didn't, they need the source images in the same folder.

## Move or Copy a Project

1. Close the project (or the app)
2. Move/copy the project folder wherever you want
3. Open it with **File → Open Project**

The app remembers where everything is.

## Recover Lost Work

**Autosave**:
- The app saves every 5 minutes to a recovery file
- If it crashes, you can restore on next launch

**Manual saves**:
- Always use **File → Save As** after major milestones
- Use **File → Save** (Ctrl+S) frequently while working

## Tips

- **Save often**: Use Ctrl+S after changes
- **Use descriptive names**: "Character_v1", "UI_buttons_final"
- **Keep backups**: ZIP old versions when you finish work
- **Test before sharing**: Open your project in a fresh folder to verify it works
- **Undo is your friend**: Use Ctrl+Z if you make mistakes

## Cloud Storage

You can save your project to Dropbox, Google Drive, OneDrive, etc.:

1. Save project to a folder in cloud storage
2. Work on it locally
3. Cloud storage syncs automatically

## Next Steps

- **Working on sprites?** See [Sprites Workspace](workspace-sprites.md)
- **Ready to export?** See [Export](workspace-export.md)

---

**See also**: [Getting Started](getting-started.md) | [Troubleshooting](troubleshooting.md)
