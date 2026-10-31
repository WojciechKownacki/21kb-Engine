# Renderer readiness matrix

A renderer feature counts as ready only when the whole path exists: it can be authored, it survives save and
load, it runs in Play, it is cooked into a package, and an image or a number from a real run proves it. A field in
an inspector, an enum value or a shader that nothing exercises does not count.

This table lists what each feature has today. `ctest:<name>` is a CTest case and `selector:<name>` is an argument
of `kb_renderer_tests`; `scripts/tests/test_renderer_readiness_matrix.py` fails when a name here does not exist or
a row leaves its last column empty. Image evidence is from Direct3D 11 with a hidden window unless a row says
otherwise; the other backends are not run by CI.

Resource ownership, shared effect inputs, storage/work bounds and requirements for extending the pipeline are recorded in
[Renderer ownership and effect contracts](renderer_ownership.md). The local Windows matrix additionally qualifies one moving
scene on D3D11/D3D12/Vulkan and development OpenGL; see [Native image regression](renderer_image_regression.md).

| Feature | Authoring | Save and load | Play | Cook and package | Evidence | Not proven |
| --- | --- | --- | --- | --- | --- | --- |
| Built-in PBR material | files, `kb_cli` | yes | yes | yes | `selector:material-schema`, `selector:packaged-material`, `selector:material-matrix` (D3D11/D3D12/Vulkan/OpenGL) | Mobile/Web/Metal runtime; sheen, transmission and anisotropy remain ignored. Clearcoat and local subsurface, precision limits and four-target fallback are described in [Material layers](material_layers.md) |
| Material graph | graph file, `kb_cli` | yes | yes | yes | `ctest:kb_renderer_graph_shader_node_cook_0`, `ctest:kb_renderer_graph_shader_node_cook_1`, `ctest:kb_renderer_graph_shader_node_cook_2`, `ctest:kb_renderer_graph_shader_node_cook_3`, `ctest:kb_renderer_graph_program_lifetime` | A semantic test for every node kind (the cook sweep proves only that each node compiles) |
| Exposure, tonemap, bloom, vignette | profile asset | yes | yes | yes | `selector:exposure-meter`, `selector:post-process-profile`, `ctest:kb_renderer_display_alpha` | Pixel test of bloom; flicker under a sudden brightness change; output profiles |
| FXAA and TAA | profile asset; per-submit camera cut flag | yes | yes | yes | `ctest:kb_renderer_tests`, `ctest:kb_renderer_temporal_history`, `ctest:kb_renderer_display_alpha` | Full moving-scene ghosting measurements across particles, animated meshes and all export backends |
| Ambient occlusion | profile asset | yes | yes | yes | `ctest:kb_renderer_gi_resolve`, `selector:post-process-profile` | Thin objects, range, camera motion, cost |
| Screen-space reflections | lighting config only | no | yes | no | `ctest:kb_renderer_screen_space`, `ctest:kb_renderer_screen_effect_lifecycle` | No profile or inspector field; detailed lifecycle image qualification is D3D11 only |
| Global illumination | profile asset | yes | yes | yes | `ctest:kb_renderer_gi_resolve`, `ctest:kb_renderer_screen_space`, `ctest:kb_renderer_screen_effect_lifecycle`, `selector:renderer-diagnostics` | Arbitrary geometry and long multi-region journeys need separate budgets; tested contributor motion/unload/reload, bounded mask cache and thin walls do not establish global scene throughput |
| Sky reflection and probes | sky backdrop components; local probes are configuration fields only | sky yes | sky yes | sky yes | `ctest:kb_renderer_sky_reflection` | Local probe fields affect statistics but have no shader sampling, blend or parallax implementation; sky motion/update cost needs separate qualification |
| Lights and shadows | light components | yes | yes | yes | `ctest:kb_renderer_shadows_particle_mesh`, `selector:light-grid`, `selector:renderer-diagnostics` | Other host/device classes; native stress verifies 41 lights with a 32-shadow limit and explicit dropped counts |
| Fog and volumetric fog | profile asset, Desktop/Mobile/Web selection | yes | deferred opaque/background; height approximation on ordinary forward transparent surfaces | yes | `ctest:kb_renderer_screen_space`, `selector:fog-profiles` (D3D11/D3D12/Vulkan/OpenGL), post-process Inspector workflow | Native tests cover profile fallback, dark interiors, anisotropy, shadowed scattering, cuts and resize. Physical mobile/browser execution remains unqualified; see [fog contract](renderer_fog.md) for transparency and G-buffer requirements |
| Mesh LOD | import and bake | yes | yes | yes | `selector:mesh-bake`, `selector:mesh-pipeline` | Silhouette metric, skinned meshes, motion-time transitions |
| Visibility and occlusion | blocker and cell components | yes | yes | yes | `selector:mesh-visibility`, `selector:mesh-pipeline` | Stepping out from behind a wall, streaming, measured benefit |
| Transparency | material blend modes | yes | yes | yes | `selector:transparent-gpu-readback` | Intersecting surfaces, camera motion, a list of known limits |
| Large worlds | project setting | yes | yes | yes | `selector:large-world` | An origin shift across physics, audio, navigation, network and scripts together |
| Heightmap terrain | terrain editor | yes | yes | yes | `ctest:kb_terrain_editor_tests` | Undo, cook, collision, chunk borders, RAM of a large terrain |
| World partition and HLOD | scene settings | yes | yes | yes | `selector:world-hlod` | Long travel, teleport, cancelled loads, hitch limits |
| GPU particles | effect asset, `kb_cli vfx-*` | yes | yes | yes | `ctest:kb_renderer_particle_motion`, `ctest:kb_renderer_particle_contact_death`, `ctest:kb_renderer_particle_depth_collision`, `ctest:kb_21kb_particle_cpu_backend_tests`, `ctest:kb_game_particle_gpu_package` | Backends other than Direct3D 11 in CTest; GPU flipbook, mesh emission, collision events |
| Custom GPU particle simulation | `.kbpsim` process asset, output inspector, `kb_cli particle-process-cook`, `vfx-set` | yes | yes | yes | `selector:particle-process` | Contract v1: billboard state; other outputs and CPU policies are rejected. GPU proof uses Direct3D 11. |
| GPU culling and meshlet indirect submission | cooked mesh metadata | yes | opt-in | yes | `selector:gpu-frustum-culling`, `selector:gpu-meshlet-submission` | D3D11 camera-edge identity/image parity and CPU/GPU costs verified; other backend runtime checks remain |
| Native compute/render effects | public plugin API and manifest | exported bundle | yes | native library and compiled shader variants | `selector:render-effect-plugin` | D3D11 external module workflow; other native target libraries and runtime backends remain |

Temporal history is scoped to a viewport and invalidated when its scene or selected camera changes,
when switching between perspective and orthographic projection, after a skipped render frame,
when TAA is re-enabled, and on resize. A host that teleports the same camera or cuts between camera
overrides sets `RenderSceneSubmitDesc::resetTemporalHistory` for that submission. Ordinary camera
movement retains reprojection history. The reset affects both temporal resolve and motion-vector
camera matrices. It does not reset exposure adaptation.

Optional indexed indirect and bounded meshlet submission use GPU-compacted instances; see [GPU culling](gpu_culling.md).

Native compute/render effect plugins use a versioned public API, owned resources and source-free bundle export; see [render effect plugins](render_effect_plugins.md).

## Not implemented

Temporal upscaling, baked lightmaps, depth of field, ray tracing and OpenXR have no code
behind them. Documents must not describe them as present; `scripts/tests/test_renderer_claims.py` guards
the first of these.
