#include "bgfx_compute.sh"

// Last step of sorting: copies each instance record to its sorted position.
BUFFER_RO(instances, vec4, 0);
BUFFER_RO(keys, vec4, 1);
BUFFER_WO(sorted, vec4, 2);

uniform vec4 u_gpuSortCamera; // w = slot count

NUM_THREADS(64, 1, 1)
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_gpuSortCamera.w))
    {
        return;
    }
    uint source = uint(keys[i].y);
    for (uint field = 0u; field < 5u; ++field)
    {
        sorted[i * 5u + field] = instances[source * 5u + field];
    }
}
