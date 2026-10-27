# Blender workflow

Save a Blender scene, then export and import it through the normal engine importer:

```sh
python scripts/blender_bridge.py --project Sample --source Art/Model.blend --cli build/bin/kb_cli --blender blender
```

Blender and the renderer-enabled `kb_cli` must be installed. Both executable arguments
accept full paths. The bridge uses Blender's glTF exporter, then calls `kb_cli import`
with material and texture import enabled. This bridge handles static models; scenes
containing an armature are refused with a diagnostic. Use the skeletal importer for
rigged models. Missing Blender produces a JSON diagnostic.

Add `--watch` to reimport whenever the saved `.blend` file changes. Unsaved edits in
Blender are not visible to the bridge. Changes to external textures require saving the
Blender file again. `--destination /Game/Models` selects the engine content folder;
use separate destinations for unrelated sources with the same file name.

The default conflict policy is `report`: an edited engine material is preserved and
the import reports a failure. Resolve the conflict and restart, or pass
`--on-conflict keep-local` to update geometry while keeping edited materials.
`--on-conflict overwrite` explicitly replaces those edits using the importer's normal
backup mechanism. See [asset reimport](asset_reimport.md) for backups and undo.

Each source and destination pair has a generated GLB, its imported asset binding and diagnostic logs under the project's
`Saved/BlenderBridge` directory. Output consists of JSON lines with the schema
`21kb.blender-bridge/v1` and `imported`, `failed` or `cancelled` status. A failed
source revision is not retried continuously; another saved change or a new bridge
invocation retries it. Export does not run embedded Blender auto-execution scripts.

Stop with Ctrl+C or create the path supplied by `--cancel-file`. The bridge terminates
its active subprocess when cancelled or when `--timeout-seconds` expires. Watching
can be bounded with `--max-updates`; zero means unlimited. Concurrent sessions for
the same source and destination are refused. After an unexpected process crash, the diagnostic
identifies the stale lock to remove before restarting.

The integration check uses real headless Blender saves and the public CLI. It changes
geometry during a watch session, verifies the resulting engine mesh, checks material
conflicts and keep-local reimport, runs an engine scene, and tests cancellation:

```sh
python scripts/tests/blender_bridge_integration.py --cli build/bin/kb_cli --blender blender --test-root build/blender-tests
```

CMake registers `kb_blender_bridge` when Blender, Python and the renderer-enabled
tools are available at configuration time. No Blender installation means this
integration test is unavailable, not that Blender integration passed.
