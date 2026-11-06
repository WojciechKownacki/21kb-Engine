# Native image regression

The Windows matrix executes real scene draws and screenshot readbacks on Direct3D 11, Direct3D 12, Vulkan and OpenGL.
It rejects renderer fallback, missing resources, dropped geometry, incomplete frame traces and blank captures. Noop cannot
qualify. Results identify the backend, device, driver, executable hash, package or development shader hashes, Git HEAD and
working source hashes. A dirty working tree is identified separately from HEAD.

Build the editor, player, CLI, cooker and renderer shaders with `KB_GENERATE_RENDERER_SHADERS=ON`, using at most two build jobs.
Then run:

```sh
ctest --test-dir build/bgfx-update -R "^kb_editor_native_image_regression$" --output-on-failure
ctest --test-dir build/bgfx-update -R "^kb_renderer_particle_blend_goldens" --output-on-failure
```

The workflow uses the existing headless editor scenario to create, save and reopen a scene with directional shadows,
a point light, sky, terrain, opaque and metallic materials, emissive geometry, soft particles, FXAA, AO, fog, vignette and
auto exposure. A Lua script moves the cube. Editor and player advance the same fixed timestep and capture frames 60, 120
and 240 at 960 by 540. A separate image check of the cube region ensures particle animation cannot satisfy the object
motion assertion. This fixture does not qualify skeletal animation or TAA history.

## Package isolation

Windows.x64 packages contain DXBC, DXIL and SPIR-V. Those three backends are captured from a cooked package. OpenGL is
qualified from the development project, because this package profile does not include GLSL. Metal, GLES and WebGPU
execution require their own hosts and are not reported as passing by this matrix.

The workflow copies the player and runtime modules into a fresh package directory, temporarily hides the authoring
project and runs the normal shipped executable. It also copies the same player into a sibling diagnostic directory,
then explicitly opens the package for instrumented captures. The diagnostic directory has no editor, assets or loose shaders.
The package shader provider fails missing entries instead of falling back to loose shaders. Production startup beside
`Game.kbpack` keeps its restrictions on development switches. The hidden authoring project is restored even on failure.

## Images, comparisons and failure evidence

Each run remains in a unique `SceneImages*` directory under the test output root. It retains the editor scenario/report,
player logs, metadata, frame CSV/trace, actual PNGs, amplified absolute RGB difference PNGs and JSON comparison reports.
`result.json` links the command exit codes and comparisons. Reports distinguish capture candidates from passing regression
results; no failed run updates references.

Editor-to-player comparisons retain the established limits: mean absolute channel error at most 2/255 and at most 0.2%
of pixels exceeding 12/255 in any channel. Every pixel counts, including particles. Backend-specific reference limits and
their rationale are stored in `sources/game/tests/goldens/Scene3D/manifest.json`. These limits allow small rasterization
and shading differences without accepting a missing object or effect. Difference PNGs multiply channel errors by four
for inspection; reported statistics use the original errors.

After positive comparisons, the test collapses the cube in the source scene, renders it again and requires the image
comparison to fail. The negative control retains its image, diff and configuration. A failed process or invalid capture
is an error and cannot count as successful detection of the intended visual mutation.

The matrix exposed missing soft particles on OpenGL: sampled depth was not converted from texture 0..1 to the projection's
homogeneous -1..1 clip range. The corrected conversion is shared by CPU/GPU particle rendering and decal reconstruction.
The floating-point particle test now checks all four native backends, including six blend equations, absent depth,
occlusion and five surface intersection gaps. It validates actual pixel values against the expected soft fade.

The moving scene also exposed a one-frame delay in D3D12 backbuffer screenshots. The bundled backend selected the previous
swapchain buffer although screenshot requests execute after the current submit and before presentation. It now captures
the just-rendered buffer. The image matrix keeps identical requested frame numbers and unchanged parity tolerances.

## Reviewing reference changes

Run `sources/game/tests/RendererImageWorkflowTests.py --help` for the explicit executable and plugin arguments. Use
`--capture-only` to produce candidates. This still requires the editor/package comparisons and negative control to pass,
but its final status is `CAPTURED`, never a regression pass.

Inspect all three frames for each backend and their editor comparison diffs. Check object motion, contact shadows,
material appearance, soft particle visibility and intersections, terrain edges and post-processing. Investigate a visible
missing effect before updating any reference. Copy only reviewed PNGs into the golden directory and update their hashes,
fixture hash, backend limits and review notes in the manifest. Do not copy local absolute paths or account information.
Then run the normal matrix against those references. A fixture change or altered reference hash fails until deliberately
reviewed; reference generation is never part of normal test execution.
