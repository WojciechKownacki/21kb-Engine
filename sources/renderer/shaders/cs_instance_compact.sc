#include "bgfx_compute.sh"

// Six vec4 input rows: world sphere, affine material lanes, instance color.
BUFFER_RO(sourceInstances, vec4, 0);
BUFFER_RW(compactInstances, vec4, 1);
BUFFER_RAW_RW(drawArguments, 3);
uniform vec4 u_gpuCullFrustum[6];
uniform vec4 u_gpuCompactParams;

NUM_THREADS(64, 1, 1)
void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= uint(u_gpuCompactParams.x)) return;
    vec4 sphere = sourceInstances[index * 6u];
    bool visible = true;
    // Invalid bounds remain conservative, just like the CPU classifier.
    if (sphere.w > 0.0)
    {
        UNROLL
        for (int p = 0; p < 6; ++p)
        {
            visible = visible && dot(u_gpuCullFrustum[p].xyz, sphere.xyz) + u_gpuCullFrustum[p].w >= -sphere.w;
        }
    }
    if (!visible) return;
    uint slot;
#if BGFX_SHADER_LANGUAGE_HLSL && !(BGFX_SHADER_LANGUAGE_METAL || BGFX_SHADER_LANGUAGE_SPIRV || BGFX_SHADER_LANGUAGE_WGSL)
    drawArguments.InterlockedAdd(4u, 1u, slot);
#else
    atomicFetchAndAdd(drawArguments[1], 1u, slot);
#endif
    for (uint row = 0u; row < 5u; ++row)
    {
        compactInstances[slot * 5u + row] = sourceInstances[index * 6u + 1u + row];
    }
}
