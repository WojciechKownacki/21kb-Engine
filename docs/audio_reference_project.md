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

## Limits

This is offline PCM through the real provider and a hidden Windows player. There is no audio device in it, so it says nothing about
latency, dropouts or memory on a device, about other platforms, or about timed snapshot transitions (those need scene time, which offline
rendering does not advance; `audio_snapshots.md` covers them in the packaged browser and runtime tests). The spatial check is a direction and
distance check, not a calibration of the panning law.
