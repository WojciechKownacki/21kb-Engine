# Android audio lifecycle

The Android activity requests audio focus when the scene has an audio backend and
the activity is resumed. The native scene owner thread combines the activity,
window focus and audio-focus state before changing playback. Java callbacks only
publish focus state; they never access the scene or the audio graph.

Transient focus loss, ducking requests and backgrounding suspend device output.
The graph retains its sources, voices, cursors and explicit user pause state.
Focus restoration restarts the existing device without replaying sounds or
creating duplicate voices. Permanent loss remains suspended until a subsequent
activity activation requests focus again. A denied or delayed request leaves
output suspended.

`AudioPlayback::SetPlatformSuspended` is an owner-thread platform API. Backends
return whether they applied the requested state. The audio backend reports
`PlaybackSuspended` separately from `NoPlaybackDevice`; scene updates do not
perform device-loss cleanup while platform suspension is active. Offline PCM
reads also reject requests during suspension. Explicit device reinitialization
retains the suspension policy but otherwise keeps its existing reset semantics.

Runtime logs report focus requests, focus changes, output-route additions and
removals, suspension/resume, device generation and callback frame counts.
Failed device transitions produce an error and are retried by the owner thread.

## Verification

The native audio runtime suite includes `platform-suspend` for deterministic
offline transport and `platform-suspend-device` for an actual OS device. They
check frozen cursors, retained resources, idempotent transitions and user-paused
sources and voices. The device test fails when no real output is available.

`sources/game/tests/android/prepare_audio_fixture.py` authors and cooks a scene
with a UI transport control, a tone source and two independently controlled
voices. Package its project with the Android CLI pipeline.

Build the Android input test APK to provide the test-only competing focus
receiver. Run `sources/game/tests/android/run_audio_tests.py` with `--adb`,
`--serial`, `--apk`, `--focus-apk`, `--application-id` and `--output`. It exercises
actual OS focus interruption, ducking, foreground restoration and background
cycles with a user-paused source. The competing request must be permitted by the
device's OS focus policy; a denied request fails the check.

Measurements come from the native mixer and OS device callbacks, not a
microphone. Reports distinguish physical devices from emulators and inventory
available output routes. Route inventory alone does not qualify switching to
unavailable wired or wireless devices. Device settings modified by the test are
restored during cleanup.
