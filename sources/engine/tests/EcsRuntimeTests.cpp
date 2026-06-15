#include "EcsTestSuites.hpp"
#include "TestSuites.hpp"

namespace kb::tests {

void RunEcsRuntimeTests() {
    RunEcsComponentApiTests();
    RunEcsQueryTests();
    RunEcsQuerySystemTests();
    RunEcsConfigTests();
    RunEcsEventTests();
    RunEcsInspectionTests();
    RunEcsNativeArchetypeStorageTests();
    RunEcsReflectionTests();
    RunEcsRelationTests();
    RunEcsSnapshotTests();
}

} // namespace kb::tests
