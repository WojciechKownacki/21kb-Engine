# Renderer readiness matrix

A renderer feature counts as ready only when the whole path exists: it can be authored, it survives save and
load, it runs in Play, it is cooked into a package, and an image or a number from a real run proves it. A field in
an inspector, an enum value or a shader that nothing exercises does not count.

This table lists what each feature has today. `ctest:<name>` is a CTest case and `selector:<name>` is an argument
of `kb_renderer_tests`; `scripts/tests/test_renderer_readiness_matrix.py` fails when a name here does not exist or
a row leaves its last column empty. Image evidence is from Direct3D 11 with a hidden window unless a row says
otherwise; the other backends are not run by CI.

| Feature | Authoring | Save and load | Play | Cook and package | Evidence | Not proven |
| --- | --- | --- | --- | --- | --- | --- |
| Built-in PBR material | files, `kb_cli` | yes | yes | yes | `ctest:kb_renderer_tests`, `selector:packaged-material` | Mobile path; clearcoat, sheen, transmission and subsurface fields of the built-in type are read and ignored |
| Material graph | graph file, `kb_cli` | yes | yes | yes | `ctest:kb_renderer_graph_shader_node_cook_0`, `ctest:kb_renderer_graph_shader_node_cook_1`, `ctest:kb_renderer_graph_shader_node_cook_2`, `ctest:kb_renderer_graph_shader_node_cook_3`, `ctest:kb_renderer_graph_program_lifetime` | A semantic test for every node kind (the cook sweep proves only that each node compiles) |
| Exposure, tonemap, bloom, vignette | profile asset | yes | yes | yes | `selector:exposure-meter`, `selector:post-process-profile`, `ctest:kb_renderer_display_alpha` | Pixel test of bloom; flicker under a sudden brightness change; output profiles |
| FXAA and TAA | profile asset; per-submit camera cut flag | yes | yes | yes | `ctest:kb_renderer_tests`, `ctest:kb_renderer_temporal_history`, `ctest:kb_renderer_display_alpha` | Full moving-scene ghosting measurements across particles, animated meshes and all export backends |
| Ambient occlusion | profile asset | yes | yes | yes | `ctest:kb_renderer_gi_resolve`, `selector:post-process-profile` | Thin objects, range, camera motion, cost |
| Screen-space reflections | lighting config only | no | yes | no | `ctest:kb_renderer_screen_space` | Off-screen objects, lost depth, camera cut, roughness range; no profile or inspector field |
| Global illumination | profile asset | yes | yes | yes | `ctest:kb_renderer_gi_resolve`, `ctest:kb_renderer_screen_space` | Moving lights, thin walls, streaming, rebuild time; a mesh without CPU geometry fills its bounding box without a diagnostic |
| Sky reflection and probes | scene components | yes | yes | yes | `ctest:kb_renderer_sky_reflection` | Probe blending, parallax, motion, update cost |
| Lights and shadows | light components | yes | yes | yes | `ctest:kb_renderer_shadows_particle_mesh`, `selector:light-grid` | Shadow image with more than six lights; the dropped-light count is available to the planner but not yet in the frame statistics |
| Fog and volumetric fog | profile asset | yes | deferred only | yes | none | Everything: no renderer test covers fog; forward materials ignore it |
| Mesh LOD | import and bake | yes | yes | yes | `selector:mesh-bake`, `selector:mesh-pipeline` | Silhouette metric, skinned meshes, motion-time transitions |
| Visibility and occlusion | blocker and cell components | yes | yes | yes | `selector:mesh-visibility`, `selector:mesh-pipeline` | Stepping out from behind a wall, streaming, measured benefit |
| Transparency | material blend modes | yes | yes | yes | `selector:transparent-gpu-readback` | Intersecting surfaces, camera motion, a list of known limits |
| Large worlds | project setting | yes | yes | yes | `selector:large-world` | An origin shift across physics, audio, navigation, network and scripts together |
| Heightmap terrain | terrain editor | yes | yes | yes | `ctest:kb_terrain_editor_tests` | Undo, cook, collision, chunk borders, RAM of a large terrain |
| World partition and HLOD | scene settings | yes | yes | yes | `selector:world-hlod` | Long travel, teleport, cancelled loads, hitch limits |
| GPU particles | effect asset, `kb_cli vfx-*` | yes | yes | yes | `ctest:kb_renderer_particle_motion`, `ctest:kb_renderer_particle_contact_death`, `ctest:kb_renderer_particle_depth_collision`, `ctest:kb_21kb_particle_cpu_backend_tests`, `ctest:kb_game_particle_gpu_package` | Backends other than Direct3D 11 in CTest; GPU flipbook, mesh emission, collision events |
| GPU culling and meshlet indirect submission | cooked mesh metadata | yes | opt-in | yes | `selector:gpu-frustum-culling`, `selector:gpu-meshlet-submission` | D3D11 camera-edge identity/image parity and CPU/GPU costs verified; other backend runtime checks remain |

Temporal history is scoped to a viewport and invalidated when its scene or selected camera changes,
when switching between perspective and orthographic projection, after a skipped render frame,
when TAA is re-enabled, and on resize. A host that teleports the same camera or cuts between camera
overrides sets `RenderSceneSubmitDesc::resetTemporalHistory` for that submission. Ordinary camera
movement retains reprojection history. The reset affects both temporal resolve and motion-vector
camera matrices. It does not reset exposure adaptation.

Optional indexed indirect and bounded meshlet submission use GPU-compacted instances; see [GPU culling](gpu_culling.md).

## Not implemented

Temporal upscaling, baked
lightmaps, depth of field, ray tracing, OpenXR and a public compute or render-effect extension API have no code
behind them either. Documents must not describe them as present; `scripts/tests/test_renderer_claims.py` guards
the first of these.
