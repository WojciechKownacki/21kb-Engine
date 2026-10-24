# Android controller input

The Android player routes GameActivity key and motion events through Android Games
Controller 2.0.2, linked statically from its AndroidX prefab. The host initializes
the library on its native thread, reports activity start/stop, processes device
connection callbacks, and publishes a fresh controller snapshot before scene input
evaluation. The Android package uses the NDK's shared C++ runtime.

Four independent controller slots expose both sticks, both triggers, face buttons,
shoulders, stick clicks, Start/Back and the D-pad, including joystick hat axes.
Controller identity comes from Android device IDs through the controller library.
Disconnecting a controller clears its buttons and axes without moving another
active controller into its slot. A reconnecting device receives a free slot.

Stick values use each device's reported flat region and are bounded to `[-1, 1]`.
Y points up in the engine. Trigger values are bounded to `[0, 1]`; nonfinite samples
become zero. Focus loss suppresses controller values while retaining physical
connection information. Keyboard and touchscreen events continue through their
existing handlers when the controller library does not consume them.

The [CLI integration test](../sources/game/tests/android/README.md) builds an APK
using the production adapter and real GameActivity/JNI event queues. Its Android
35 x86_64 emulator scenario verifies two OS virtual controllers, independent axes
and buttons, hats, deadzones, focus suppression, held-input disconnection,
reconnection with a new device ID and native shutdown. The adapter also builds for
arm64 with NDK 29 at API 23. Physical USB/Bluetooth controller mappings and complete
game lifecycle coverage remain to be verified on target devices.

Android Games Controller is licensed under Apache 2.0. Its attribution and license
are included by the Android package's third-party notices and SBOM.
