# Native compute and render effect plugins

`kb/render/effects/RenderEffectApi.hpp` defines the version 1 native plugin
boundary. It contains fixed-width descriptors, opaque owned handles and function
pointers. Plugin C++ code does not include or link bgfx. The separate project in
`samples/RenderEffectPlugin` builds an example module using only that public
header. Its compute shader writes an image and adds 42 to an input buffer; the
plugin verifies the actual GPU result asynchronously.

Build the example as an ordinary external CMake project:

```sh
cmake -S samples/RenderEffectPlugin -B effect-build -DKB_RENDER_EFFECT_INCLUDE_DIR=<sdk-renderer-include>
cmake --build effect-build --config Release --target sample_render_effect
```

Compile `cs_gradient.sc` with the SDK shader compiler and shader include directory
for each required backend, for example `--type compute --platform windows
--profile s_5_0` for `dxbc`. The supported manifest profile names are `dxbc`,
`dxil`, `spirv`, `glsl`, `essl`, `metal` and `wgsl`. The native library must match
the target platform. Place the library and compiled shaders below the manifest's
directory, using a UTF-8 manifest such as:

```text
21kb-render-effect 1
library "sample_render_effect.dll"
shader "gradient" "dxbc" "gradient.dxbc.bin"
shader "gradient" "spirv" "gradient.spirv.bin"
```

Export copies the native library and compiled variants into an independent
directory, publishes a new relative-path manifest, and refuses an existing output.
Runtime loading uses those exported files; shader sources and development caches
are unnecessary. No effect-specific engine source changes are needed.

```sh
kb_cli render-effect-export --manifest Effect/effect.kbfx --output ExportedEffect
kb_cli render-effect-run --manifest ExportedEffect/effect.kbfx --frames 8
```

The run command uses a hidden Windows graphics host and reports dispatches,
presentations, completed readbacks, the plugin's status and owned resources after
unload as JSON. An application loads the same bundle through
`renderer.RenderEffects().Load(manifest, error)`. The renderer executes loaded
effects at the end of each frame, after scene composition and before capture.
For custom viewport hosts, `RenderEffectHost::Run` accepts an explicit target,
view range and optional scene input. The automatic display effect input refers
to the renderer's primary scene target; custom viewport hosts supply their own.

The plugin exports `kb_create_render_effect_v1`. It checks the host API size and
version and returns its own size, version, instance and lifecycle callbacks.
Callbacks run on the renderer thread. `onDestroy` executes before the native
library is unmapped. Native code is trusted: loading uses the existing engine
module loader, including verified-release checks in signed applications. Include
the exported native library in the application's normal release manifest.

The host owns programs, uint/vec4 buffers, RGBA8 storage textures, value/sampler
uniforms and readbacks. Handles are scoped to one plugin and never reused within
the host lifetime. Invalid or stale ownership is refused. Input buffers can be
initialized or updated from CPU memory before their first dispatch in a frame;
updates after dispatch are refused. Storage buffers receive compute writes.
Value uniforms use `u_fx_` names and sampler uniforms use `s_fx_` names. Dispatch
descriptors declare binding access; the backend orders dispatches and transitions
compute output into texture presentation. Borrowed scene color is sampled only
inside the current frame callback and cannot be released by the plugin.

Readback captures the buffer after the frame's queued GPU work into host-owned storage. Polling returns pending, complete or
invalid; a plugin unload can retire a pending readback without leaving pointers
into unloaded native code. Unload also releases resources the plugin did not
explicitly release. Renderer shutdown drains remaining readback fences before
destroying their storage and the graphics context. Creation failures roll back
partial resources and the native module.

One host permits six plugins, 4,096 live resources, buffers of at most 1,048,576
elements, and textures of at most 4,096 pixels per dimension within hardware
limits. Missing compute support, shader variants, image-write formats and invalid
bindings return explicit errors. Callbacks must not throw or reenter load/unload.

`kb_renderer_tests render-effect-plugin <exported-effect.kbfx>` checks source-free
loading, compute-to-image synchronization, buffer readback, resize, unload while
readback is pending, unsupported manifests and partial factory rollback. The
sample's independent build and both CLI commands exercise the public workflow.
