# Built-in PBR layers

`builtin.pbr` evaluates clearcoat and subsurface together. Both use the same direct-light functions in the forward and deferred paths. Base color, metallic/roughness, normal, occlusion and emissive inputs retain their existing meanings.

| Input | Meaning |
| --- | --- |
| `clearcoatFactor` | Dielectric coat weight, clamped to 0–1 |
| `clearcoatRoughnessFactor` | Coat GGX roughness, independent of the base roughness; evaluated at a minimum of 0.02 |
| `clearcoatTextureAssetId` | Linear texture, red channel multiplied by coat weight |
| `clearcoatRoughnessTextureAssetId` | Linear texture, green channel multiplied by coat roughness |
| `subsurfaceFactor` | Wrap and back-scattering strength, clamped to 0–1 |
| `subsurfaceColor` | Linear scattering tint, clamped to 0–1 |

Missing unassigned coat textures use white. The coat attenuates base lighting by its Fresnel reflection and adds a separate direct and environment reflection lobe. Subsurface is a local wrap/back-scattering approximation, without screen-space diffusion, thickness transport or a subsurface texture input. Clearcoat uses the base shading normal; a separate coat-normal map is not supported.

## Capabilities and fallback

| Path | Layer behavior |
| --- | --- |
| Built-in forward | Both layers and the separate scattering tint |
| Built-in deferred with five color attachments and the optional layer shader | Both layers; the fifth target stores scattering RGB and strength |
| Built-in deferred on OpenGLES, fewer than five attachments, or missing optional layer shader | Four-target fallback: clearcoat takes precedence; subsurface alone retains its strength and uses base diffuse color (`baseColor * (1 - metallic)`) as its tint |

The fallback emits `MaterialShadingFallback` for affected subsurface draws. It does not change authored data. The optional `scene_mesh_gbuffer_layered` program is requested lazily for subsurface draws; a missing variant is not retried every frame. Renderer shutdown clears that failure cache. A failure to allocate the extra target after selecting the full path reports `DeferredRendererUnavailable`.

Graph shading models retain their own model selections and parameter contracts. Surface graphs using the fifth target also cook a `GBufferCompact` variant. OpenGLES and devices with fewer than five attachments select that four-target variant; other backends can use it if the full graph binary is absent. Graph evaluation, base parameters and model selection are preserved. The compact variant omits the extra scattering/fuzz color or iris normal: subsurface uses diffuse tint, cloth loses its extra colored fuzz, and eye shading uses its sclera normal. These draws emit `MaterialShadingFallback`. Sheen, transmission, anisotropy and material-layer blending fields in the built-in type remain unsupported and diagnosed by the material schema.

## Assets and verification

glTF `KHR_materials_clearcoat` imports both factors and the coat/roughness texture paths. Those inputs participate in the material dependency and cook paths. Existing material serialization retains subsurface and coat values; no material format migration is needed.

`kb_renderer_tests material-matrix --backend d3d11|d3d12|vulkan|opengl` renders spheres under front, side and back lighting. It compares forward/deferred images, sweeps base and coat roughness, tests scattering tint and combined layers, checks texture channels against scalar controls, and removes the optional shader through the runtime provider to exercise the fallback. Images are written to `material_matrix_tests` in the test working directory.

These readbacks are linear RGBA8 without tonemapping. The G-buffer also quantizes material values and normals, so sharp highlight peaks are not bit-identical between paths. The qualified coat matrix starts at roughness 0.12; narrower lobes need separate precision qualification. Texture-channel controls use a broader lobe to isolate channel selection from this precision difference.

The base matrix includes linear metallic/roughness channels, occlusion strength and indirect-only attenuation, emissive multiplication, normal scale, alpha masking and blending, repeated transformed UVs, and two-sided interior lighting. Two-sided surfaces reverse their final shading normal on back faces in both paths. Transparent surfaces use forward shading even when the scene lighting path is deferred.

Albedo and emissive color textures use sRGB decoding; metallic/roughness, normals, AO and coat inputs are linear. The sprite-oriented `pointSampling` option applies point filtering and UV clamping to albedo/emissive. Ordinary built-in materials retain their texture sampler addressing, including repeat for tiled UVs.

`kb_renderer_tests graph-compact-gbuffer` compares full graph data against actual four-attachment GPU draws for subsurface, cloth and eye shading, and compiles each compact fragment for ESSL. Program-selection tests also remove the full graph artifact and require the compact graph program instead of a built-in material fallback. These tests do not substitute for running on a physical mobile device.

`RendererImageWorkflowTests.py --capture-only --material-layers` adds lacquer and scattering assets to the editor/package fixture. It checks saved/reloaded editor Play images against an isolated cooked package on the supported native backends and retains a missing-geometry negative control. This is a native Windows qualification workflow; shader compilation alone does not qualify mobile, Web or Metal runtime behavior.

The `--material-graph` fixture additionally cooks a scattering graph for WebGL and Android ETC2. The cooker omits its five-output `GBuffer/essl` artifact, requires `GBufferCompact/essl`, and reports the reduction. Package validation uses the same target rule and rejects a missing compact artifact.

`sources/game/tests/web/run_material_matrix_tests.py` compares twelve cooked materials in a hardware WebGL browser with native reference images for forward and deferred. The deferred references explicitly apply the documented four-target reductions. It checks every material region, framebuffer completeness, shader compilation, project lighting-path selection and runtime fallback diagnostics. Packaged hosts apply the saved project path and quality settings again after graphics recreation; fallback categories are reported once per runtime to bound log growth.
