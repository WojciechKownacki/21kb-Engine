# GPU frustum culling

Enable GPU submission with `Renderer::SetGpuDrivenRuntimeDispatchEnabled(true)`.
The viewport submission must also permit `gpuDrivenRuntimeDispatchEnabled`.
The editor's GPU-driven rendering setting controls the same runtime switch.
Mesh resources use the existing cooked GPU-driven mesh metadata and
`RenderGpuDrivenMeshDesc::allowGpuCulling` / `allowIndirectDraws` flags.
Compute and indexed indirect draw support, and both compaction programs, are
required. Unsupported backends retain CPU visibility and ordinary draws.

Opaque, GBuffer, depth and shadow passes compact their candidate instances into
the standard mesh instance layout. A clear dispatch initializes an indexed
indirect command; the compaction dispatch increments its instance count and
copies each surviving instance. The draw consumes both outputs on the GPU.
The runtime never waits for a visibility readback. Transparent ordering,
selection and motion-vector passes keep their CPU visibility contract.
Bounded draw/instance admission also keeps CPU culling before applying budgets.

CPU and GPU use the same normalized sphere/frustum planes, clip-depth convention,
unjittered projection and temporal guard band. Material displacement margins and
section/instance bounds are resolved before dispatch. Invalid bounds remain
conservative. Portals, visibility blockers, layer masks, LOD and fading retain
their existing authoring behavior.

Static batches without camera-dependent LOD/fading can reuse candidate commands
while the camera moves. Instance input uploads are cached by retained command
revision. Scene, binding and resource changes invalidate that revision. Each
view and submission owns a separate output slot through the frame; `TickFrame`
recycles slots after all cameras have submitted. Shutdown releases input,
instance and indirect buffers using the renderer's deferred resource lifetime.
Custom users of `SceneGpuDrivenCullingPass::Compact` call `EndFrame` once after
all submissions. An allocation or size-limit failure retains ordinary drawing.

Submission counters describe admitted candidates; the actual visible instance
count resides in the indirect command. `gpuCullingDispatchCount` counts compute
dispatches, `gpuDrivenUploadBytes` reports input transfers and
`meshCommandReuseCount` reports retained candidate commands. These counters alone
do not measure a speedup.

Run `kb_renderer_tests gpu-frustum-culling` on a configured Windows graphics host.
The focused check reads back actual indirect counts and compacted identities for
moving cameras, reverse-Z, guard bands and clip boundaries, compares complete
production images with CPU visibility, checks scene invalidation, and prints
submission and frame/readback costs for a dense static scene.
