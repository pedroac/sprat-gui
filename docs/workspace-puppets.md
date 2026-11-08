# Puppets Workspace

Build character rigs and animate them using poses.

**Keyboard**: Alt+P

## What Is a Puppet?

A **puppet** is a **character made from sprite parts** that you can pose and animate.

**Example**: A character with a head, body, arms, and legs — each part is a separate sprite. You arrange them, pose them, and create animations without drawing new frames.

## When to Use Puppets

✓ **Cutout-style characters** (2D parts assembled)  
✓ **UI avatars** (face + accessories)  
✓ **Monsters** with interchangeable parts  
✓ **When you want to reuse poses** across animations

✗ **Hand-drawn frame-by-frame animation** (use Timeline instead)

## Create a Puppet

1. Click **Add** → **New Puppet**
2. Give it a name
3. A window opens with a sprite picker on the right
4. **Drag sprites onto "Parts"** (left side):
   - Drag a sprite → adds it as a new part
   - Drag a folder → creates one part showing all sprites in it (useful for clothing variants)
5. **Add Part** → creates an empty part (assign sprites later)
6. Name your parts (e.g., "head", "body", "arm_left")
7. Click **Create**

Done! Your puppet appears on the canvas.

## Arrange Parts on the Canvas

1. Click a part to select it
2. **Drag it** to move it
3. **Arrow keys** to nudge by 1 pixel (Shift+arrows for 10 pixels)

The parts are arranged in **draw order** — first part in the list is drawn at the back.

To change order:
- **Drag a part up/down** in the tree (left side), or
- **Right-click** → **Bring to Front**, **Send to Back**, etc.

## Create Poses

A **pose** is a specific arrangement of parts. You combine poses to create animations.

1. Below the canvas is the **Poses** strip (thumbnails)
2. Click **Add Pose** to create a new one
3. Move parts around on the canvas
4. Each change updates the pose automatically

**Rest** (the first pose) is special — the default position. New parts start here.

## Animate with Poses

1. Switch to **Animations** tab (below canvas)
2. Click **Add** to create a sequence
3. Right-click to **Add Pose** (pick from your saved poses)
4. Each step in the sequence is a frame (hold for N frames)
5. Click **Play** to preview

Different parts can move at different speeds within the same animation.

## Change Which Sprite a Part Shows

Parts can show different sprites (variants).

1. Click a part in the tree
2. Under **Shows** (right panel), tick a variant
3. It changes on the canvas and in the selected pose

Useful for clothing, expressions, and equipment changes.

## Attach Parts Together

If one part should follow another (like an arm following a shoulder), you can **attach** it.

This is for advanced rigs. For simple puppets, arrange parts by hand.

To attach:
- Drag a part's **pivot ring** (small circle in the middle) onto another part's marker
- The part "hangs" from that marker and follows it

To detach:
- Drag it away from the marker

## Tips

- **Test poses first**: Make sure your poses look right before animating
- **Keep it simple**: Start with 3-5 poses, not 30
- **Variants**: Use variants for clothing, expressions, and equipment
- **Draw order matters**: List body parts front-to-back (back leg, body, front leg, arm)
- **Undo**: Changes are undoable

## Advanced: Cutout Rigging

For complex character rigs with **IK (inverse kinematics)** — arms that bend naturally — see the [Advanced Puppet Guide](workspace-puppets-advanced.md) (coming soon).

For now, stick to simple pose-based animation.

## Next Steps

- **Ready to animate?** Go to the **Animations** tab (below canvas)
- **Export your puppet?** Go to [Export](workspace-export.md)

---

**See also**: [Frame Animation](workspace-animations.md) | [Shortcuts](shortcuts.md)
