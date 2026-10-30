# Renderer startup

The renderer creates the GI and ambient-occlusion resolve program when a deferred
viewport first requests screen-space effects. A scene that does not use those
effects does not load or compile `fs_ssgi_resolve.sc` during renderer startup.
Enabling the effects later uses the same GPU program and history path.

Sky reflection shaders and cube maps are also created on demand, when an active
sky backdrop first reaches the reflection pass. An absent backdrop keeps those
resources uninitialized. Capture and prefiltering then progress through the
normal frame sequence.

Run `kb_renderer_tests gi-resolve` from the build directory, or use its registered
CTest case `kb_renderer_gi_resolve`. It checks the
initial shader requests through a real binary provider and hidden D3D11 renderer.
It then verifies screen-space bounce, temporal accumulation, ambient occlusion,
off-screen voxel contributions and shadowed voxel bounce through GPU readback.
The checks run in Release and Debug with AddressSanitizer.

`kb_renderer_tests sky-reflection`, registered as `kb_renderer_sky_reflection`,
checks that startup and an absent backdrop request no sky shaders. It then
captures an authored constant gradient and reads the sharpest and roughest
reflection mips from the GPU. The absolute HDR channel tolerance is 0.005 to allow
half-float storage and prefilter accumulation.

The packaged WebGL host reaches its first frame with both unused lighting
programs deferred. This is an empty-scene startup check on the tested desktop
browser path. GI and sky reflections enabled in a browser still need their own
rendering verification, as do complete gameplay, input and lifecycle flows.


Post-process profiles use the same validation for load and atomic save. Bloom strength,
threshold and radius and exposure adaptation rates must be non-negative; soft knee is
0..1, gamma and luminance references are positive, and minimum exposure cannot exceed
maximum exposure. Invalid saves leave the previous file intact. Runtime bloom uniforms
are bounded to finite half-float range and the combined highlight is saturated at 65504
before writing RGBA16F, preventing HDR overflow before tone mapping.

The `post-process-profile` renderer test checks bloom halos and repeatability for None,
ACES and AgX output, including extreme strength. Its SSAO cases cover contact versus
open floor, thin occluders, proportional scene scale and camera samples. GPU timings
reported there measure the whole small test frame with and without AO after warmup;
they are not a general scene budget. SSAO uses visible depth and cannot represent
occluders absent from the depth buffer. `kb_editor_post_process_validation` exercises
headless GUI editing and CLI validation without damaging saved profiles.


Temporal antialiasing and transparency
------------------------------------

`kb_editor_animation_retarget_package` also compares 30-frame moving skeletal mesh,
particle and camera sequences between source assets and a cooked package with the
source directory unavailable. It covers perspective and orthographic TAA, reset-history
references, FXAA, and alpha, additive and premultiplied particles. A camera cut resets
history at frame 16. Pixel checks bound residual history in locally flat regions; they
do not promise zero blur at every moving silhouette. `kb_renderer_temporal_history`
checks resize, camera changes, skipped history and TAA toggling; `kb_renderer_display_alpha`
checks reactive coverage and the display pipeline. GPU sequence readbacks are queued
asynchronously so capturing images does not itself invalidate temporal history.

Transparent meshes use object-depth back-to-front sorting, not order-independent
transparency or per-pixel triangle sorting. Intersecting or cyclically overlapping
surfaces cannot be ordered correctly everywhere by one object order: split the geometry
when that matters, or use masked/opaque materials. Camera motion can change the order
when object depths cross. The `transparent-gpu-readback` test checks reference alpha
composition and deterministic camera round trips through intersecting surfaces.
Particles retain their selected blend/sort policies; additive blending is order
independent for RGB, while alpha and premultiplied layers require suitable sorting.
TAA uses the remaining opaque coverage as reactive history weight for transparency;
transparent motion has no independent per-layer motion vector. Fast, thin transparent
edges may shimmer or soften; FXAA avoids temporal history at the cost of less temporal
stability. These are supported rendering limits, not claims of exact transparency.


Depth-dependent particle outputs
-------------------------------

The particle editor exposes both Volumetric and Decal output choices. Their serialized
payloads use the same CLI validation as other outputs. Decals project into a square
box rotated around the vertical axis; `depth` controls its vertical extent. Both decals
and volumetric particles skip drawing when sampled opaque depth is unavailable; they
do not substitute a billboard or read a stale depth texture.

`kb_renderer_particle_depth_outputs` reads actual GPU pixels for overlapping particles,
particle sizes, decal quarter/diagonal turns, depth loss after valid frames, opaque
occlusion and render target resize/restore. The restored target must match its original
reference image. Volumetric low/high quality retain their authored raymarch step counts.
The reported timings compare complete 64x64 frames after warmup with one smaller volume
and eight larger overlapping volumes; they measure the effect of increased fill work
on that fixture, not isolated shader cost or a general hardware performance budget.
