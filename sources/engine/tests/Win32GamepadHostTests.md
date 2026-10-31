# Windows controller tests

`kb_win32_gamepad_host_tests` checks DirectInput sample normalization, axis and
button overrides, diagonal and neutral hats, dead zones, disabled axes and slot
isolation. It also opens a hidden native window and calls the production device
collector and actuator capability API. This host check does not require a
controller and does not prove physical hotplug or device-specific mappings.

For hardware verification, attach at least two controllers, including one
DirectInput device. Check independent inputs, disconnect a held controller,
reconnect it, and check that another controller retains its slot. Repeat while
changing window focus. Verify that XInput devices are not duplicated and that
rumble affects the same controller as the input slot.

`ConfigureWin32GamepadMapping` accepts a DirectInput product GUID and a mapping
for subsequently opened devices. Axes identify X, Y, Z, Rx, Ry, Rz and two
sliders; button indices identify the device's native buttons. Negative indices
disable controls. Defaults cover common joystick layouts; vendor-specific
layouts should supply an explicit mapping. DirectInput force feedback is not
exposed as XInput rumble capability.
