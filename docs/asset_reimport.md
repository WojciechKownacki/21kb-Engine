# Reimporting assets

A model that was imported once can be imported again from its changed source file. The engine replaces the imported files in place, keeps the asset id and path (so scenes and materials that reference it keep working), and does not overwrite textures or materials the author edited since the import unless asked to.

For saved Blender scenes, the [Blender bridge](blender_workflow.md) exports static
models and watches for changes, using these same conflict policies and backups.

## Import record

Every import of a model writes a sidecar next to the asset: `<asset>.importrecord`. It stores:

- the source file name and its content hash,
- the import option flags and the import transform,
- the hash of every file the import produced (mesh container, textures, materials).

A reimport compares the files on disk with these hashes. A file whose hash differs from the recorded one was edited after the import.

## Change kinds

`PlanImportChanges` classifies each produced asset:

| Kind | Meaning |
| --- | --- |
| Added | The new import produces an asset that did not exist. |
| Changed | The import produces new content and the file on disk is still the imported one. |
| Unchanged | The new import produces the same content. |
| LocalOverride | The file on disk was edited since the last import and the source also changed it. |
| Removed | The earlier import produced the asset and the new one does not. It is reported, never deleted. |

## Conflict policy

`AssetImportOptions::conflictPolicy` decides what happens to LocalOverride assets:

- `Report` (default): nothing is written. The result is a failure that names the edited assets.
- `KeepLocal`: edited assets are left as they are, everything else is updated.
- `Overwrite`: edited assets are replaced. The old files are kept in a backup first.

`AssetImportOptions::dryRun` plans the change and reports it without writing anything.

## Backups and undo

Before a reimport replaces or removes files it copies them (with their `.meta` siblings) to `<project>/Saved/ImportBackups/<id>/` together with a manifest. `AssetImportBackups::Restore` puts the files back and removes the files the reimport created. `List` returns the backups newest first.

## Command line

```
kb_cli import --reimport <asset> <source> [--on-conflict report|keep-local|overwrite] [--diff]
kb_cli import --list-backups
kb_cli import --undo <backup id>
```

`--diff` plans and prints the changes without writing. Repeating the same import over existing files (static or skeletal models) goes through the same guard, so a plain re-run of an import also refuses to overwrite edited materials unless `--on-conflict` says otherwise.

Explicit reimport also accepts skeletal meshes. The replacement source may have a
different filename: the selected mesh keeps its asset path and identity. Geometry
and animation previews compare serialized output, including changes in external
buffers. Local mesh or animation edits are reported before publication. Keeping
such an edit keeps the coupled model unchanged; overwriting creates an undo backup.

## Editor

Project Files, context menu of an imported model:

- Reimport... asks for the changed source file. If textures or materials were edited since the import a dialog offers Keep my edits, Overwrite or Cancel.
- Undo Last Reimport restores the newest backup. It is shown while a backup exists.

The editor automation scenario ops `reimport_asset` (`asset`, `source`, `on_conflict`, `dry_run`, `expect`, `conflicts`) and `undo_reimport` drive the same code; `AssetReimportScenario.json` covers unchanged, changed, edited-material conflict, keep, overwrite and undo.

## Limits

- The editor menu supports static and skeletal models. A skeletal replacement must contain a mesh; animation-only sources use the animation import workflow.
- Edits are detected per file; there is no per-property merge of materials.
- The source path is asked for each time; it is not stored.
- Undo Last Reimport is not part of the editor's Ctrl+Z history.
