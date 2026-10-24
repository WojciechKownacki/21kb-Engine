# Game script logging

Game hosts register the built-in `Log(message)` function before the script function
registry is locked. Lua scripts and compiled graphs use the same registration.

Desktop and Web hosts write UTF-8 text to stdout with the prefix
`kb_game: script: `, a trailing newline and an immediate flush. The generated Web
shell displays stdout in its output area. Android writes at info level to the
system log with the tag `21kb` and the same prefix. No reference to a temporary
startup diagnostics stream is retained by the function callback.

Build `kb_cli`, `kb_cooker` and `kb_game`, then run:

```sh
ctest --test-dir build -R '^kb_game_touch_script_package$' --output-on-failure
```

This Windows test authors and cooks a real Lua script, runs the packaged player
without a visible window, requires a rendering backend, and checks ASCII and
UTF-8 log output along with three rendered frames. A separately cooked failing
script must report its assertion and return a failed exit code. Sanitized MSVC
test packages also stage the configured AddressSanitizer runtime DLL.

Release and AddressSanitizer Windows package runs and a production WebGL package
in headless Chrome have exercised script output. Android host compilation and
static analysis are checked; delivery through an Android device system log is
still unverified.

Game hosts drain scene and script errors after updates and after the script
shutdown lifecycle. Errors identify the affected entity and script. Windows
disables failing behaviours, continues rendering, and returns a failed exit code;
an error in `Destroyed` also makes the run fail. The shared Web/Linux runtime
stops on an update error and preserves its failed result through repeated
shutdown calls. Renderer frame/submission failures also make shutdown unclean.
Android reports update/shutdown script errors through the system log and stops
its update loop after an update error.

Shutdown tears down only behaviours that have started. It does not invoke
`Created` or `Ready` on newly attached behaviours, and repeated teardown does not
restart stopped behaviours. A subsequent normal scene frame can start enabled
behaviours again. The packaged Windows regression checks exactly one `Ready`
and `Destroyed` delivery, including final scene destruction.

Web runtime failures set `document.documentElement.dataset.kbError` to
`runtime-failed` and the location fragment to `#kb-error-runtime-failed`.
Diagnostics go to stderr. Streams are supplied for each tick or shutdown call;
the runtime does not retain the initialization diagnostics stream.

Loss of the browser WebGL context reports `graphics-context-lost` in the error
dataset and location fragment and pauses simulation. The host allows browser
restoration, cancels input captures, and releases bgfx resources outside the
browser event callback. The mounted package, script state and physics scene stay
alive. A restored event rebuilds the renderer from packaged shaders and assets,
reinstalls canvas input handlers, and requests a full scene synchronization.
The first resumed update receives cancelled contacts and unfocused input before
normal input resumes. The frame clock restarts without accumulating paused time.
The error clears after rebuilding; the ready fragment returns after scene
submission. The first-frame flag records historical submission, so the error
dataset and fragment take precedence while graphics are unavailable.

If restoration does not arrive within two seconds, the host replaces the canvas
and asks bgfx to create a new GPU context. The replacement preserves canvas
attributes, layout and the original touch policy, and updates `Module.canvas`.
The old context's handlers are removed. At most three replacements are allowed
until 300 consecutive submitted frames reset the retry budget.
Failed surface creation, failed renderer rebuilding or exhausted retries report
`graphics-surface`, `graphics-restore` or `graphics-restore-limit` with a reload
instruction, stop the loop, and shut down the running scene once. A browser
refusal to create a context remains a terminal error. This recovery path covers
WebGL context events; WebGPU device-loss recovery is a separate requirement.

The packaged browser regression runner uses the production host and normal
authoring/cook APIs. Install the pinned test transport from
`sources/game/tests/web/requirements.txt`, build the native tools and WebGL host,
then invoke it with absolute paths:

```sh
python sources/game/tests/web/run_packaged_runtime_tests.py \
  --cli /path/to/kb_cli --cooker /path/to/kb_cooker \
  --engine-root /path/to/source --shaderc /path/to/shaderc \
  --directory /path/to/web-build/bin --browser /path/to/chrome \
  --output /path/to/test-results
```

