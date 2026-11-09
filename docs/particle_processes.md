# Custom GPU particle processes

`particle-process-cook --source Custom.particle.sc --output Custom.kbpsim --profile all`
compiles an authored simulation without an engine fork. `--shaderc` overrides the
configured compiler. A single named profile can be cooked for a local preview;
project cooking refuses a process missing any variant required by its target.
Shader errors retain the authored filename and line. Failed compilation leaves
an existing output intact. The compiled asset contains bytecode and the source
basename, with no runtime compiler or source-file dependency.

Set `effect.backendPolicy GpuVisualRequired` and the emitter's optional
`customProcess.path "/Game/Custom.kbpsim"` (or `customProcess.assetId`) in a
`.kbvfx` effect. `vfx-set` accepts these same serialized keys. The particle editor
provides **Custom simulation (.kbpsim)** in the output inspector and sets the
required GPU policy when assigning it. The asset participates in dependency
validation, hot reload, project cooking and immutable runtime packages.

Contract version 1 supports Billboard output and compute-capable renderers.
Existing GPU eligibility rules still apply; event actions needing CPU positions
are unavailable with a custom process. Other outputs or CPU policies are
rejected during authoring validation; missing
bytecode, GPU capability or allocation budget is reported as
`custom-process-unavailable`. A failed live replacement retains the accepted
program and state. Rebuilt providers use engine module ABI 25.

```c
void particle_process(inout ParticleProcess p, float dt, float timeSeconds) {
    p.custom.x += dt;
    p.position.x += 2.0 * dt;
    p.velocity.y = 3.0;
    p.color = vec4(0.25, 0.5, 0.75, 1.0);
    p.size = 0.5 + p.custom.x;
}
```

The engine invokes the function after its stateful motion integration, before
sorting and drawing. `position` and `velocity` are in render-relative world
coordinates, also for local-space emitters; the engine converts them back into
owner space for persistent integration. `size`, `rotation` and `color` affect
the rendered instance. `age` and `lifetime` are read-only inputs by convention;
changing them does not change birth/death scheduling. `custom` is a persistent
vec4 per slot, reset on every birth, restart and program replacement. `dt` is the
nonnegative elapsed process time; repeated dispatch at the same simulation time
passes zero. Built-in motion retains its bounded integration interval.

The wrapper owns the entry point, 64-thread dispatch, slot bounds and four
bindings (birth records, rendered instances, motion state, private custom data).
Authors should use the function's fields instead of accessing wrapper buffers.
Shaders are trusted executable GPU code, not a security sandbox. Arbitrary
external textures and buffer bindings are outside this version's contract.
Each emitter owns its program and custom buffers; scene release, replacement
and renderer shutdown retire them through the graphics command queue. The
custom state and its reset buffer add 64 requested bytes per slot, accounted
against the existing emitter allocation budget. A process also requires the
64-byte motion/reset state per slot when it was not already allocated for forces
or collision. Renderer preparation publishes GPU readiness before the first
required-GPU tick; editor previews wait for a real render consumer. Runtime execution performs no
readback. `samples/ParticleProcess.particle.sc` is a standalone example.
