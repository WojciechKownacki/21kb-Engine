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

Web runtime failures set `document.documentElement.dataset.kbError` to
`runtime-failed` and the location fragment to `#kb-error-runtime-failed`.
Diagnostics go to stderr. Streams are supplied for each tick or shutdown call;
the runtime does not retain the initialization diagnostics stream.

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
timeouts fail the test. All three scenarios passed with hardware WebGL2 in
headless Chrome using `--browser-arg=--use-angle=gl` (ANGLE/OpenGL). ANGLE/D3D11
reached eight script ticks after the physics fix, but its context checks timed
out or reported context loss; that configuration is not a passing graphics run.
These short lifecycle checks are separate from full joystick/UI, audio, storage,
long-running graphics and browser lifecycle acceptance. Linux execution and
Android device delivery remain separate verification requirements.
