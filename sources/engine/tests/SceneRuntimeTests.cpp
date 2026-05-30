#include "TestSuites.hpp"

#include <cstdlib>

int main() {
    kb::tests::RunEcsRuntimeTests();
    kb::tests::RunSceneHierarchyTests();
    kb::tests::RunSceneSystemTests();
    kb::tests::RunScenePrefabTests();
    return EXIT_SUCCESS;
}
