#include "EcsTestSuites.hpp"

#include <cstdlib>

int main() {
    kb::tests::RunEcsComponentApiTests();
    kb::tests::RunEcsCommandBufferTests();
    kb::tests::RunEcsQueryTests();
    kb::tests::RunEcsQuerySystemTests();
    kb::tests::RunEcsConfigTests();
    kb::tests::RunEcsEventTests();
    kb::tests::RunEcsInspectionTests();
    kb::tests::RunEcsKernelTests();
    kb::tests::RunEcsNativeArchetypeStorageTests();
    kb::tests::RunEcsReflectionTests();
    kb::tests::RunEcsRelationTests();
    kb::tests::RunEcsSnapshotTests();
    return EXIT_SUCCESS;
}
