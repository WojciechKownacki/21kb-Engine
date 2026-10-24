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

`run_game_content_loading_tests.py` uses the same arguments to author a larger
texture workload. It generates its own deterministic 2048 by 2048 RGB image,
imports it with the CLI, references it from a native UI Image and cooks the
normal runtime dependency closure. The actual package must exceed 4 MiB.
Both cold and subsequent loads must render four known corner colors in the
correct orientation and the native button. The report retains the resulting
package size, startup/resource timings, WASM capacity and JavaScript heap
observations. This verifies an actual cooked texture workload rather than
padding a small package with unused bytes.

For single-threaded Web builds, the shared load service dispatches one queued
job or read through each browser event-loop callback. Texture mip streaming
uses that service and continues uploading through bgfx. Priorities,
cancellation and lane limits still apply. Worker counts report zero; native
asynchronous file-read counters exclude the cooperative memory reads.
Blocking service waits explicitly pump queued work. A job or block transform
cannot be interrupted mid-call, so callers must bound their work. Threaded
builds use the normal workers.

The sanitized `kb_web_background_tests` target runs in Node and checks actual
production reads, transforms, priorities, cancellation, lane draining, waits,
event-loop yielding and service lifetimes. The CI Web job runs that target as
well as its trusted browser input tests. Node verifies the queue contract;
the cooked browser workload separately verifies real GPU rendering.
