#include "bgfx_compute.sh"

// One key per slot: authored view depth, distance or normalized age, with the slot as tie breaker.
// Dead slots and power-of-two padding sort after every live slot, including negative front-to-back keys.
BUFFER_RO(instances, vec4, 0);
BUFFER_WO(keys, vec4, 1);
BUFFER_RO(spawns, vec4, 2);

uniform vec4 u_gpuSortCamera; // xyz = camera position, w = slot count
uniform vec4 u_gpuSortDepth; // view-space depth row, including translation
uniform vec4 u_gpuSortParams; // x = padded count, y = mesh layout, z = sort mode, w = simulation time

NUM_THREADS(64, 1, 1)
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_gpuSortParams.x))
    {
        return;
    }
    float key = -3.402823e38;
    if (i < uint(u_gpuSortCamera.w))
    {
        vec3 position;
        bool live;
        if (u_gpuSortParams.y > 0.5)
        {
            // Mesh layout: the translation is the fourth column, a live instance has fade 1 in the third.
            live = instances[i * 5u + 2u].w > 0.5;
            position = instances[i * 5u + 3u].xyz;
        }
        else
        {
            vec4 instance = instances[i * 5u];
            live = instance.w > 0.0;
            position = instance.xyz;
        }
        if (live)
        {
            if (u_gpuSortParams.z < 2.5)
            {
                key = dot(vec4(position, 1.0), u_gpuSortDepth);
                if (u_gpuSortParams.z > 1.5) key = -key;
            }
            else if (u_gpuSortParams.z < 3.5)
            {
                vec3 offset = position - u_gpuSortCamera.xyz;
                key = dot(offset, offset);
            }
            else
            {
                float age = clamp((u_gpuSortParams.w - spawns[i * 2u].w) / max(spawns[i * 2u + 1u].w, 0.000001), 0.0, 1.0);
                key = floor(age * 65535.0 + 0.5);
            }
        }
    }
    keys[i] = vec4(key, float(i), 0.0, 0.0);
}
