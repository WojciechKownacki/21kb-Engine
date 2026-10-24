# Browser touch input

The browser player registers touch start, move, end and cancel callbacks on its
canvas. Emscripten forwards each DOM event to the native collector, which publishes
independent contact IDs, positions and phases in `InputDeviceState::TouchPoints()`.
Touch does not set mouse buttons. Preventing the handled touch event's default
action and setting `touch-action: none` on the game canvas suppress browser gesture
handling and compatibility mouse clicks; detaching restores the previous style.

The collector converts Emscripten's integer CSS coordinates to canvas pixels using
each axis's current canvas/CSS size ratio. This includes backbuffer and device pixel
scaling. Resizing the viewport ends existing contacts before accepting coordinates
in the new extent. It shares the bounded contact-frame implementation with Android:
begins survive queued moves, releases survive another finger's later motion, and
terminal samples clear after the runtime consumes a frame. Ten active or pending
samples fit without allocating contact storage. Extra new contacts do not replace
earlier IDs.

Window blur resets buttons and axes, ends contacts and closes active text input.
Moving focus between elements inside the page does not count as losing window
focus. A cancelled contact cannot return through an unpaired move; it needs a new
start event. Touching a scene input field activates the browser text input bridge.
Registration failure produces an `input-init` startup diagnostic.

The [browser input CLI test](../sources/game/tests/web/README.md) verifies the native
collector in a real headless Chromium WebAssembly module with ASan and UBSan,
including trusted DOM events and native state readback. The production WebGL player
also builds with Emscripten 6.0.9. These checks cover the platform bridge. The scene
UI still selects one primary touch; simultaneous virtual joystick and UI button
handling, complete packaged-game behavior, WebGPU player execution and other
browser implementations require further integration coverage.
