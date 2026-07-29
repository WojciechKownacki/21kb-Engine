# Headless editor automation

Run a versioned scenario through the production editor:

```powershell
ctest --test-dir build -C Debug -R kb_editor_headless_automation_scenario --output-on-failure
```

For a task-specific run, invoke `kb_editor.exe` with
`--selftest-scenario <json> --selftest-root <directory> --selftest-task
<LIB-id-name>`. On Windows, wait for the GUI-subsystem process
(`Start-Process -Wait`) when invoking it outside CTest.

The document root is:

```json
{
  "schema": "21kb.editor-automation/v1",
  "continueOnFailure": false,
  "steps": []
}
```

Every run creates an isolated project under
`<root>/<task>/workspace`, copies the input scenario, and writes a report,
manifest, JSONL trace, console/Inspector snapshots, and requested BMP panel
captures under `<root>/<task>`. The repository-level `SelfTest` directory is
ignored by Git.

## Operations

Paths accepted by `write_file`, `write_pcm_wave`, `save_scene`, and
`open_scene` are relative to the isolated project and cannot escape it.
`write_file` expands `{{PROJECT_ROOT}}`, `{{ARTIFACT_ROOT}}` and
`{{ASSET_ID:/Game/path.asset}}` (an already discovered asset) in its content
to forward-slash absolute paths, allowing authored scripts to persist their
own output inside the task folder. Entity and asset `id` values are
scenario-local aliases.

| Operation | Required fields |
| --- | --- |
| `write_file` | `path`, `content` |
| `write_pcm_wave` | project-relative `path`; optional `duration_ms` (1..10000), `sample_rate` (8000..48000), `frequency_hz`, `amplitude` (0..1); authors a valid mono 16-bit PCM fixture |
| `configure_physics_layers` | project-relative `path`, two distinct indices/names (`first_layer`, `first_name`, `second_layer`, `second_name`) and `interact`; writes the binary asset and sets the project-wide physics-layers reference; use `reload_scene` before Play Mode |
| `select_project_settings_category` | `category`: `inputs`, `graphics`, or `physics`; selects the visible Project Settings page before a panel capture |
| `set_joint_connection` | `entity` (a Joint owner) and `connected_entity`; assigns the Joint's referenced entity and marks it modified for scene persistence |
| `assert_joint_connection` | `entity` and `connected_entity`; verifies the Joint still targets that entity, including after scene reload |
| `set_inspector_scroll` | `position`: `top` or `bottom`; moves the Inspector to the requested end before a capture |
| `set_physics_debug_draw` | `enabled`; toggles the scene's physics debug wireframes before a Scene panel capture |
| `assert_physics_debug_line_count` | `count`; verifies the exact production wireframe line count currently emitted by the scene |
| `copy_fixture` | `source` relative to the scenario directory, project-relative `destination`; copies arbitrary binary fixtures |
| `assert_file` | project-relative `path`; optional `exists`, `min_size`, `contains` |
| `assert_game_flow` | Exercises the production `GameInstance` checkpoint, pause/resume, valid/invalid scene transitions, win/lose and restart lifecycle against the active project descriptor. |
| `init_agent_project` | Runs the production `kb_cli init-agent` provisioning path for the active project, including loadable starter and sample gameplay assets. |
| `assert_first_release_network_model` | Verifies the compiled first-release network contract is offline-only, rejects session opening and exposes no `Network.*` script API. |
| `assert_network_object_lifecycle` | Verifies authority ownership, duplicate rejection and spawn/despawn lifecycle through the production `NetworkObjects` owner. |
| `assert_replication_schema` | Verifies a versioned replication wire contract, quantization/dequantization, delta selection and rejection of an incompatible same-version schema. |
| `assert_rpc_contract` | Verifies reliable/unreliable RPC ownership for client-to-server and server-to-client directions, including spoofing rejection. |
| `assert_network_variable` | Verifies typed network-variable callbacks, monotonic revisions, stale update rejection and saturated-revision safety without automatic object replication. |
| `assert_network_prediction` | Verifies input commands, ordered snapshot interpolation, and reconciliation on position, velocity, or acknowledgement divergence. |
| `assert_network_budget` | Verifies deterministic tick accumulation, invalid-rate rejection, exact queue capacity and packet backpressure. |
| `assert_network_security` | Verifies server ownership validation, spoofing rejection, payload and rate limits, and declared-length deserialization bounds. |
| `discover_assets` | none |
| `unload_asset` | asset alias or virtual path; force-loads then unloads the live `AssetManager` entry so a running system must reacquire it |
| `import_asset` | `source` project-relative path, `destination` virtual folder |
| `create_entity` | `id`; optional `name` |
| `create_mesh_entity` | `id`, `asset` alias or virtual path |
| `duplicate_entity` | `entity`, result `id`; optional `name` |
| `delete_entity` | `entity` |
| `rename_entity` | `entity`, `name`; uses the production Hierarchy rename transaction |
| `reparent_entity` | `entity`, `parent` |
| `create_prefab` | `entity`, project-relative `path` |
| `instantiate_prefab` | result `id`, project-relative `path`, `virtual_path`; optional `parent` or complete `x`,`y`,`z`, and optional `name` |
| `select_entity` | `entity` |
  | `add_component` | `entity`, `component` |
  | `set_animator_root_motion_owner`, `assert_animator_root_motion_owner` | Animator `entity`, owner: `none`, `animator`, `character_controller`, or `rigidbody`; setter uses the production Inspector command and rejects incompatible ownership |
  | `set_property`, `assert_property` | `entity`, `component`, `property`, `value`; assertion optionally `tolerance` |
