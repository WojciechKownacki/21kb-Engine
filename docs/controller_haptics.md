# Controller haptics

`Input.HasHaptics` reports connection, support, dual-motor capability and a reason
when the selected controller cannot accept vibration. `Input.SetVibration` sets
normalized low- and high-frequency intensities. Values are clamped to `[0, 1]`;
nonfinite values and unavailable slots are rejected. Zero intensities stop the
selected controller, and `Input.StopVibration` stops all active controllers.
`Input.BindHapticsUser` and `Input.SetUserVibration` route a local user's requests
to a controller slot.

The Windows host uses XInput. The Android host uses the controller library's
vibration capability flags and JNI vibration requests. Dual-motor devices retain
both intensities; single-motor devices receive their maximum. The Web host uses
browser actuators that advertise `dual-rumble` and provide both `playEffect` and
`reset`. A connected controller can lack vibration support.

Android and Web refresh bounded 250 ms effects while an effect is active. Focus
loss, host shutdown and explicit stop clear the requested effect. Regaining focus
does not restart it. Disconnection discards the old slot state. If a platform
refuses a stop request, the last bounded effect expires without further refresh.

On Web, a successful set result means the request was accepted for asynchronous
processing. Rejection subsequently disables that actuator's capability until the
actuator is replaced or reconnected. A rejected request is never reported as a
measurement of physical vibration. Browser visibility, device support and browser
policy can prevent vibration even when a controller supplies input.

The Web integration test cooks a Lua scene and exercises the production scene API,
Web host and actuator calls in a browser. Android instrumentation exercises the
production provider on its Android/JNI thread with intercepted actuator calls.
These tests establish request values, routing, start/stop and failure handling;
they do not measure motors or assert that physical hardware vibrated.
