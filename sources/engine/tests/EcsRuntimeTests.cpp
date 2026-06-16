#include "EcsTestSuites.hpp"
#include "TestSuites.hpp"

namespace kb::tests {

void RunEcsRuntimeTests() {
    RunEcsComponentApiTests();
    RunEcsCommandBufferTests();
    RunEcsQueryTests();
    RunEcsQuerySystemTests();
    RunEcsConfigTests();
    RunEcsEventTests();
    RunEcsInspectionTests();
    RunEcsKernelTests();
    RunEcsNativeArchetypeStorageTests();
    RunEcsReflectionTests();
    RunEcsRelationTests();
    RunEcsSnapshotTests();
    RunEcsSystemSchedulerTests();
    RunEcsWorkerPoolTests();
    RunEcsDeterministicReplayTests();
    RunEcsSeededStressTests();
}

} // namespace kb::tests
