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
preserve existing snapshots.

Use `--add-snapshot Name` with `new` or `set` to create a snapshot. Select it with
`--snapshot Name` and repeat `--set-snapshot Bus=volume` to add or replace bus
volume overrides. The last `=` separates the bus name from its volume. Editing
also supports `--clear-snapshot-bus Bus` to return that bus to its authored gain,
and `--remove-snapshot Name` to delete a snapshot. Snapshot additions, removals,
override removals, and override writes occur in that order, after bus edits;
the entire candidate is validated before writing. Removing a bus and its
snapshot override in one batch is supported. Invalid snapshot edits leave both
the saved bus graph and snapshot data unchanged. Timed transitions are runtime
state and are not authored by these asset commands.

```sh
kb_cli audio-mixer set --project Sample --mixer /Game/Audio/Main.kbmixer --add-snapshot Quiet --snapshot Quiet --set-snapshot Effects=0.25
kb_cli audio-mixer assign --project Sample --mixer /Game/Audio/Main.kbmixer --snapshot Quiet
```

Assignment updates the default scene, or the scene selected with `--scene`.
`--snapshot Name` selects an existing snapshot; invalid mixers or missing snapshots
leave the scene file unchanged. `--mixer none` clears both scene selections.
The scene's existing serialization and asset dependencies carry the assignment
through cooking; the command does not activate an audio device.

`kb_cli_tests audio-mixer` checks saved graphs and scene references, invalid edits,
snapshot preservation, and project path restrictions. These tests do not establish
rendered audio equivalence between editor and packaged runtime.

The packaged browser scenario `run_game_audio_routing_tests.py` creates two buses
with gains of 0.5 and a source with volume 0.25, assigns the mixer through CLI,
and cooks four assets. With the generated 440 Hz tone, four consecutive native
PCM blocks per phase must match the product gain (RMS about 0.00442). Overriding
the parent and then child gain to 1 must produce RMS about 0.00884 and 0.01767;
clearing each override must restore the authored values. Removing the active
mixer must stop output and explicitly reject a play request on the named bus
with `MixerUnavailable`, rather than routing it to the master output. Restoring
the mixer must resume autoplay at the original amplitude with one audio context.

This scenario passed on desktop Chromium's native audio backend with hardware
WebGL2 rendering through bgfx. The observer reads the real WebAudio output and
requires amplitude within 2% of the analytic signal, frequency within 30 Hz
(2048-frame analysis windows), and silence peaks below 0.00001. A native UI pixel
capture is also required. This verifies the cooked mixer and live runtime
changes; an editor-output comparison, physical speaker output, other browser
backends, and mobile devices remain unverified.

The same scenario also routes the playing source to a child of a muted parent.
Transport must remain playing while the measured output is silent, including
after a gain override on the muted parent. Switching the source to `Output`
must produce the parent-only gain; an empty `outputBus` must use the implicit
master gain. Switching to an undeclared bus must explicitly return `UnknownBus`
and silence output, followed by successful autoplay when the valid child route
is restored. All thirteen phases retain one audio context and require the same
PCM criteria. This covers live source routing changes; it does not establish
gapless switching or runtime replacement of a mixer's graph topology.
