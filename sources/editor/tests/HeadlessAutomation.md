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

Paths accepted by `write_file`, `save_scene`, and `open_scene` are relative to
the isolated project and cannot escape it. Entity and asset `id` values are
scenario-local aliases.

| Operation | Required fields |
| --- | --- |
| `write_file` | `path`, `content` |
| `copy_fixture` | `source` relative to the scenario directory, project-relative `destination`; copies arbitrary binary fixtures |
| `assert_file` | project-relative `path`; optional `exists`, `min_size`, `contains` |
| `discover_assets` | none |
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
| `set_property`, `assert_property` | `entity`, `component`, `property`, `value`; assertion optionally `tolerance` |
| `assert_entity` | `entity`; optional `exists` |
| `assert_component` | `entity`, `component`; optional `exists` |
| `assert_parent` | `entity`, `parent` |
| `assert_asset` | virtual `path`; optional `type`, `exists` |
| `create_asset` | `id`, `type`, `folder`; types: `lua_script`, `input_action`, `input_axis`, `input_context`, `material`, `material_function`, `material_graph`, `material_type` |
| `copy_asset`, `move_asset` | `asset`, destination virtual folder |
| `delete_asset` | `asset` |
| `assign_asset` | `entity`, `asset`, `role`; roles: `mesh`, `material`, `audio_clip`, `animator_controller`, `script` |
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
| `capture` | `panel`, `checkpoint`; panels: `hierarchy`, `scene`, `inspector`, `assets`, `console`, `project_settings`, `script_editor`, `plugins`, `material_editor` |
| `capture_runtime` | `checkpoint`; optional `require_non_uniform`; requires Play Mode and writes a PNG from the production GPU readback path |
| `snapshot` | `kind`, `checkpoint`; kinds: `console`, `inspector_tree` |
| `assert_console` | `contains`; optional `category`, `level`, `count_at_least` |
| `clear_console` | none |
| `assert_no_errors` | none |

`write_file` plus `discover_assets`, `attach_script`, `play`, input operations,
and `step` is the universal runtime channel: the scenario may author arbitrary
Lua and exercise the same registered library, plugins, scene systems, and
backends as Play Mode. Editor authoring is exercised through semantic
operations and the production Inspector pointer/text/key handlers.

## Honest boundary

The runner can verify engine state, editor documents, imported binary fixtures,
all Lua-exposed runtime APIs, plugin attachment, material graph cook, and GPU
readback without a visible window. It does not fabricate physical hardware:
audible speaker output, controller vibration, driver overlays, and operating
system file/colour dialogs require the corresponding real device or the
existing visible-host editor suites. A scenario can assert whether those live
backends are actually present with `assert_backend`; absence is reported, not
silently replaced by a fake backend.
