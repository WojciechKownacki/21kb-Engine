# Browser package loading checks

`sources/game/tests/web/run_game_loading_tests.py` authors a native UI project
through the CLI, cooks its package and runs the exported player in an owned
headless browser profile. It accepts the same tool and browser arguments as
the browser storage scenarios.

A cold load and a second load with the same profile and local origin must
reach native script readiness and render the button with a hardware WebGL2
context. The report records the actual first-frame time, package resource
timing and transfer sizes, JavaScript heap usage and WASM linear-memory
capacity. A 20 ms sampler records the largest observed WASM capacity during
startup. Cache use and timing are observations; the test does not require a
second load to be faster.

The browser receives a real moderate memory-pressure notification through its
debugging protocol. Native script progress, hardware graphics and another GPU
capture must remain valid afterwards. This checks handling of that browser
notification, not a physical low-memory device or an operating-system kill.

Separate local URL directories serve the same runtime with a missing, empty,
truncated or corrupt package. The real host must report `fetch-http`,
`pack-empty` or `pack-mount`, retain that error on later frames and show its
reload message. No case may reach a first native frame or throw an unhandled
browser exception. The console must contain the corresponding host diagnostic.
Distinct package URLs prevent a previously cached valid package from hiding
the injected file error.

The `21kb.web-game-loading/v1` result retains package/runtime hashes, native
observations, browser events, resource and heap measurements and three GPU
captures. WASM capacity and JavaScript heap samples do not measure process RSS,
GPU residency or every transient allocation. This small native UI fixture
does not establish large-package, decompression or physical mobile memory
limits. The truncated-file case is a complete HTTP response carrying a partial
container; it does not simulate a connection interrupted mid-response.
