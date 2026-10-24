# Browser input tests

The standalone test builds the production `WebTouchInput` collector and shared
contact frame into a WebAssembly module with AddressSanitizer and
UndefinedBehaviorSanitizer. The CLI starts its own headless Chromium profile and
local HTTP server. DevTools sends trusted touch events through the actual DOM
listeners and Emscripten callbacks; snapshots are read back from native
`InputDeviceState`. A real mouse input control checks that the observation path
works before testing that touch produces no emulated mouse clicks.

Use Emscripten 6.0.9, CMake, Ninja, Python and a Chromium browser. Activate the SDK
in the current shell and run from the repository root:

```sh
cmake -S sources/game/tests/web -B build/web-input -G Ninja -DCMAKE_TOOLCHAIN_FILE="$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" -DCMAKE_BUILD_TYPE=Debug
cmake --build build/web-input
python -m pip install --require-hashes -r sources/game/tests/web/requirements.txt
python sources/game/tests/web/run_input_tests.py --browser /path/to/chromium --directory build/web-input --output build/web-input-results
```

On Windows, run the SDK's `emsdk_env.bat` first and pass the expanded toolchain
path to CMake. The transport dependency, websocket-client 1.9.0, is test-only and
licensed under Apache 2.0; installation preserves its license and attribution.

Checks cover independent IDs, queued phases, one finger ending while another
moves, cancellation, actual tab focus loss/restoration, independent canvas/CSS
scales, resize, child text focus, malformed native events, callback removal and
restoring the canvas's previous `touch-action`. DOM observations also verify
`isTrusted` and the ID of the released finger. Missing browsers, registration
failure, timeouts, sanitizer diagnostics and failed checks fail the test.

The result uses schema `21kb.web-input-tests/v1`. This isolated input test does
not verify a rendered game, graphics backend, virtual controls or the scene UI's
simultaneous handling of multiple contacts. Those need separate integration
scenarios with the full player.

The packaged touch scenario authors a fresh project through the CLI, saves a Lua
joystick controller and a native UI button, cooks its package, and runs the
production player. Build the native CLI, cooker, shader compiler and WebGL host,
then pass their paths:

```sh
python sources/game/tests/web/run_game_touch_tests.py \
  --cli /path/to/kb_cli --cooker /path/to/kb_cooker \
  --engine-root /path/to/source --shaderc /path/to/shaderc \
  --directory /path/to/web-build/bin --browser /path/to/chrome \
  --output /path/to/test-results --browser-arg=--use-angle=gl
```

This runner uses the same pinned transport dependency. GPU screenshots are
decoded with the engine's Python PNG helper; no image package is required.
Eighteen checks cover independent gameplay and UI contacts in either press
order, reordered packets, release and click delivery, cancellation, actual tab
focus changes, and DPI/viewport resizing. Five actual GPU captures verify the
knob position and button rendering. A real mouse positive control precedes the
check that touch generates no mouse events. Native touch state comes from the
packaged Lua API, and DOM events must be trusted.

Missing hardware WebGL2, software rendering, shader errors, context loss,
runtime errors and failed checks fail the run. Recovery or CPU fallback does
not count as a passing GPU test. Each fresh output directory retains authoring
and cook commands, package/runtime hashes, browser events, native snapshots,
screenshots and a `21kb.web-game-touch-tests/v1` result. The verified configuration
is desktop headless Chrome with ANGLE/OpenGL. Desktop touch emulation does not
establish physical mobile, WebGPU or long-running browser acceptance; other
browser/backend configurations remain separate checks.

`run_packaged_runtime_tests.py` also accepts `--case geometry-depth` with the same
paths. It cooks a black opaque mesh and a partially transparent material that
visualizes copied scene depth. Actual pixels must distinguish the mesh from
cleared background, and the observed depth blits must use complete read/draw
framebuffers. The case runs in a separate project copy so its materials and UI
settings cannot affect other requested cases. The default runtime suite includes
this case and `geometry-depth-opaque` alongside the seven lifecycle and error
scenarios. The latter uses full material opacity. Both verify actual GPU display
readbacks with alpha 255 and visible scene depth in the browser screenshot;
the scene's internal reactive alpha must not hide the final canvas image.

The native `kb_renderer_display_alpha` CTest requires shader tools and real bgfx
readbacks. It checks that display output is opaque for source alpha zero, half
and one while general texture copies retain those values, then verifies temporal
reactivity, translucent blend alpha and FXAA. Missing tools or a graphics device
fail this check. It runs in the Windows release and sanitizer CI jobs.