| `assert_entity` | `entity`; optional `exists` |
| `assert_name` | `entity`, expected string `value`; verifies the live entity name |
| `assert_component` | `entity`, `component`; optional `exists` |
| `assert_ui_element` | `entity`, numeric `element`; optional `exists`, `visible`, `kind`; queries the live runtime UI tree in Play Mode |
| `assert_parent` | `entity`, `parent` |
| `assert_asset` | virtual `path`; optional `type`, `exists` |
| `create_asset` | `id`, `type`, `folder`; types: `lua_script`, `input_action`, `input_axis`, `input_context`, `material`, `material_function`, `material_graph`, `material_type` |
| `copy_asset`, `move_asset` | `asset`, destination virtual folder |
| `delete_asset` | `asset` |
| `assign_asset` | `entity`, `asset`, `role`; roles: `mesh`, `material`, `audio_clip`, `animator_controller`, `script` |
| `assign_material_slot`, `assert_material_slot` | Mesh Renderer `entity`, material `asset`, integer `slot`; assigns or verifies the production per-slot material override |
| `set_material`, `assert_material` | `asset`, `property`, `value`; numeric factors plus `double_sided` and `alpha_mode` |
| `save_material` | `asset` |
| `find_material_node` | graph `asset`, node alias `id`, serialized node `kind` |
| `add_material_node` | graph `asset`, node alias `id`, serialized node `kind`, integer `x`,`y` |
| `connect_material_nodes` | graph `asset`, node aliases `from`,`to`, `from_pin`,`to_pin` |
| `wait_material_cook` | optional `timeout_ms`; succeeds only for a fresh or cache-valid production GPU program |
| `configure_input_action` | `asset`, `name`, `value_type` |
| `configure_input_mapping` | `context`, `index`, `key`; optional `scale` |
| `activate_input_context` | `asset`; optional `priority` |
| `set_project_input_enabled` | `enabled` |
| `set_plugin`, `assert_plugin` | catalog plugin `id`, `enabled` |
| `assert_backend` | `backend` (`physics`, `audio`, `haptics`, `basic_lighting`), `available`; checks the live scene, not only project configuration |
| `attach_script` | `entity`, `asset` |
| `open_asset` | `asset` alias or virtual path |
| `new_scene`, `reload_scene` | none |
| `save_scene`, `open_scene` | `path` |
| `undo`, `redo`, `play`, `stop` | none |
| `key` | `key`, `down`; optional `gamepad` |
| `analog` | `key`, `value`; optional `gamepad` |
| `pointer` | `x`, `y` |
| `touch` | `points` array of `{id,x,y,phase}` where phase is `began`, `moved`, or `ended` |
| `focus` | `focused` |
| `gamepad_connected` | `index`, `connected` |
| `step` | `frames`; optional `dt` |
| `inspector_pointer` | `action` (`down`, `drag`, `up`); `x`,`y` for down/drag |
| `inspector_text` | `text` |
| `inspector_key` | `key` (`enter`, `escape`, `backspace`, `delete`, `left`, `right`, `home`, `end`) |
| `capture` | `panel`, `checkpoint`; panels: `hierarchy`, `scene`, `inspector`, `assets`, `console`, `project_settings`, `script_editor`, `plugins`, `material_editor`. `script_editor` captures the real child editor renderer and loaded document, not only panel chrome. |
| `capture_runtime` | `checkpoint`; optional `require_non_uniform`; requires Play Mode and writes a PNG from the production GPU readback path |
| `snapshot` | `kind`, `checkpoint`; kinds: `console`, `inspector_tree` |
| `assert_console` | `contains`; optional `category`, `level`, `count_at_least` |
| `clear_console` | none |
| `assert_no_errors` | none |

`write_file` plus `discover_assets`, `attach_script`, `play`, input operations,
and `step` is the universal runtime channel: the scenario may author arbitrary
Lua and exercise the same registered library, plugins, scene systems, and
backends as Play Mode. The visible editor and the runner call the same
`EditorSceneContext` play-mode tick for scheduler execution, system faults,
script diagnostics, render invalidation, and quit handling. Editor authoring
is exercised through semantic operations and the production Inspector
pointer/text/key handlers.

## Honest boundary

The runner can verify engine state, editor documents, imported binary fixtures,
all Lua-exposed runtime APIs, plugin attachment, material graph cook, and GPU
readback without a visible window. Its Play host registers the same physical
Win32 XInput haptics backend as the visible editor; capability and actuator
calls therefore report the attached device honestly. It does not fabricate
physical hardware: audible speaker output, observable controller vibration,
driver overlays, and operating system file/colour dialogs require the
corresponding real device or the existing visible-host editor suites. A
scenario can assert whether those live backends are actually registered with
`assert_backend`; device capability remains a separate runtime query.
