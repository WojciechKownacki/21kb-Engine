#include "TestSuites.hpp"

#include <cstdlib>

int main() {
    kb::tests::RunAssetRuntimeTests();
    kb::tests::RunEcsRuntimeTests();
    kb::tests::RunSceneHierarchyTests();
    kb::tests::RunSceneSystemTests();
    kb::tests::RunScenePrefabTests();
    kb::tests::RunScriptRuntimeTests();
    kb::tests::RunVisualGraphTests();
    return EXIT_SUCCESS;
}
