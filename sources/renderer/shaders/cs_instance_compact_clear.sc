#include "bgfx_compute.sh"

BUFFER_RAW_RW(drawArguments, 3);
uniform vec4 u_gpuCompactParams;

NUM_THREADS(1, 1, 1)
void main()
{
    // Raw words avoid backend-dependent typed indirect views.
    rawStoreUint(drawArguments, 0u, uint(u_gpuCompactParams.y));
    rawStoreUint(drawArguments, 1u, 0u);
    rawStoreUint(drawArguments, 2u, floatBitsToUint(bgfx_indirectArgBase.x) + uint(u_gpuCompactParams.z));
    rawStoreUint(drawArguments, 3u, 0u);
    rawStoreUint(drawArguments, 4u, 0u);
    rawStoreUint(drawArguments, 5u, 0u);
    rawStoreUint(drawArguments, 6u, 0u);
    rawStoreUint(drawArguments, 7u, 0u);
}
