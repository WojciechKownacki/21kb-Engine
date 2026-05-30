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
    RunEcsRelationTests();
    RunEcsSnapshotTests();
}

} // namespace kb::tests
