# Audio snapshot verification

Snapshots replace the authored volume of their listed buses. Other buses keep
their authored gain. A runtime bus volume override has precedence over the
snapshot or transition, while authored mute silences the bus subtree. Use the
[mixer CLI](audio_mixer_cli.md) to create snapshot assets and persist the selected
snapshot in a scene.

`sources/game/tests/web/run_game_audio_snapshot_tests.py` uses the standard full
browser fixture arguments. It imports an analytic 440 Hz WAV, creates the mixer
and snapshots through CLI, assigns `Quiet` to the scene, cooks four assets, and
runs the packaged native game. The browser supplies trusted UI clicks; the test
does not bypass activation or synthesize PCM.

The authored parent gain is 0.5 and source volume is 0.25. The child bus has
authored volume 0.5, `Quiet` volume 0.125, and `Loud` volume 1. Expected output:

| State | Combined gain | Approximate PCM RMS |
| --- | ---: | ---: |
| Quiet | 0.015625 | 0.00110 |
| Loud | 0.125 | 0.00884 |
| Runtime child override of 0.5 | 0.0625 | 0.00442 |
| Authored mix without a snapshot | 0.0625 | 0.00442 |

The scenario requires the cooked Quiet selection before activation, then verifies
each state with four consecutive real native PCM blocks. Changing a snapshot
must preserve a runtime override; clearing the override must restore the active
snapshot's gain. Clearing the snapshot must restore the authored mix.

Two transitions configured for 0.5 seconds of scene time fade to Quiet and back
to the authored mix. Each must produce at least three intermediate PCM blocks
between the endpoint levels, moving in the expected direction with at most 5%
adjacent-block variation against that direction. Completion must restore the
expected snapshot selection and steady amplitude. This checks an audible ramp
and its result, rather than completion metadata alone. It does not establish a
precise wall-clock duration or exact interpolation curve.

This scenario passed on desktop Chromium using native miniaudio WebAudio output
and hardware WebGL2 through bgfx, including a native UI pixel capture and
intermediate PCM levels in both fades. Steady RMS must remain within 2%
of the analytic signal, with frequency within 30 Hz for 2048-frame windows.
All phases retain one audio context and playing source intent.

Retargeting a running positive-duration transition now starts at its current
per-bus blend. The last completed snapshot remains selected until the new
transition finishes. Runtime overrides and mute remain stronger layers and are
not captured as underlying snapshot gains. A non-positive duration still applies
immediately; a non-finite duration leaves the running transition unchanged.

The browser scenario also interrupts a two-second fade after 0.3 seconds of
scene time, then retargets again after another 0.3 seconds. Actual PCM must remain
within 75% to 130% of the initial base amplitude across at least sixteen blocks,
then settle back at the authored mix. The previous behavior jumped to the old
target before starting the next fade and fails this signal envelope. The updated
scenario requires twenty-two checks and one native UI GPU capture.

`kb_audio_runtime_tests snapshot-transition` verifies fifteen deterministic
scene-time checkpoints using a real mixer graph and owned WAV memory decoding.
Both parent and child gains vary, so the expected signal is their product.
Each checkpoint requires four consecutive 2048-frame native PCM blocks within
2% of the analytic RMS, with at most eight attempts. The cases include partial
fades, interruption, overlapping retargets, return to authored gains, override
precedence, clearing an override after retargeting, invalid durations, and an
immediate transition. This isolates gain processing from asynchronous file
streaming, which has separate decoder and transport tests.

The native checkpoints passed in Windows Release and ASan builds.
`kb_engine_tests script-audio-mixer` separately checks shared asset IO and script
API state. The runtime-state layout change increments the engine module ABI;
native modules must be rebuilt. `kb_21kb_particle_stage2_tests module-abi` checks
that the module host rejects the previous ABI before calling its creation
function. Its default invocation retains the complete existing test suite.

Exact real-time curve timing, editor output equivalence, physical output, and
other device/backend combinations remain separate verification work.
