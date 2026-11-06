# Two-axis animation blending

A `BlendSpace2D` asset (`.kbblend2d`) maps two Float parameters to animation
clips. The Animator samples up to three clips at a shared normalized phase.
It blends generic transforms, skeletal poses, morph channels, named curves
and root motion through the existing Animator binding and ownership rules.

The source format is versioned text:

```text
kbblend2d 1
sample 0 0 "/Game/Animation/Idle.kbanim"
sample 1 0 "/Game/Animation/Forward.kbanim"
sample 0 1 "/Game/Animation/Side.kbanim"
```

References can also be stable asset identities. Coordinates must be finite and
unique, with at most 256 samples. A document with no samples is valid for
authoring and returns no geometric weights; a controller referencing it is
rejected with a diagnostic. One sample produces a constant motion. Collinear
samples interpolate on adjacent segments, including vertical and negative
coordinates. Outside the domain, the query projects onto the nearest boundary.

The retained plan normalizes geometry in double precision and triangulates
the domain. Interior queries use barycentric weights. Geometric ordering fixes
the choice of triangles when the sample storage order changes. Near-degenerate
orientation tests use a tolerance of 64 double-precision machine epsilons in
the normalized domain.

Clips share a phase while the state clock advances using their weighted
duration. Looping must agree across clips; generic and skeletal bindings cannot
be mixed within one space. Missing channels contribute their
bind pose or zero scalar weight. The dominant clip supplies animation events;
ties use the last contribution in the geometric order. Skeletal clips retain
the normal retargeting and binding checks.

Animator Controller version 4 adds a motion row:

```text
blend2D 0 0 "/Game/Animation/Motion.kbblend2d" "MoveX" "MoveY"
```

The first two numbers select the layer and state. Both parameter names must
refer to distinct Float parameters. A state has exactly one motion source:
a clip, a 1D tree, or a BlendSpace2D. Older controller formats remain readable.
The controller retains the space, the space retains its clips, and skeletal
clips retain their skeleton dependencies during cooking. Missing dependencies
and referenced empty spaces fail validation.

## Editing

Choose New BlendSpace2D in Project Files to create an empty asset, or open an
existing asset. The panel shows its triangles, samples and
current weights. Click empty space to change the query, select a row to edit
its coordinates or clip, and use Add Sample or Remove to change the document.
Dragging a point records one Undo step; its scene preview updates on release.
Fit frames the sample domain. Previous and Next page through the sample list.

Select an actor in the hierarchy and press Use Selected to preview the working
asset on that actor. The preview copies the scene and uses a private controller;
project behaviours do not run there. The source scene and asset bytes remain
unchanged until Save. The phase ruler scrubs without emitting crossed events,
and Play / Pause drives the same runtime evaluator used by the player.

Save validates and atomically replaces the asset, then refreshes its runtime
payload. Undo, Redo and Revert operate on the working document. Invalid edits
and incompatible clips preserve the last valid preview. Closing a dirty
document offers Save, Discard and Cancel.

## CLI and MCP

```sh
kb_cli blend-space new --project . --space /Game/Animation/Motion.kbblend2d
kb_cli blend-space add --project . --space /Game/Animation/Motion.kbblend2d --at 0,0 --clip /Game/Animation/Idle.kbanim
kb_cli blend-space add --project . --space /Game/Animation/Motion.kbblend2d --at 1,0 --clip /Game/Animation/Forward.kbanim
kb_cli blend-space add --project . --space /Game/Animation/Motion.kbblend2d --at 0,1 --clip /Game/Animation/Side.kbanim
kb_cli blend-space sample --project . --space /Game/Animation/Motion.kbblend2d --at 0.25,0.25
kb_cli blend-space assign --project . --space /Game/Animation/Motion.kbblend2d --controller /Game/Animation/Movement.kbanimcontroller --state Move --x-param MoveX --y-param MoveY
```

`set --index <0..255>` changes a sample's `--at` and/or `--clip`.
`remove --index <0..255>` removes one sample. `show` reports samples and triangles.
Assignment replaces an existing state's motion and creates missing Float axes;
an existing parameter of another type is rejected. Asset paths are confined to
the project's Assets directory. Writes validate before atomic publication.

Commands emit `21kb.blend-space-2d/v1` JSON. The `blend_space` MCP tool exposes
the same operations with typed arguments, explicit action names, zero-based
indices and the same validation backend.
