# Android input

See [UI safe areas](ui_safe_areas.md) for the edge-to-edge window and native inset bridge.

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

## Touch contacts and lifecycle

Touchscreen motion retains Android pointer IDs and surface pixel coordinates.
Queued moves preserve a contact's `Began` phase until the scene consumes a frame.
An `Ended` contact remains available for that frame even when another finger moves
later in the same native input batch. The primary pointer follows the first active
contact, rather than a contact that has already ended.

`ACTION_CANCEL`, focus loss, pause, stop, surface loss and resize end existing
contacts. Terminal samples remain pending while rendering is suspended, then clear
after a scene update. A later move cannot revive a cancelled contact: a new down
event must start it. Resize publishes the new viewport extent before accepting
coordinates in that surface. Negative finite coordinates are retained for drags
outside the surface; malformed pointer counts, IDs, indices and nonfinite positions
are rejected before changing the contact set.

Storage is bounded to ten current or pending terminal samples without heap
allocation. GameActivity's default native event supports up to eight simultaneous
pointers. If more samples accumulate before a frame consumes them, new contacts
are ignored without replacing earlier IDs. An ID reused within one frame can
appear as an old `Ended` sample followed by the new `Began` sample.

The CLI APK test verifies two contacts through the real surface touch handler and
native queue, cancellation, focus suppression, an actual activity orientation
change, updated pixel coordinates and native surface destruction/recreation. It
also checks malformed native events and bounded storage. The tested host is an
Android 35 x86_64 emulator running without a window. Physical touchscreen drivers,
system navigation gestures and a complete packaged game's lifecycle remain
unverified; synthesizing `ACTION_CANCEL` does not verify an OS navigation gesture.

Android Games Controller is licensed under Apache 2.0. Its attribution and license
are included by the Android package's third-party notices and SBOM.
