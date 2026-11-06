# Android input tests

## Packaged controller test

`prepare_controller_fixture.py` authors and cooks a scene with six independent
analog actions, face buttons and directional-pad actions for each controller
slot. Package its generated project with `scripts/package_game.py` for Android,
using a dedicated application ID and the normal production player.

Compile `UinputController.c` with the Android NDK for the selected host ABI, for
example with `aarch64-linux-android23-clang -std=c11 -D_GNU_SOURCE -Wall -Wextra
-Werror UinputController.c -o uinput-controller`. Then run:

```sh
python sources/game/tests/android/run_packaged_controller_tests.py --adb adb --serial <authorized-host> --apk <packaged-player.apk> --helper <uinput-controller> --application-id <test-application-id> --output <results-directory>
```

This requires shell access to `/dev/uinput`. The helper registers two OS devices
and sends kernel events only while the packaged player owns focus. The test
reads the cooked Lua scene's public action values, checks independent axes and
buttons, dead zones, held-button disconnection and reconnection with a new OS
device ID, and removes its devices afterward. These are OS virtual controllers;
physical USB and Bluetooth controller mappings require separate hardware tests.

## Input adapter test APK

This APK runs the production Android controller and touch adapters with GameActivity 4.4.2
and Android Games Controller 2.0.2. It uses a transparent, nonfocusable activity
with preview disabled. The CLI registers two OS virtual controllers with `uinput`
and sends Java `MotionEvent` and `KeyEvent` objects through GameActivity's surface
and activity handlers. Native assertions inspect the resulting `InputDeviceState`.
No controller events are injected into other applications.

Build with JDK 21, Android SDK 36, NDK 29.0.14206865 and CMake 4.1.2 installed.
Set `ANDROID_HOME` to the SDK directory, then run from the repository root:

```sh
java -classpath platform/android/gradle/wrapper/gradle-wrapper.jar org.gradle.wrapper.GradleWrapperMain -p sources/game/tests/android --no-daemon assembleDebug
python sources/game/tests/android/run_controller_tests.py --adb adb --serial <authorized-host> --apk sources/game/tests/android/build/outputs/apk/debug/21kb-Controller-Tests-debug.apk --output build/android-controller-results
```

The host must provide the standard shell `uinput` tool and allow test APK
installation. A headless Android emulator can be started with `-no-window` and
`-no-audio`. Missing tools, failed registration, rejected events, timeouts and
failed assertions are failures, never skipped passes.

For a device without shell `uinput`, add `--touch-only` to the Python command. This
runs the production touch adapter, cancellation, orientation and surface lifecycle
checks without registering controllers. Its report explicitly marks controllers
as untested; it does not count as a controller-suite pass. All adb commands use the
explicit `--serial` device, including installation and cleanup.

Use `--ime-only` for the software keyboard scenario on an unlocked host with an
installed IME. This mode uses a visible, focusable activity, opens the OS keyboard,
and sends commits, composition, selection edits and editor actions through its
real InputConnection. OS Back is injected into that test activity. Native checks
cover Unicode prefill, deferred composition, repeated commits, one submit/cancel,
reopening, long paste and deletion. The runner restores the original stay-awake
setting and returns an originally sleeping host to sleep, including after failure.
It does not register controllers or verify the full packaged renderer.

The test checks independent sticks, triggers, buttons and hats, hardware flat
regions, finite/range conversion, focus suppression, disconnection with held
inputs, slot isolation, reconnection with a new device ID, and native shutdown.
The engine exposes four controller slots. Extra library slots do not alias them.
Android axes pointing down are converted to the engine's positive-up convention.

The touchscreen scenario dispatches two-contact events through the actual
GameActivity SurfaceView touch handler and reads the native adapter's snapshots.
It holds one event batch to check that a move cannot erase a begin or an earlier
release, then verifies cancel, focus loss, stale moves, an actual orientation
change and coordinates against the resized surface. Removing and reattaching the
SurfaceView exercises native window termination and initialization. Explicit frame
consumption checks that terminal samples persist until consumed and then clear.
Native boundary cases cover malformed counts, IDs, indices, nonfinite coordinates,
unrelated sources, lifecycle commands, reused IDs and the contact capacity.
The same scenario also verifies the production edge-to-edge window policy: the
native surface must cover the decor window, and native safe-area insets must match
actual system insets before and after orientation and surface recreation. The
transparent, nonfocusable test window does not exercise a real software keyboard.
The CLI requires a separate `touch=PASS` instrumentation result.

An emulator run verifies the Android SDK/JNI/native event path with OS virtual
devices and touchscreen events delivered to the test surface. Physical controller
mappings, wireless transport, touchscreen drivers, OS navigation gestures and a
fully packaged game's lifecycle require their own device tests.
