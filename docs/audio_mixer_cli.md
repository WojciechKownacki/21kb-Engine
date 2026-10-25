# Audio mixer authoring through CLI

The `audio-mixer` command uses the engine's shared mixer validation and atomic
asset writer. It works in builds with or without the renderer.

```sh
kb_cli audio-mixer new --project Sample --name Main --bus Output --bus Effects --set Effects.parent=Output --set Output.volume=0.5 --set Effects.volume=0.5
kb_cli audio-mixer show --project Sample --mixer /Game/Audio/Main.kbmixer
kb_cli audio-mixer set --project Sample --mixer /Game/Audio/Main.kbmixer --set Effects.mute=true
kb_cli audio-mixer assign --project Sample --mixer /Game/Audio/Main.kbmixer
```

New mixers default to `Assets/Audio`; `--folder` can select another folder inside
the project's Assets directory. Existing mixers accept a `/Game/` path or a
physical path inside that directory. Creating a mixer never overwrites an asset.

Repeat `--bus Name`, `--remove-bus Name` (editing only), and
`--set Bus.parent|volume|mute=value` to edit a graph in one operation. All additions,
then removals, then property assignments are applied to a candidate before saving.
A parent of `-` routes directly to the implicit master output. Volume is a finite,
nonnegative linear gain; mute accepts `true`, `false`, `1`, or `0`. Bus names may
contain dots; the last dot before `=` separates the property name.

Duplicate buses, cycles, missing parents, invalid values, and dangling snapshot
references reject the operation without modifying the existing mixer file. Edits
preserve existing snapshots. Snapshot creation and transition authoring are not
provided by this command.

Assignment updates the default scene, or the scene selected with `--scene`.
`--snapshot Name` selects an existing snapshot; invalid mixers or missing snapshots
leave the scene file unchanged. `--mixer none` clears both scene selections.
The scene's existing serialization and asset dependencies carry the assignment
through cooking; the command does not activate an audio device.

`kb_cli_tests audio-mixer` checks saved graphs and scene references, invalid edits,
snapshot preservation, and project path restrictions. These tests do not establish
rendered audio equivalence between editor and packaged runtime.
