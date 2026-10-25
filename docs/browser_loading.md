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
reload message. Each failed load must stop before a first native frame and avoid an unhandled
browser exception. The console must contain the corresponding host diagnostic.
Distinct package URLs prevent a previously cached valid package from hiding
the injected file error.

A separate route declares a complete HTTP body but closes the connection after
sending only half of it. The browser's real request failure must reach the
native `fetch-http` path. The report records declared and sent byte counts.
After the server restores the complete response, a normal reload of the same
URL must recover native script readiness and hardware rendering. A real button
click must then save both domains and complete their browser transaction.

The `Game.kbpack` request stops with `fetch-timeout` after 30 seconds without
additional received bytes. Duplicate progress notifications and headers alone
do not extend that idle deadline. A transfer with continuing byte progress can
last longer. The watchdog is cancelled when the body finishes and during
shutdown. Cancelling the browser request preserves the original timeout cause.
Reload starts a new request for the complete package.

`run_game_download_timeout_tests.py` exercises that real deadline. Two owned
browser profiles run concurrently: one local response sends a quarter of the
body and stalls; the other sends chunks at zero, 26 and 32 seconds. The stalled
request must fail before a native frame, while the longer active transfer must
render normally on hardware. Reloading the failed URL after restoring its full
response must recover rendering, button input and save/settings persistence.
The report retains actual chunk byte counts and times. The sanitized policy
test also checks the exact idle boundary, increasing versus duplicate byte
counts and deadline reset for a new request. These checks cover the package
request after WASM startup, rather than a stalled download of the WASM module.

The `21kb.web-game-loading/v1` result retains package/runtime hashes, native
observations, browser events, resource and heap measurements and four GPU
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

The browser reads the response stream directly into a checked, owned WASM
buffer. A known uncompressed length reserves that buffer once; an unknown or
HTTP-compressed length grows it as decoded bytes arrive. The host does not keep
a complete JavaScript response buffer. Each copy obtains the current WASM view
after allocation, which can grow linear memory. The mounted package borrows
the buffer until runtime shutdown and package unmount, then releases it.
The existing whole-file package and seal budgets also bound declared and
received download sizes. Oversized responses report `pack-size`, while a
refused buffer allocation reports `fetch-memory`. Neither failure starts the
runtime. Header lengths are checked before conversion to a WASM pointer-sized
integer, and HTTP compression never treats encoded length as decoded size.

`run_game_streamed_loading_tests.py` extends the cooked texture scenario with
real unknown-length and gzip HTTP responses, requiring the same GPU pixels.
It also serves an oversized declared length above the 32-bit range with no
body and requires a stable native failure before rendering. The sanitized
buffer tests exercise allocation refusal, growth, overflow and ownership
without allocating a large test payload. Loading reports include both the
observed JavaScript heap and the retained heap after a debugging-protocol GC.
The latter distinguishes retained buffers from temporary browser chunks;
neither observation establishes every transient peak or process RSS.

For the same 6,098,165-byte cooked package, a headless Chromium/WebGL2 check
observed retained JavaScript backing storage fall from 6,319,278 to 213,038
bytes after this change. Cold and warm checks both rendered the expected
texture and button. Observed WASM capacity remained 93,454,336 bytes for the
known-length response. These samples demonstrate removal of the retained
response copy, rather than a reduction of all memory counters or a mobile
memory guarantee.

For single-threaded Web builds, the shared load service dispatches one queued
job or read through each browser event-loop callback. Texture mip streaming
uses that service and continues uploading through bgfx. Priorities,
cancellation and lane limits still apply. Worker counts report zero; native
asynchronous file-read counters exclude the cooperative memory reads.
Blocking service waits explicitly pump queued work. A job or block transform
cannot be interrupted mid-call, so callers must bound their work. Threaded
builds use the normal workers.
When both classes remain queued, long work gets a turn after at most eight
normal job or read dispatches. Priorities still order requests within each
class. This lets queued saves and other long work progress during a sustained
streaming backlog.

The sanitized `kb_web_background_tests` target runs in Node and checks actual
production reads, transforms, priorities, cancellation, lane draining, waits,
event-loop yielding and service lifetimes. The CI Web job runs that target as
well as its trusted browser input tests. Node verifies the queue contract;
the cooked browser workload separately verifies real GPU rendering.
The queue regression also schedules long work alongside 32 memory reads. The
long request must run before the read backlog empties, and every read must
still complete with its expected bytes.
