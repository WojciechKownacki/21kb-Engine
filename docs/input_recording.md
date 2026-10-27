# Input recording and replay

`CaptureInputFrame` snapshots keyboard, pointer, touch, text, focus and all four
controller slots together with the duration of that input tick. `ApplyInputFrame`
restores the snapshot; `ReplayInputRecording` evaluates the same mapping and
trigger code as live input, in recorded order. Device slots remain distinct.
Use the same input mapping assets and initial action state when comparing runs.
This reproduces input action evaluation, not deterministic world, physics,
network or asynchronous asset execution.

The `.21kbinputrecording` binary format is versioned. Version 5 preserves touch
cancellation; versions 3 and 4 remain readable. Unknown versions, trailing or
truncated data, invalid key kinds or slots, duplicate controls or contacts,
invalid Unicode and nonfinite coordinates, values or durations fail loading.
Durations must be nonnegative. Encoding invalid snapshots returns no bytes;
writing them fails without replacing an existing recording.

The headless scene runner consumes the same recording API:

```sh
kb_cli run --project Sample --input-replay session.21kbinputrecording --report input
kb_cli run --project Sample --input-replay session.21kbinputrecording --frames 120 --input-record prefix.21kbinputrecording --report input
kb_cli run --project Sample --frames 60 --input-record idle.21kbinputrecording --report input
```

Replay defaults to the recording's frame count. An explicit `--frames` selects
a prefix and cannot exceed its length. Each snapshot is applied before the
scene update, using its recorded duration; `--dt` cannot override it. Project
and entity input mappings are activated as in the packaged player. `--realtime`
paces replay by cumulative recorded durations; timeout and cancellation remain
cooperative. Audio probes currently require fixed-duration input and reject
replay explicitly.

Recording captures the runner's actual pre-update device state. A headless run
without replay does not collect a physical keyboard or controller; idle input
stays idle. A stopped run writes only snapshots for its completed ticks. A
zero-tick recording is valid storage but cannot drive a replay run. File write
failures fail the command. The `21kb.input-execution/v1` JSON report distinguishes
requested and completed ticks, simulated seconds, replay and confirmed recording
writes; logs remain on stderr. MCP `run_scene` exposes the same operation through
`inputReplay`, `inputRecord` and `report: "input"`.
