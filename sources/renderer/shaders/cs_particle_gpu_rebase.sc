#include "bgfx_compute.sh"

// Moves the live particles of a world-space GPU emitter with the render origin (docs/large_worlds.md). The
// records hold positions relative to the render origin; when the origin moves, every slot's position (the xyz of
// the first of its two records, in the birth records and in the collision state alike) is moved by the old origin
// minus the new one, so the particles stay where they are in the world without being cleared.
// u_gpuParticleRebase: xyz = old render origin minus new, w = slot count.
BUFFER_RW(records, vec4, 0);

uniform vec4 u_gpuParticleRebase;

NUM_THREADS(64, 1, 1)
void main()
{
    uint slot = gl_GlobalInvocationID.x;
    if (slot >= uint(u_gpuParticleRebase.w))
    {
        return;
    }
    vec4 position = records[slot * 2u];
    records[slot * 2u] = vec4(position.xyz + u_gpuParticleRebase.xyz, position.w);
}
