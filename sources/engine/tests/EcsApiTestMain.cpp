#include "EcsTestSuites.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void RunSuite(const char* name, void (*run)()) {
    std::cerr << "ecs_api begin " << name << std::endl;
    run();
    std::cerr << "ecs_api passed " << name << std::endl;
}
} // namespace

int main() {
    RunSuite("component-api", kb::tests::RunEcsComponentApiTests);
    RunSuite("command-buffer", kb::tests::RunEcsCommandBufferTests);
    RunSuite("query", kb::tests::RunEcsQueryTests);
    RunSuite("query-system", kb::tests::RunEcsQuerySystemTests);
    RunSuite("config", kb::tests::RunEcsConfigTests);
    RunSuite("event", kb::tests::RunEcsEventTests);
    RunSuite("inspection", kb::tests::RunEcsInspectionTests);
    RunSuite("kernel", kb::tests::RunEcsKernelTests);
    RunSuite("native-storage", kb::tests::RunEcsNativeArchetypeStorageTests);
    RunSuite("reflection", kb::tests::RunEcsReflectionTests);
    RunSuite("relation", kb::tests::RunEcsRelationTests);
    RunSuite("snapshot", kb::tests::RunEcsSnapshotTests);
    return EXIT_SUCCESS;
}
