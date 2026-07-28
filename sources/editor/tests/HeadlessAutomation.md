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
| `discover_assets` | none |
| `import_asset` | `source` project-relative path, `destination` virtual folder |
| `create_entity` | `id`; optional `name` |
| `create_mesh_entity` | `id`, `asset` alias or virtual path |
| `select_entity` | `entity` |
| `add_component` | `entity`, `component` |
| `set_property`, `assert_property` | `entity`, `component`, `property`, `value`; assertion optionally `tolerance` |
| `assert_entity` | `entity`; optional `exists` |
| `assert_component` | `entity`, `component`; optional `exists` |
| `create_asset` | `id`, `type`, `folder`; types: `lua_script`, `input_action`, `input_axis`, `input_context`, `material`, `material_function`, `material_graph`, `material_type` |
| `configure_input_action` | `asset`, `name`, `value_type` |
| `configure_input_mapping` | `context`, `index`, `key`; optional `scale` |
| `activate_input_context` | `asset`; optional `priority` |
| `attach_script` | `entity`, `asset` |
| `open_asset` | `asset` alias or virtual path |
| `new_scene` | none |
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
| `capture_runtime` | `checkpoint`; requires Play Mode and writes a PNG from the production GPU readback path |
| `snapshot` | `kind`, `checkpoint`; kinds: `console`, `inspector_tree` |
| `assert_console` | `contains` |
| `assert_no_errors` | none |

`write_file` plus `discover_assets`, `attach_script`, `play`, input operations,
and `step` is the universal runtime channel: the scenario may author arbitrary
Lua and exercise the same registered library, plugins, scene systems, and
backends as Play Mode. Editor authoring is exercised through semantic
operations and the production Inspector pointer/text/key handlers.
