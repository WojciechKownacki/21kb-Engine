# 3D reference workflows

`kb_game_reference_3d_package` creates two projects using public CLI commands,
then runs their source scenes and Windows packages. It generates standard glTF,
OBJ and WAV inputs and Lua behaviour source. It does not write engine scene,
world, controller or imported asset documents directly.

The animated scene imports a two-bone skinned mesh and animation, creates an
Animator Controller, and assigns them through `component set`. A falling rigid
body, floor queries, looping audio rendered to PCM and persistent save values
run alongside the animation. Runtime probes compare bone positions, physics
positions and audio levels between source and cooked playback. Captures at two
times check rendered motion and source/package image equality.

The streamed world imports mesh geometry, places three objects with colliders,
and uses `world migrate` and `world build` to create cells and HLOD proxies.
It removes the original objects from the startup scene and places the world
through a `ContentInstance`:

```text
kb_cli component add --project <project> --entity World --type ContentInstance
kb_cli component set --project <project> --entity World --type ContentInstance --set kind=3 --set assetId=/Game/Worlds/Stream.21kbworld
```

Set `kind` before assigning an asset path. The CLI checks that the asset type
matches the selected source kind and leaves the scene file unchanged on failure.
Kinds are `0` prefab, `1` scene, `2` world fragment, `3` partitioned world and
`4` navigation mesh. Numeric identities remain supported.

A camera with `StreamFocus` moves between distant cells, waits for the original
cell to unload and the destination to become queryable, then returns. The probe
checks live entity membership, collider queries and a saved completion value.
This exercises asynchronous loading and unloading rather than only inspecting
world files.

Both projects are cooked into separate distributions containing `Game.kbpack`,
a renamed player and provider DLLs. The test relocates source projects and import
inputs before launching those players without a project override. It also runs
the packs through the development player to compare screenshots with source
playback. A second process verifies saved data survives restart.

Build `kb_cli`, `kb_cooker`, `kb_game` and the provider modules, then run:

```text
ctest --test-dir <build> -R "^kb_game_reference_3d_package$" --output-on-failure
```

Generated projects, command output, profile files, PNG captures and `result.json`
are retained under `<build>/reference_3d_workflow_tests`. The test requires a
Windows renderer and the shader compiler; it is not a clean-machine installer
test or validation of other target platforms.
