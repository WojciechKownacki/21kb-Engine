# Browser renderers

The WebGL and WebGPU exports contain separate browser runtimes and cooked shader
formats. A WebGPU export requires a secure browser context and an available GPU
adapter. Its startup checks adapter availability before downloading and starting
WebAssembly. Adapter requests have a 15-second deadline. Unsupported contexts,
adapter failures, renderer initialization failures and device loss report a visible
failure rather than a successful first frame.

To provide an optional WebGL alternative, first package the same project for
`WebGL.wasm32`. Pass that package directory as `--webgl-fallback` when packaging
`WebGPU.wasm32` with `scripts/package_game.py`. The packager verifies the WebGL
package manifest and the project content fingerprint, copies the complete package
into `webgl/`, and verifies the copied bytes before including them in the final
package. Keep the project content unchanged between the two builds.

The WebGPU entry page redirects to this alternative after a graphics startup or
device failure. Without an explicitly supplied alternative it keeps the failure
visible. Redirects are restricted to a different page on the same HTTP(S) origin.
The fallback is a new runtime session: unsaved in-memory state does not transfer.
Browser saves are scoped to the deployment directory, so the `webgl/` alternative
currently has its own save namespace.

`run_game_webgpu_tests.py` checks the cooked player's rendering, compute readback
on its actual GPU device, startup failures and GPU-process loss in an owned browser.
`run_webgpu_fallback_tests.py --webgpu-directory <export>` checks failure routing
from the compiled WebGPU export to a rendered WebGL package. Omitting that option
tests the production preflight entry without a WebGPU binary; that narrower test
does not establish successful WebGPU rendering.
