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
and hardware WebGL2 through bgfx: nineteen checks, one native UI pixel capture,
and ten intermediate PCM blocks in each fade. Steady RMS must remain within 2%
of the analytic signal, with frequency within 30 Hz for 2048-frame windows.
All phases retain one audio context and playing source intent.

Transition interruption, overlapping transitions, curve continuity when
retargeting, editor output equivalence, physical output, and other device/backend
combinations remain separate verification work.
