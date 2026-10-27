#include "bgfx_compute.sh"

// Diagnostic copy of the actual compute-written render records, without CPU simulation or conversion.
BUFFER_RO(particleInspectSource, vec4, 0);
IMAGE2D_WO(particleInspectTarget, rgba32f, 1);
uniform vec4 u_particleInspect; // x = number of slots to inspect, one slot per texture row

NUM_THREADS(64, 1, 1)
void main()
{
    uint slot = gl_GlobalInvocationID.x;
    if (slot >= uint(u_particleInspect.x)) return;
    for (int lane = 0; lane < 5; ++lane)
    {
        imageStore(particleInspectTarget, ivec2(lane, int(slot)), particleInspectSource[slot * 5u + uint(lane)]);
    }
}
