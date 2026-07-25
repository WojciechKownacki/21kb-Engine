# Sprint 39 · Renderer — Detailed Engineering Tasks

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Close concrete implementation gaps in the renderer through code-level tasks with observable acceptance conditions, measured performance effects, regression coverage, and alignment with the renderer architecture established in Sprints 02 and 19.

## GPU Foundation, RHI & Bindless

- [ ] Implement a bindless resource layer that uploads all material albedo/normal/ORM textures into a single large descriptor array (or a texture-array atlas keyed by a per-instance texture index) so a full GBuffer pass can be drawn with one program and zero per-draw sampler rebinds, and report the achieved reduction in sampler-set calls in the submit stats.
- [ ] Add a persistent per-frame structured instance-data GPU buffer holding model matrix, previous-frame matrix, material index, and bounds for every proxy, indexed by instance id, so vertex shaders read transforms from the buffer instead of per-draw uniforms and the per-instance uniform upload path is eliminated.
- [ ] Introduce GPU timestamp query capture around each render-pass view and surface per-pass GPU milliseconds in the pass profile, so the pass-cost debug view reports measured GPU time rather than only estimated target bytes.
- [ ] Implement double- and triple-buffered upload ring buffers for the instance, light, and cull buffers with explicit frame-fence tracking, so dynamic buffers are never CPU-stalled waiting on in-flight GPU reads.

## GPU-Driven Rendering & Culling

- [ ] Build a Hi-Z depth pyramid pass that seeds from the current depth buffer and downsamples into a full mip chain, exposing the pyramid as a graph resource consumed by the cull pass.
- [ ] Implement a two-pass GPU occlusion cull that tests each instance's screen-space bounds against last frame's Hi-Z pyramid, draws the passing set, then re-tests false negatives against the current-frame Hi-Z and re-queues newly disoccluded instances so nothing pops in one frame.
- [ ] Replace the CPU-parity stub in the GPU-driven recorder with a real indirect execution path that writes draw-indirect structs from the compute visible list and submits with an indirect buffer, so visible-instance counts drive draws entirely on the GPU with no CPU readback.
- [ ] Implement GPU meshlet cluster culling by wiring the meshlet cull shader to test per-meshlet bounding cones and spheres against the frustum and Hi-Z, compacting surviving meshlets into an indirect draw list, and rasterizing them through the instanced path.
- [ ] Add a compute LOD-selection pass that picks each instance's LOD index from projected screen-space bounds and writes it into the indirect draw arguments, replacing any CPU LOD choice and reporting the selection count from GPU results.
- [ ] Implement per-instance GPU sorting of the visible list into front-to-back opaque and back-to-front transparent key order via a compute radix sort, so draw submission consumes an already-sorted indirect buffer instead of CPU sort keys.

## Shadows

- [ ] Implement cascaded shadow maps for the directional light by computing N log-uniform split frustums, rendering each into an atlas slice, and selecting/blending cascades per pixel in the deferred lighting shader, removing the current single-projection limitation.
- [ ] Implement a virtual shadow map system that allocates small shadow tiles into a sparse page pool addressed by a clipmap, marks pages dirty from moved casters and lights, and re-renders only dirty pages so shadow resolution tracks screen pixels without a fixed map size.
- [ ] Add point-light shadows via cube or dual-paraboloid depth rendering packed into the shadow atlas, and sample them in the lighting shader so point lights beyond the first slot cast shadows.
- [ ] Add spot-light shadows by rendering a perspective depth map per shadowing spot into an atlas slot and applying filtering in the lighting pass, extending shadowing beyond the single directional light.
- [ ] Implement the EVSM shadow filter path (currently only enumerated) by rendering moments into a float map, blurring them, and reconstructing visibility with Chebyshev bounds in the shader.
- [ ] Implement contact-hardening (PCSS) shadows (currently only enumerated) with a blocker-search plus penumbra-estimate filter kernel driven by light size.
- [ ] Implement screen-space contact shadows by ray-marching the depth buffer along the light vector for lights that request them, adding fine short-range occlusion the shadow map misses.

## Clustered & Tiled Lighting

