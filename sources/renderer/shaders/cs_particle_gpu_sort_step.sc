#include "bgfx_compute.sh"

// One compare-exchange step of a bitonic sort (descending by distance, ties by slot).
// u_gpuSortParams: x = padded count, y = block size k, z = compare distance j.
BUFFER_RW(keys, vec4, 0);

uniform vec4 u_gpuSortParams;

bool KeyLess(vec4 a, vec4 b)
{
    return a.x < b.x || (a.x == b.x && a.y < b.y);
}

NUM_THREADS(64, 1, 1)
void main()
{
    uint pair = gl_GlobalInvocationID.x;
    uint count = uint(u_gpuSortParams.x);
    if (pair >= count / 2u)
    {
        return;
    }
    uint k = uint(u_gpuSortParams.y);
    uint j = uint(u_gpuSortParams.z);
    uint lo = (pair / j) * (2u * j) + (pair % j);
    uint hi = lo + j;
    vec4 a = keys[lo];
    vec4 b = keys[hi];
    // Blocks alternate direction; the final k = count merge is descending.
    bool descending = (lo & k) == 0u;
    bool shouldSwap = descending ? KeyLess(a, b) : KeyLess(b, a);
    if (shouldSwap)
    {
        keys[lo] = b;
        keys[hi] = a;
    }
}