The positive script must reach eight ticks, a live WebGL2 rendered frame and a
negative rigidbody velocity under gravity. Shader compilation results are read
from the context actually used by the host. Web exports without pthread support
run Jolt jobs and the step driver on the calling thread, preserving deferred
publication of pipelined step results.
Separately cooked scripts fail in `Ready` and after eight ticks; each must emit
its actual Lua error and the failure marker. Missing results, browser errors and
timeouts fail the test. The graphics loss case first verifies a live context,
rendered frame, shader results and gravity, then uses the real
`WEBGL_lose_context` extension twice. It requires a paused simulation during loss,
new shader uploads, a continuous script counter and physics velocity, cancelled
held contacts, and actual panel pixels after each restoration. Missing fault
injection support or a software renderer fails the graphics recovery test.
The recreation case withholds restoration twice and requires two new canvases
and actual hardware contexts, while the original contexts remain lost.
The exhaustion case requires three successful recreations followed by an error,
one script shutdown and a stopped simulation on the fourth loss.
The positive package resizes a real UI panel, changes focus to a second owned
tab and returns, then requires live hardware graphics and the panel's pixels.
The loader case removes the fixture's WASM file and requires its actual HTTP 404,
the loader abort result and a visible reload instruction. Script failures also
require an actionable player message. These startup failures do not count as
passing rendered frames.
All nine scenarios passed with hardware WebGL2 in
headless Chrome using `--browser-arg=--use-angle=gl` (ANGLE/OpenGL).
The attempted ANGLE/D3D11 and Edge/OpenGL configurations failed startup checks;
their graphics execution remains unverified.
These short lifecycle checks are separate from full joystick/UI, audio, storage,
long-running graphics and browser lifecycle acceptance. Linux execution and
Android device delivery remain separate verification requirements.
Browser exports use `sources/game/src/web/WebGameShell.html`, with the game
canvas, hidden diagnostics and a terminal failure message. Loading and recovery
update the page title. The canvas follows the viewport; stdout is also retained
in a bounded diagnostic buffer. CMake treats the page as a link dependency.
`Module.requestFullscreen` remains exported for embedding pages.
An authored joystick and independent native UI button passed trusted touch,
cancellation, real tab focus changes and DPI/viewport resizing with actual GPU
pixel checks on desktop Chrome. This uses desktop touch emulation; physical
mobile browsers, the remaining browser/backend matrix and long-running
acceptance remain separate requirements.
The portable `sources/game/tests/web/run_game_touch_tests.py` runner creates and
cooks this fixture through the CLI and retains native snapshots, trusted DOM
events, package hashes and five GPU screenshots. Invocation and acceptance
conditions are documented in `sources/game/tests/web/README.md`.

The positive package also resizes an authored UI panel and reads its actual
pixels. The browser test observes the framebuffer status of each real texture
copy without changing the copy operation. Scenes without mesh, swarm, decal,
stroke or particle emitters skip the transparent depth copy; UI alone does not
need it. This avoids invalid depth-to-color framebuffer copies in otherwise
empty WebGL scenes. Depth copying for scenes with geometry remains a separate
WebGL validation requirement on untested backends. The geometry-depth case
authors an opaque black mesh and a translucent material that displays sampled
scene depth. Its GPU capture must distinguish geometry from cleared background;
uniform fallback depth fails. The test also observes complete read and draw
framebuffers for the actual depth blits. The bgfx framebuffer copy path uses
depth/stencil attachments and nearest framebuffer blits for these formats,
instead of color texture copies. The verified case uses desktop Chrome with
ANGLE/OpenGL. The partial- and full-opacity cases also read the actual display
buffer and require opaque output alpha. HDR scene alpha records the share of
opaque geometry still visible through translucency for temporal reprojection;
the display composite converts it to alpha one. General fullscreen texture
copies retain source alpha. Native GPU readbacks cover all three source alpha
values zero, half and one, unchanged display color, temporal reactivity,
translucent blending and FXAA.

The packaged browser runner accepts an optional `--stability-seconds` duration
for repeated geometry-depth, focus and resize checks. Use a short duration for
targeted regressions; longer runs are opt-in. Its versioned `stability.json`
retains actual GPU pixel checks, Lua progress, resource creation/deletion and
live handle counts by viewport size. Copy observations are bounded while error
counts remain cumulative. Handles and JavaScript heap usage are not GPU memory
measurements, and synchronous readbacks are not performance benchmarks. A short
passing run does not establish long-running acceptance. See the browser test
README for invocation and limitations.

`sources/game/tests/web/run_game_text_tests.py` verifies packaged native text
editing through the browser's real text and composition events. It uses CLI
authoring, font import and cook, then checks native Unicode/IME state, UTF-8
deletion, submit/cancel, focus and DPI resizing with GPU field and glyph
captures. Its JSON result distinguishes this desktop browser path from the
remaining physical mobile keyboard and occlusion requirements. Invocation uses
the same paths and browser arguments as the packaged touch runner.
