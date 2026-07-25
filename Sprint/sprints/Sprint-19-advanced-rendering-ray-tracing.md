# Sprint 19 · Advanced Rendering & Ray Tracing

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Extend the validated renderer foundation with capability-gated ray tracing, advanced shadows and shading, reconstruction, wide-gamut HDR output, robust fallbacks, diagnostics, and platform-specific performance budgets.

## Ray-Tracing Foundation

- [ ] Add ray-tracing acceleration structures (bottom-level and top-level)
- [ ] Add build, refit, and compaction of acceleration structures
- [ ] Add per-instance transforms and masks in the top-level structure
- [ ] Add a ray-tracing pipeline with ray-generation, hit, and miss shaders
- [ ] Add inline ray queries for lightweight tracing
- [ ] Add ray payloads and recursion controls
- [ ] Add alpha-tested and transparent hit handling
- [ ] Add streaming and updating of acceleration structures for dynamic scenes
- [ ] Add skinned-mesh acceleration-structure updates
- [ ] Add a capability check and raster fallback where ray tracing is unavailable
- [ ] Add ray-tracing cost budgets and diagnostics
- [ ] Add ray-tracing debug visualization

## Ray-Traced Lighting Effects

- [ ] Add ray-traced reflections
- [ ] Add ray-traced shadows with soft penumbra
- [ ] Add ray-traced ambient occlusion
- [ ] Add ray-traced global illumination
- [ ] Add ray-traced translucency and refraction
- [ ] Add hybrid ray-traced and screen-space composition
- [ ] Add ray count and quality scaling
- [ ] Add a reference path tracer for validation and cinematics
- [ ] Add multi-bounce and importance sampling
- [ ] Add ray-traced-lighting debug visualization

## Ray-Traced Denoising

- [ ] Add a spatiotemporal denoiser for ray-traced signals
- [ ] Add separate denoisers for reflections, shadows, and global illumination
- [ ] Add temporal accumulation with history rejection
- [ ] Add variance-guided spatial filtering
- [ ] Add disocclusion handling
- [ ] Add firefly suppression
- [ ] Add denoiser quality presets
- [ ] Add denoiser debug visualization

## Virtual Shadow Maps

- [ ] Add virtual shadow maps with a page table
- [ ] Add on-demand shadow page allocation and residency
- [ ] Add per-pixel shadow resolution matched to screen density
- [ ] Add caching of static shadow pages
- [ ] Add invalidation on light or caster movement
- [ ] Add clip-map or cascade integration for directional lights
- [ ] Add page-pool budgets and eviction
- [ ] Add virtual-shadow-map debug visualization

## Advanced GPU Shading Features

- [ ] Add variable-rate shading with per-tile and per-material rates
- [ ] Add content-adaptive and motion-adaptive shading rates
- [ ] Add a mesh and amplification shader pipeline
- [ ] Add meshlet rendering through mesh shaders
- [ ] Add GPU work graphs for GPU-driven work expansion
- [ ] Add bindless resources and descriptor indexing
- [ ] Add a wave and subgroup intrinsics abstraction
- [ ] Add sampler-feedback-driven texture streaming
- [ ] Add streaming virtual textures for large material sets
- [ ] Add capability gating and fallbacks for each feature
- [ ] Add advanced-feature diagnostics

## Upscaling, Frame Generation & Reconstruction

- [ ] Add temporal super-resolution upscaling
- [ ] Add an integration layer for vendor hardware upscalers
- [ ] Add frame generation and interpolation
- [ ] Add reactive and transparency masks for reconstruction
- [ ] Add motion-vector and depth inputs for upscalers
- [ ] Add dynamic resolution feeding the upscaler
- [ ] Add sharpening and post-upscale filters
- [ ] Add latency management with frame generation
- [ ] Add quality and performance presets
- [ ] Add upscaling and frame-generation diagnostics

## HDR Display Output & Wide Gamut

- [ ] Add HDR display detection and capability query
- [ ] Add HDR10 and perceptual-quantizer output
- [ ] Add scRGB and extended-range output
- [ ] Add wide-gamut (Rec.2020 and DCI-P3) handling
- [ ] Add HDR tone-mapping and paper-white calibration
- [ ] Add an in-game HDR calibration screen
- [ ] Add UI and subtitle brightness handling in HDR
- [ ] Add graceful fallback to standard-dynamic-range output
- [ ] Add HDR-output validation and diagnostics
