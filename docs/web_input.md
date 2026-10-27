# Browser touch input

See [UI safe areas](ui_safe_areas.md) for cutouts, viewport scaling and keyboard overlays.

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

Window blur resets buttons and axes, cancels contacts and closes active text input.
Moving focus between elements inside the page does not count as losing window
focus. A cancelled contact cannot return through an unpaired move; it needs a new
start event. Touching a scene input field activates the browser text input bridge.
Registration failure produces an `input-init` startup diagnostic.

The [browser input CLI test](../sources/game/tests/web/README.md) verifies the native
collector in a real headless Chromium WebAssembly module with ASan and UBSan,
including trusted DOM events and native state readback. The production WebGL player
also builds with Emscripten 6.0.9. These checks cover the platform bridge.

The scene UI captures one contact that begins over a widget. Contacts that begin
outside UI remain independent, so a held gameplay contact does not prevent another
finger from pressing a button. The captured ID survives contact reordering and its
release uses its own position while other fingers remain down. Focus loss ends the
UI press without activation; later moves do not revive it. Native callers can pass
the same contact list through `SceneUIInput::touchPoints`.
The input structure and cancellation protocol require engine module ABI 11 or later.
Rebuild native engine modules; the module host rejects the previous ABI before
calling a module's creation function.

`InputTouchPhase::Cancelled` distinguishes interrupted contacts from normal
`Ended` releases. Cancel, focus loss, resize and detachment clear `TouchDown` for
those contacts. A cancelled UI contact releases its press without clicking. Input
recordings use version 5 to preserve that distinction; versions 3 and 4 remain
readable, including version 4's text and viewport fields.

Gameplay scripts can enumerate the same snapshot with `Input.TouchCount()` and
`Input.TouchPoint(index)` in Native, Lua and Visual Graph. Indices are zero-based;
track the returned `UInt32` contact ID across frames because the order can change.
`TouchPoint` returns `valid`, `id`, `x`, `y`, `phase` and `active`. Positions use
render-viewport pixels, phases are Began=0, Moved=1, Ended=2 and Cancelled=3, and
only Began/Moved are active. The count includes pending terminal samples so
gameplay can end its gestures. An invalid index returns `valid=false`,
`active=false`, zero ID/position and phase Cancelled. Queries do not consume the
snapshot or change UI capture. Physical contacts are shared by local users.

The engine script suite executes these queries through Native, Lua and a
compiled Visual Graph with reordered IDs, all four phases and the full UInt32 ID
range. `kb_game_touch_script_package` authors and cooks a Lua caller, executes it
in the hidden packaged Windows player and checks a failing script control. That
package test checks API availability with an empty contact snapshot; it does not
inject platform touch events or validate a browser joystick.

Multiple simultaneous UI captures, a complete virtual joystick and button flow,
packaged-game behavior, WebGPU execution and other browser implementations require
further integration coverage.
