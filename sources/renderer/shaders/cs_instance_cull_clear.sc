#include "bgfx_compute.sh"

BUFFER_WO(instancePredicates, uint, 2);
BUFFER_WO(visibleInstanceList, uint, 3);
BUFFER_WO(visibleInstanceCounters, uint, 4);

NUM_THREADS(1, 1, 1)
void main()
{
    instancePredicates[0] = 0u;
    visibleInstanceList[0] = 0u;
    visibleInstanceCounters[0] = 0u;
    visibleInstanceCounters[1] = 0u;
    visibleInstanceCounters[2] = 0u;
    visibleInstanceCounters[3] = 0u;
}
