# Android lifecycle verification

The Android host drains queued `UserStorage` writes on pause, stop and save-state
callbacks. This is a completion barrier for submitted writes, not an automatic
scene snapshot or a disk `fsync`. Callers still inspect each write result and
explicitly save their game state.

Window destruction releases the renderer before the native glue acknowledges
the command. Memory pressure releases renderer resources and prunes unreferenced
asset entries. Referenced scene assets remain available for reconstruction.
Rendering resumes against the current window after the activity resumes.
Configuration changes refresh the surface dimensions and reset frame timing.

Runtime logs identify renderer generations, the first submitted frame of each
generation, and mesh/material/texture cache counts after release. These messages
support lifecycle checks without requiring an editor or a debugger.

## Build and exercise an APK

Build the host CLI, cooker and runtime asset-pack validator first. Set the Android
SDK and Java environment variables and select the test device explicitly with
`ANDROID_SERIAL`. `CMAKE_BUILD_PARALLEL_LEVEL` limits both native compile/link
pools and packaging Gradle workers when specified.

`sources/game/tests/android/prepare_lifecycle_fixture.py` creates and cooks a UI
fixture using the same save/settings script as the browser tests. It accepts
`--cli`, `--cooker`, `--engine-root`, `--shaderc`, `--root`, an optional
`--cook-cache`, and either Android target profile.

Use `scripts/package_game.py` with the generated `Sample/Sample.21kbproject`, an
Android target, `Development`, an output directory, the engine/build roots and
product metadata. Specify a dedicated `--android-application-id` and `--launch`.
The pipeline builds source targets, cooks and signs the asset pack, builds and
verifies the APK, installs it and requires a first rendered frame from the newly
started application process. The probe preserves the device's existing logcat
buffer.

Run `sources/game/tests/android/run_lifecycle_tests.py` with `--adb`, `--serial`,
`--apk`, the same `--application-id`, an output directory and `--storage-flow`.
The test taps the rendered save button, backgrounds the application, sends memory
pressure, resumes twice, kills the background process, and verifies that save and
settings values reload in the replacement process. It also compares screenshots
and requires empty renderer caches after release. The fixture must be static.

`--rotation` additionally exercises both landscape orientations through the
device's window-manager rotation controls. It records and restores the previous
rotation policy. Devices without these controls fail this optional check rather
than silently skipping it.

Reports distinguish physical devices from emulators and record the APK hash,
API level, command exit codes, screenshots and runtime logs. Input-controller,
audio interruption, other GPU backends and emulator qualification are separate
checks; this test does not establish their coverage.
