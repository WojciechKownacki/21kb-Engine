# Reference audio project

`kb_game_audio_reference_package` builds one small project that uses every part of the audio pipeline the engine claims, renders it to real PCM
through the miniaudio provider, and checks each level against the analytic one. It runs offline through `kb_cli run`, again through the
cooked package in the packaged player, and fails when an expectation is wrong.

## The project

Authored with `kb_cli` only (`sources/game/tests/AudioReferenceProjectTests.py`; the checks are the Lua script `AudioReference.lua`):

| Part | Authored as | Checked as |
|---|---|---|
| Stream | an Audio Source with a 4 second 220 Hz sine (peak 0.4), loop and autoplay, on bus Music | master RMS 0.113 within 3%; the source reports playing |
| Buses | Output (0.8) with Music (0.5), Sfx, Voice and Filtered under it | Music bus peak 0.2, Output and master peak 0.16, the buses nobody feeds are silent |
| DSP | a 4th order low-pass at 55 Hz on bus Filtered, fed by a 220 Hz drone | the drone's peak matches the filter's analytic attenuation (within 25%) and is at least 40 dB down; the chain is installed |
| One-shot | `Audio.Play` of a 0.3 second 880 Hz sine (0.5) on bus Sfx | peak 0.5 on both channels, the voice stops playing and the bus is silent after the clip ends |
| Spatial | an Audio Source 6 units left of the listener, then right, then far away | the near channel is at least 3 times the far one, mirrored when the source moves, quieter beyond the maximum distance |
| Snapshot | a snapshot Quiet with Music at 0.1 | Music peak 0.04 while selected; a runtime bus override (1.0) is stronger than the snapshot (peak 0.4); clearing the override returns to the snapshot and clearing the snapshot returns to the authored mix |

The levels are of the blocks the provider rendered, read through the signal meters (`Audio.Meter`), not computed from the settings.

## What else it checks

- `kb_cli run --report audio --json` now states how the offline run went: `blocks`, `short_blocks` (a block that came back shorter than asked
  for, the offline kind of dropout; the run fails on one), `worst_block_mix_ns` and `realtime_factor` (the mixing time divided by the duration
  of the audio: below 1 means the mix is faster than real time). The test requires four complete blocks, no short block and a factor below 1.
- The same report lists `unavailable_metrics`: `voice_count`, `decoder_memory_bytes` and `device_underruns`, which the miniaudio provider does
  not report, so they are named rather than shown as zero.
- A script that expects a wrong level fails the run with its message (the negative control).
- The package is cooked with no loose assets and the player runs the same script: the clip played by path from the script is kept in the
  package by an Audio Source that names it, because a path inside a script is not a dependency of the scene.

## Native output qualification

Pass `--qualify-device` to `AudioReferenceProjectTests.py` with the same paths as
its registered CTest command. This opt-in mode requires an available OS output
device; a no-device or null backend fails instead of silently skipping. The
shipped player runs the offline signal checks first, then the actual device
callback with streams, a looping one-shot, DSP and timed snapshot transitions.
It closes and reopens the device twice, rejects stale voice handles, and requires
six seconds of mixed callback frames. The authoring project is renamed during
package execution. The output bus is muted during device qualification; child
meters continue to measure the decoded signals.

Budgets are 128 MiB for tracked engine allocation requests, 512 MiB for the
packaged process (50 ms sampled private bytes and OS peak working set), at most 1% of
callbacks exceeding their buffer period, no short PCM blocks, a callback period
at most 100 ms, and observation of the first child signal within 500 ms of audio
clock frames after starting the voice. These are workload qualification limits,
not universal real-time guarantees or an acoustic latency measurement.

`Audio.Diagnostics()` and `AudioDiagnostics::Report(scene)` expose the current
engine generation, backend, sample rate, callback block/frame counts, last period,
worst callback duration, deadline misses and allocation current/peak/count.
Device short-block counts exclude valid idle silence from an unattached graph.
The CLI audio JSON includes these fields under `runtime`. Integer counters are
exact decimal strings. Older or alternative providers can lack this optional
capability without changing their existing playback interface.

`kb_audio_runtime_tests real-device` additionally stops the OS output device,
checks public status, reinitializes, verifies that old voices remain invalid,
and requires zero retained tracked allocations after shutdown. `device-soak`
repeats this lifecycle for 30 minutes. The default suite tests allocator
alignment, rejected overflow realloc, shrink/free and offline engine teardown.

## Limits

The default CTest remains an offline PCM test. Device qualification is explicit
and requires a usable native output endpoint. Engine allocation bytes include
only requests routed through miniaudio's engine allocator; they exclude asset
payloads, STL allocations and driver memory. Callback deadline misses are mix
overruns, not OS/hardware underrun counts. Period and audio-clock onset counters
do not measure end-to-end acoustic latency. This harness targets Windows; it
does not establish other platform support. The spatial check verifies direction
and distance, not a calibrated panning law.
