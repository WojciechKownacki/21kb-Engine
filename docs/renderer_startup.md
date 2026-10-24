# Renderer startup

The renderer creates the GI and ambient-occlusion resolve program when a deferred
viewport first requests screen-space effects. A scene that does not use those
effects does not load or compile `fs_ssgi_resolve.sc` during renderer startup.
Enabling the effects later uses the same GPU program and history path.

Run `kb_renderer_tests gi-resolve` from the build directory, or use its registered
CTest case `kb_renderer_gi_resolve`. It checks the
initial shader requests through a real binary provider and hidden D3D11 renderer.
It then verifies screen-space bounce, temporal accumulation, ambient occlusion,
off-screen voxel contributions and shadowed voxel bounce through GPU readback.
The checks run in Release and Debug with AddressSanitizer.

The packaged WebGL host has also been traced with the unused GI program deferred.
This does not establish complete browser support: sky-capture shader compilation
still fails in the checked browser path, and GI enabled in a browser needs its own
rendering verification. The WebGL host's first-frame and full gameplay acceptance
remain open.
