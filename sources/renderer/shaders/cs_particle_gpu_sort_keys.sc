#include "bgfx_compute.sh"

// First step of sorting a GPU emitter's instances back to front: one key per slot, x = distance to the
// camera, y = slot. (Compute views of vertex buffers are float4 typed in Direct3D 11, so the key is a vec4.)
// Dead slots (zero-sized instances) get distance 0 and the padding up to a power of two gets -1, so
// after a descending sort the first `count` keys are the real slots, farthest live particle first.
BUFFER_RO(instances, vec4, 0);
BUFFER_WO(keys, vec4, 1);

uniform vec4 u_gpuSortCamera; // xyz = camera position, w = slot count
uniform vec4 u_gpuSortParams; // x = padded count, y = 1 when the instances are in the mesh layout

NUM_THREADS(64, 1, 1)
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_gpuSortParams.x))
    {
        return;
    }
    float key = -1.0;
    if (i < uint(u_gpuSortCamera.w))
    {
        if (u_gpuSortParams.y > 0.5)
        {
            // Mesh layout: the translation is the fourth column, a live instance has fade 1 in the third.
            bool live = instances[i * 5u + 2u].w > 0.5;
            key = live ? distance(instances[i * 5u + 3u].xyz, u_gpuSortCamera.xyz) : 0.0;
        }
        else
        {
            vec4 instance = instances[i * 5u];
            key = instance.w > 0.0 ? distance(instance.xyz, u_gpuSortCamera.xyz) : 0.0;
        }
    }
    keys[i] = vec4(key, float(i), 0.0, 0.0);
}
