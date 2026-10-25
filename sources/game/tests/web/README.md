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

For a short repeated focus/resize check, add `--stability-seconds 15` to the
packaged runtime command. A nonzero duration selects only
`geometry-depth-opaque`; the default remains the nine ordinary scenarios.
The test checks actual GPU pixels and opaque display alpha, progressing Lua
frames, complete depth copies, and stable live graphics handles after warmup
at equivalent viewport sizes. It alternates canvas sizes and device pixel
ratios and changes focus through a second owned tab. Context loss, recovery,
software rendering and runtime errors fail this check.

`stability.json` uses schema `21kb.web-graphics-stability/v1` and records the
requested and elapsed durations, browser version, samples, cycles and resource
counts. Graphics API handles and JavaScript heap usage do not measure VRAM;
GPU memory is reported as unavailable. Readbacks make this a correctness test,
not a timing benchmark. Short runs do not establish sustained stability;
`longAcceptance` is true only after a successful requested duration of at least
1800 seconds. Longer runs are opt-in. The verified configuration remains
headless Chrome with ANGLE/OpenGL. Copy observations retain bounded samples
and cumulative failure counts, so discarding old samples cannot hide failures.

`run_game_text_tests.py` accepts the same paths and browser arguments as the
packaged touch runner. It authors a native Input Field, imports the bundled
DejaVu Sans font through the CLI, saves and cooks the scene, then runs the
production player. The font's existing license is copied beside the package.
The shared headless harness retains commands, runtime/package hashes, browser
events, native Lua snapshots and a `21kb.web-game-text-tests/v1` result.

