#include "bgfx_compute.sh"

BUFFER_RO(instanceBounds, vec4, 0);
BUFFER_RO(instanceMetadata, vec4, 1);
BUFFER_RW(instancePredicates, uint, 2);
BUFFER_RW(visibleInstanceList, uint, 3);
BUFFER_RW(visibleInstanceCounters, uint, 4);

uniform vec4 u_gpuCullFrustum[6];
uniform vec4 u_gpuCullParams;

bool sphereInsideFrustum(vec4 bounds)
{
    bool visible = bounds.w > 0.0;
    UNROLL
    for (int planeIndex = 0; planeIndex < 6; ++planeIndex)
    {
        vec4 plane = u_gpuCullFrustum[planeIndex];
        float planeLength = dot(plane.xyz, plane.xyz);
        if (planeLength > 0.00001)
        {
            float distanceToPlane = dot(plane.xyz, bounds.xyz) + plane.w;
            visible = visible && distanceToPlane >= -bounds.w;
        }
    }
    return visible;
}

NUM_THREADS(64, 1, 1)
void main()
{
    uint instanceIndex = gl_GlobalInvocationID.x;
    uint instanceCount = uint(u_gpuCullParams.x);
    if (instanceIndex >= instanceCount)
    {
        return;
    }

    vec4 metadata = instanceMetadata[instanceIndex];
    vec4 bounds = instanceBounds[instanceIndex];
    bool visible = sphereInsideFrustum(bounds);
    instancePredicates[instanceIndex] = visible ? 1u : 0u;
    visibleInstanceList[instanceIndex] = visible ? instanceIndex : 0xffffffffu;
    if (visible)
    {
        atomicAdd(visibleInstanceCounters[0], 1u);
        visibleInstanceCounters[2] = uint(metadata.x);
    }
}
