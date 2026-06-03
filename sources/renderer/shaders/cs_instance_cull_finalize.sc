#include "bgfx_compute.sh"

BUFFER_RW(visibleInstanceCounters, uint, 4);

uniform vec4 u_gpuCullParams;

NUM_THREADS(1, 1, 1)
void main()
{
    visibleInstanceCounters[1] = uint(u_gpuCullParams.x);
}