Trusted mouse input must focus the field and its normal DOM text control.
Trusted text/composition events verify committed Unicode, pending IME text
remaining outside the native field, and exactly one composition commit.
All text input and composition start/update events must be trusted. DevTools
can mark its final `compositionend` untrusted; the report retains that flag
separately and requires exactly one end event with the expected committed text.
An independent insertion identical to the previous IME commit must also reach
the native field. Only composition input can be suppressed as a duplicate
composition commit; ordinary text or paste must retain repeated content.
The [Input Events specification](https://www.w3.org/TR/input-events-2/) distinguishes
ordinary text insertion from updates belonging to an IME composition.
Backspace and forward Delete must preserve UTF-8 boundaries, including a
supplementary character. Enter submits once without adding a newline; Escape
reports cancellation without changing committed text. Focus changes and a
viewport/DPI resize must preserve text and allow further editing.

Four real GPU captures check the field before editing, after editing, after DPI
resizing and after reducing the viewport height. The authored field sits in a
masked scroll view. Reducing the viewport must scroll the focused field into
view, preserve its text and allow further input at its new visible position.
The edited capture also requires text contrast across multiple glyph
columns: more than 50 pixels must exceed the blue background's red and green
channels by 64, across more than eight columns. This tolerates small antialiased
glyphs while rejecting an empty field or caret alone. Missing fonts, software
rendering, lost/recovered graphics, runtime errors and failed checks fail the
test. Desktop DevTools IME and viewport emulation do not verify physical mobile
keyboards, mobile Back behavior, keyboard occlusion or complete font coverage.

The packaged storage scenario uses the same tool paths and browser arguments:

```sh
python sources/game/tests/web/run_game_storage_tests.py \
  --cli /path/to/kb_cli --cooker /path/to/kb_cooker \
  --engine-root /path/to/source --shaderc /path/to/shaderc \
  --directory /path/to/web-build/bin --browser /path/to/chrome \
  --output /path/to/storage-results --browser-arg=--use-angle=gl
```

It authors and cooks a native save button and Lua controller, verifies real GPU
pixels, writes save/settings slots, then closes and reopens its owned browser
with the same profile and local origin. Native values must survive. Real quota
denial and transaction abort must report failure and preserve the previous
committed data. Explicit retry must persist the failed working revision without
rewriting it. An actual held IndexedDB transaction exercises the unchanged
15-second deadline: retries are refused after timeout and a late real commit
cannot turn that failure into success. Reopening must read valid data.
Other URL directories must have separate slots, and a denied
initial storage read must fail startup explicitly. The result uses schema
`21kb.web-game-storage-tests/v1` and retains commands, hashes, native states,
browser events and GPU captures. See [browser storage](../../../../docs/browser_storage.md)
for the asynchronous flush contract and platform limits. Browser restart tests
do not establish physical power-loss durability or mobile browser acceptance.

`run_game_storage_pages_tests.py` accepts the same tool paths and browser
arguments. It authors a native input field, cooks its controller and opens two
players in the same owned browser profile before either has saved a slot.
Trusted text and Enter events choose different slot names. After both native
flushes report `Clean`, a third player must load both values. Two GPU captures
check the native fields, and all three players require live hardware contexts
and successful shader compilation. The report uses schema
`21kb.web-game-storage-pages/v1`. The test does not accept serially opening the
two writers after their respective saves, since that hides stale working views.

`run_game_storage_delete_tests.py` accepts the same arguments and uses schema
`21kb.web-game-storage-delete/v1`. It authors a native button and controller
through the CLI, cooks the game, and closes and reopens one owned browser
profile after each mutation. It verifies two save slots and a settings slot,
independent deletion of same-name save/settings files, missing and invalid
names, active-document preservation and recreation of a deleted slot. All
five normal sessions must render with hardware and compiled shaders; two GPU captures
check the button. A working-filesystem removal without persistence after
restart fails the test.
Two additional sessions abort real IndexedDB deletion transactions, once per
domain. Native synchronization must fail, the next browser restart must retain
the preceding committed files, and the normal button must then delete them
successfully. These sessions also require hardware contexts and compiled shaders.

`run_game_loading_tests.py` uses the same arguments and reports
`21kb.web-game-loading/v1`. Cold and subsequent loads require native readiness
and hardware-rendered button captures. It records first-frame/resource timings,
WASM capacity and JavaScript heap usage, then requires continued native progress
and rendering after a moderate browser memory-pressure notification. Five
separate local package URLs check missing, empty, truncated and corrupt files
and a response interrupted before its declared length. All require explicit
native errors and the visible reload message. Restoring the complete response
and reloading the same URL must restore native UI/GPU operation and a button
must commit save/settings data. See
[browser loading](../../../../docs/browser_loading.md) for measurement limits.

`run_game_content_loading_tests.py` authors a deterministic 2048 by 2048 texture
with four known corner colors, imports it and references it from a native UI
Image before cooking. The resulting package must exceed 4 MiB. Cold and
subsequent loads must render the actual texture colors and native button on a
hardware GPU. The report uses `21kb.web-game-content-loading/v1` and retains
startup/resource timings and memory observations. Pass `--cook-cache` to reuse
an existing owned cook cache across these storage and loading scenarios.

`run_game_streamed_loading_tests.py` uses those same arguments and reports
`21kb.web-game-streamed-loading/v1`. In addition to cold/warm texture readback,
it serves complete packages without a declared length and with HTTP gzip
encoding. Both must render the same GPU pixels. A declared length above the
32-bit range with no body must fail before rendering. The report records
actual transmitted byte counts and retained JavaScript heap after GC.

Build `kb_web_background_tests` in the standalone Web test project and run
`node BackgroundLoadTests.js` to check the production cooperative load service
with ASan and UBSan. It verifies queued execution, priorities, cancellation,
download buffer allocation refusal, size bounds, overflow, ownership,
real memory/file reads, transforms, lane draining, explicit waits and safe
dispatch lifetimes. The CI Web job runs it automatically.

`run_game_download_timeout_tests.py` uses the same arguments and reports
`21kb.web-game-download-timeout/v1`. It checks the real 30-second package idle
deadline alongside a 32-second transfer that continues delivering bytes. The
two cases run concurrently in owned profiles. The first must preserve its
timeout diagnostic when the request is cancelled; the second must reach native
readiness and hardware-rendered UI. Restoring the stalled response and normally
reloading must also recover rendering, input and persistence. The standalone
sanitized test covers the production idle policy's boundary and progress rules.

`run_game_wasm_timeout_tests.py` uses the same arguments and reports
`21kb.web-game-wasm-timeout/v1`. It applies the real stalled/progressing/reload
scenario to the WASM module before native startup. A failed bootstrap must not
request the package or create a shader; successful and reloaded sessions still
require native input, persistence and hardware GPU readback.
`run_game_wasm_loading_tests.py` reports `21kb.web-game-wasm-loading/v1` for
missing/corrupt modules, a response interrupted mid-body, a generic MIME type
and a script/WASM directory separate from the HTML page. The two valid variants
require native readiness and actual hardware-rendered button captures.

`run_wasm_bootstrap_failure_tests.py --browser /path/to/chromium --directory
build/web-player/bin --output build/web-player-results` requires only a built
production player and the pinned browser transport dependency. It checks the
three WASM failure cases before native startup. Its report explicitly excludes
native runtime and GPU rendering acceptance; the unused empty package sentinel
must never be requested. The CI Web player job builds `kb_game_webgl` and runs
this command. Complete cooked game/GPU checks remain separate.

`run_game_wasm_options_tests.py` takes the full game fixture arguments and
reports `21kb.web-game-wasm-options/v1`. It verifies `Module.locateFile`, a
preloaded `Module.wasmBinary` and an existing `Module.instantiateWasm` callback.
All three require native readiness, hardware shader/draw checks and real button
pixels. Each page lacks a default module file; the report must observe one
complete module request and one custom instantiation, rather than an ignored
option or duplicate download.

`run_game_audio_property_tests.py` takes the full game fixture arguments and
reports `21kb.web-game-audio-properties/v1`. It generates a WAV, imports it and
authors source/listener settings through the CLI. The cooked native script must
read the authored values and mutate volume and pitch through the shared
property API. A real hardware-rendered UI capture is required. The report
explicitly excludes audio-output acceptance; this scenario checks authoring,
save/cook/load and script properties. See
[audio properties](../../../../docs/audio_component_properties.md).

`run_game_audio_tests.py` uses the same full fixture arguments and reports
`21kb.web-game-audio/v1`. Its generated mono 440 Hz WAV passes through normal
import, cook, native playback and WebAudio. Five trusted UI clicks exercise
play, pause, resume, stop and play again. A read-only output observer requires
four consecutive real PCM blocks per phase, checking frequency, authored gain,
silence and stable resumed amplitude. It also requires real native UI GPU
pixels. It does not unlock audio itself or bypass browser activation policy.
See [browser audio verification](../../../../docs/browser_audio.md) for the
measurement bounds and remaining platform coverage.
The native status must distinguish suspended output before activation and after
controlled `AudioContext.suspend()` interruptions. Source transport intent must
survive two explicit backend restarts through trusted native UI clicks, with
previous contexts closed and no amplitude doubling. An owned second tab must
hide the player while PCM continues; restoring focus must preserve both PCM
and native UI rendering. OS interruption and automatic recovery are separate
coverage.

`run_game_audio_autoplay_tests.py` reports `21kb.web-game-audio-autoplay/v1`.
It requires queued authored autoplay before activation, then actual PCM after a
trusted click without a script play request. Live volume, mute, pitch and
enabled writes through the shared property API must change measured PCM as
expected, retaining one context and a hardware-rendered native UI capture.

`run_game_audio_ogg_tests.py` reports `21kb.web-game-audio-ogg/v1` and repeats the
autoplay and live-property PCM scenario using a normally imported and cooked
Ogg Vorbis fixture. It requires actual decoder output, not a format-catalog
entry alone. See [audio formats](../../../../docs/audio_formats.md).