- [ ] Implement a real clustered light-culling compute pass that builds a froxel grid, assigns each light to overlapping clusters into a per-cluster index list, and has the forward-plus and deferred shaders read the cluster list per pixel, so the clustered path culls lights instead of looping all of them.
- [ ] Implement analytic area-light shading using linearly-transformed cosines for rectangle, disk, and tube lights, which are currently packed but shaded as point lights, so they produce correct soft specular.
- [ ] Raise the light limit beyond the fixed uniform arrays by moving light data into a GPU structured buffer indexed by the cluster lists, so scenes with hundreds of lights render without the current cap.
- [ ] Add light-cookie and IES-profile support by sampling a projected texture or IES intensity map per spot and point light in the lighting shader, driven by a per-light texture index.

## Global Illumination & Reflections

- [ ] Wire the existing prefiltered environment, irradiance map, and BRDF LUT into the deferred and forward shaders so specular reflections and diffuse ambient come from real image-based lighting instead of the analytic hemisphere.
- [ ] Implement runtime prefiltering that convolves a captured or loaded cubemap into a GGX-prefiltered mip chain and an irradiance map on the GPU, and generate the split-sum BRDF LUT once at startup, producing the textures the IBL config expects.
- [ ] Implement parallax-corrected local reflection-probe blending by ray-intersecting the reflection vector against each probe's proxy volume and blending the nearest probes per pixel.
- [ ] Implement screen-space reflections as a Hi-Z ray-march over the GBuffer that produces a reflection color and confidence buffer and composites over IBL specular where the trace hits, falling back to probes on miss.
- [ ] Implement dynamic diffuse global illumination via an irradiance probe volume that traces or gathers radiance per probe, stores spherical-harmonic or octahedral irradiance, and samples it for indirect diffuse.
- [ ] Implement screen-space global illumination as a horizon-based indirect-bounce pass over the GBuffer that adds one bounce of colored indirect diffuse.
- [ ] Implement a baked-lightmap sampling path that reads a per-instance lightmap UV set and atlas texture for static diffuse global illumination.
- [ ] Implement ground-truth ambient occlusion as a compute pass over depth and normals producing a bent-normal and AO buffer that modulates IBL diffuse and specular.

## Materials & Advanced Shading Models

- [ ] Implement the subsurface-scattering shading model with a separable screen-space diffusion pass over a dedicated thickness/mask GBuffer channel, so skin and wax materials render translucently instead of as default-lit.
- [ ] Implement the clear-coat shading model by adding a second specular lobe with its own roughness and normal in the lighting shader, honoring the encoded shading model that is currently decoded but unused.
- [ ] Implement the cloth and hair shading models with sheen and Kajiya-Kay specular respectively, so the enumerated models produce distinct BRDFs instead of falling through to default-lit.
- [ ] Implement order-independent transparency for the transparent pass using per-pixel linked lists or weighted-blended OIT, so overlapping transparent surfaces composite correctly without CPU depth sorting.
- [ ] Add refraction and transmission for thin-translucent and single-layer-water models by sampling the scene color buffer with a roughness-driven blur offset by the surface normal, producing glass and water refraction.
- [ ] Add GPU per-object material-batch sorting that groups draws by pipeline-state key into contiguous ranges and issues one multidraw-indirect per state bucket, minimizing state changes across the whole opaque pass.

## Post-Processing, Anti-Aliasing & Upscaling

- [ ] Implement a temporal super-resolution upscaler that renders the scene at a fraction of output resolution, accumulates jittered samples with the existing motion vectors, and reconstructs full-resolution output with disocclusion rejection.
- [ ] Implement screen-space depth-of-field with a circle-of-confusion computation from the depth buffer plus a gather/bokeh blur pass driven by camera focal parameters.
- [ ] Implement per-object and camera motion blur that reconstructs blur from the motion-vector buffer already produced for temporal AA, tile-classifying max velocity and gathering along it.
- [ ] Add a color-grading pipeline that loads real 3D LUT assets into the color-grade slot and applies user grade strength, replacing the neutral-only LUT currently bound.
- [ ] Add lens post effects — chromatic aberration, vignette, and film grain — as a single parameterized fullscreen pass so final-image lens character is authorable.
- [ ] Implement physically based bloom with a progressive dual-filter mip pyramid replacing the single-radius blur, giving wide energy-conserving glare.
- [ ] Implement a physically based lens-flare and glare pass that thresholds bright HDR pixels and convolves them with a starburst and ghost kernel.

