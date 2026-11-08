# Profiles: Compression and Output Options

Control how your atlases are packed and exported.

## What Is a Profile?

A **profile** is a set of settings that controls:
- **Image format** (PNG, WebP, AVIF, DDS, etc.)
- **Compression** (GPU compression, dilation)
- **Resolution scaling** (original size, 2x smaller, etc.)
- **Optimization** (deduplication)

Use profiles to create atlases for different targets: mobile, web, high-end games, etc.

## Default Profiles

The app comes with pre-built profiles:

**fast** — Quick export, minimal compression (good for testing)

**default** — Balanced quality and file size (good for most projects)

**optimized** — Maximum compression (for mobile/web)

## Use a Profile

When exporting:

1. Go to **Atlases** workspace (Alt+A)
2. **Right panel** shows **Profiles**
3. Pick from the dropdown

The layout preview updates to show how the atlas will pack with that profile.

## Create a Custom Profile

1. Go **Settings → Manage Profiles**
2. Click **Create Profile**
3. Name it (e.g., "Mobile", "WebGL")

Now configure:

### Image Format
- **PNG** — Lossless, larger files (good for most things)
- **WebP** — Smaller files, good quality (modern browsers/engines)
- **AVIF** — Smallest files, but slower to encode
- **DDS** — GPU compression (saves VRAM)

### Dilation (Artifact Reduction)
Adds a border around trimmed sprites to prevent **dark halos** when scaled/rotated.

- **0** — No dilation (faster)
- **2-4** — Good for most sprites
- **8+** — Heavy artifacts, use higher values

If sprites look dark around the edges when rotated, increase dilation.

### GPU Compression
For **DDS format only**:
- **None** — Use PNG instead
- **DXT1** — RGB only (no transparency), very small
- **DXT5** — RGBA (with transparency), good quality

Only useful if your engine supports DDS.

### Resolution Scaling
- **1x** — Original size
- **2x** — Half size (smaller files, faster on mobile)
- **3x** — Third size (for very large atlases)

If your sprites are too big, scale them down. The app resizes them automatically.

### Save Changes

Click **Save** when done. The profile is ready to use.

## Global Deduplication

If you have **duplicate sprites** (same image twice), deduplication removes them:

**Settings** → scroll down to **Deduplication**

**Exact** — Byte-for-byte identical (safest)

**Perceptual** — Visually similar (removes near-duplicates)

Turn on **Exact** to save file size. The app creates **aliases** (pointers to the same sprite).

## Tips

- **Test before shipping**: Always preview with your target profile before final export
- **Mobile?** Use **optimized** profile with 2x scaling
- **Web?** Use **WebP** format for smaller files
- **High-end?** Use **PNG** or **AVIF** with no scaling
- **DDS?** Only if your engine is Unreal or DirectX-based

## Example Profiles

**Mobile (Android/iOS)**:
- Format: WebP
- Dilation: 4
- Scaling: 2x
- Deduplication: Exact

**Web (HTML5)**:
- Format: WebP
- Dilation: 2
- Scaling: 1x (or 2x if sprites are large)
- Deduplication: Exact

**Desktop (Unity/Godot)**:
- Format: PNG
- Dilation: 2
- Scaling: 1x
- Deduplication: Exact

## Next Steps

- **Ready to export?** See [Export](workspace-export.md)
- **Atlases too big?** Reduce resolution scaling or enable compression

---

**See also**: [Atlases Workspace](workspace-atlases.md) | [Export](workspace-export.md)
