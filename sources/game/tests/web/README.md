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