## Volumetrics, Atmosphere & Decals

- [ ] Implement volumetric fog as a froxel-grid compute pipeline that injects per-light scattering, ray-marches in-scattering with shadow sampling, and composites the result into the scene, activating light shafts that are currently flags-only.
- [ ] Implement a physically based sky-atmosphere model with transmittance and multiscatter lookup tables and aerial perspective, rendered before opaque and sampled by fog and IBL, giving time-of-day skies with no assets.
- [ ] Implement deferred decals that project material properties onto the GBuffer within decal box volumes during a dedicated pass between GBuffer and lighting, so surface detail can be layered without geometry.
- [ ] Implement height and exponential distance fog with a dedicated fullscreen pass that reconstructs world position from depth and blends a fog color, giving cheap atmospheric depth independent of the volumetric path.

## Frame Graph & Memory

- [ ] Implement physical transient-resource aliasing that allocates a single backing memory pool and binds aliased textures according to the compiler's existing aliasing plan, so the computed savings are realized instead of only estimated.
- [ ] Make the render pass set data-driven by allowing passes to be registered and inserted at runtime rather than a fixed enum, so decals, SSR, and volumetrics can add passes without editing the core enum.
- [ ] Emit explicit GPU barriers and transitions from the compiler's barrier list into the submission layer, so resource state transitions are graph-driven and validated rather than implicit in submit order.
- [ ] Add async-compute scheduling in the graph so independent compute passes (cull, Hi-Z, ambient occlusion, histogram) can run on a separate queue overlapping graphics work, reporting overlap in the pass profiles.

## HDR Output & Color Management

- [ ] Implement HDR display output that detects display HDR capability, requests a 10-bit or half-float backbuffer instead of the current LDR one, and applies the appropriate encode so tone mapping targets the display's real nit range.
- [ ] Implement a configurable display-referred tone-mapping curve with paper-white nits, peak nits, and hue-preserving desaturation in the output transform so HDR and SDR outputs share one grading pipeline with correct paper-white anchoring.

## Hardware Ray Tracing

- [ ] Add a ray-tracing acceleration-structure manager that builds and refits a per-mesh bottom-level structure and a per-frame scene top-level structure from the render proxies, exposed as a graph resource, gated on capability detection.
- [ ] Implement ray-traced shadows that trace visibility rays from GBuffer positions toward each shadowing light and denoise the result temporally, as an alternative to shadow maps where hardware supports it.
- [ ] Implement ray-traced reflections that trace from the GBuffer where screen-space reflections fail off-screen, shade hits with the deferred BRDF, and denoise, giving accurate off-screen reflections.
- [ ] Implement ray-traced ambient occlusion tracing short cosine-hemisphere rays against the acceleration structure as a higher-quality alternative to the screen-space pass.

## CPU Culling & Scene Systems

- [ ] Implement a two-level bounding-volume hierarchy or portal/occluder system on the CPU side of the scene extractor to reject whole sub-trees before per-instance GPU culling, reducing the instance count fed into the cull shader for large worlds.
- [ ] Add GPU-driven per-cluster small-triangle and back-face cone culling in the meshlet path so sub-pixel and back-facing meshlets are discarded before rasterization, cutting overdraw on dense meshes.
- [ ] Implement streaming virtual texturing that pages high-resolution material textures on demand based on GPU feedback of visible texel density, so large texture sets fit a fixed budget.

## Render Instrumentation

- [ ] Add a GPU-driven visibility and overdraw debug visualization that shades pixels by triangle or meshlet id or by overdraw count sourced from the GPU cull results, so culling correctness is inspectable.
- [ ] Implement a per-frame render-graph capture that serializes the compiled passes, barriers, aliases, and measured GPU timings to a file for offline inspection, making the graph's behavior verifiable in tests.
