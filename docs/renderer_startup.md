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
