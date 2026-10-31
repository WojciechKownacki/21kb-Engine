# Linux controller integration test

Build `kb_linux_gamepad_tests` with the packaged Linux host and `BUILD_TESTING`
enabled. Run it in a test desktop session with read access to `/dev/input/event*`
and write access to `/dev/uinput`, with no other gamepads connected. The test
creates only its own virtual controllers and removes them before returning.
Their events are visible to other applications, so keep the test session isolated.

The test uses real kernel input devices and the production evdev collector. It
checks six axes, face buttons, a directional hat, hardware dead zones, focus
release and recovery, two independent slots, held-button disconnection and
reconnection. It is intentionally not registered as an unattended CTest because
it requires device permissions and an isolated input session.

The collector follows the Linux gamepad event mapping and accepts digital or
analog directional pads and triggers. It queries current kernel state each frame
and discovers new devices every 500 ms. Disconnecting a device releases its slot
without moving another controller. Devices denied by operating-system permissions
are ignored; the host does not change permissions or require elevated access.

Kernel API references:
- https://docs.kernel.org/input/gamepad.html
- https://docs.kernel.org/input/event-codes.html

Running this test on another Linux-kernel platform validates the evdev adapter,
but does not replace verification of the packaged Linux graphical host.
