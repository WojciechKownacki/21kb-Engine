#include "ScenePrefabTestSuites.hpp"
#include "TestSuites.hpp"

#include <cstdlib>
#include <string_view>

namespace {

bool RunSuite(std::string_view suite) {
    if (suite == "assets") {
        kb::tests::RunAssetRuntimeTests();
    } else if (suite == "ecs") {
        kb::tests::RunEcsRuntimeTests();
    } else if (suite == "scene-hierarchy") {
        kb::tests::RunSceneHierarchyTests();
    } else if (suite == "scene-system") {
        kb::tests::RunSceneSystemTests();
    } else if (suite == "scene-prefab") {
        kb::tests::RunScenePrefabTests();
    } else if (suite == "scene-prefab-instantiation") {
        kb::tests::RunScenePrefabInstantiationTests();
    } else if (suite == "scene-prefab-capture") {
        kb::tests::RunScenePrefabCaptureTests();
    } else if (suite == "project-scene") {
        kb::tests::RunProjectSceneTests();
    } else if (suite == "script") {
        kb::tests::RunScriptRuntimeTests();
    } else if (suite == "script-api") {
        kb::tests::RunScriptApiCatalogTests();
    } else if (suite == "visual-graph") {
        kb::tests::RunVisualGraphTests();
    } else if (suite == "input") {
        kb::tests::RunInputTests();
    } else if (suite == "engine-module") {
        kb::tests::RunEngineModuleTests();
    } else {
        return false;
    }
    return true;
}

void RunAllSuites() {
    kb::tests::RunAssetRuntimeTests();
    kb::tests::RunEcsRuntimeTests();
    kb::tests::RunSceneHierarchyTests();
    kb::tests::RunSceneSystemTests();
    kb::tests::RunScenePrefabTests();
    kb::tests::RunProjectSceneTests();
    kb::tests::RunScriptRuntimeTests();
    kb::tests::RunScriptApiCatalogTests();
    kb::tests::RunVisualGraphTests();
    kb::tests::RunInputTests();
    kb::tests::RunEngineModuleTests();
}

} // namespace

int main(int argc, char** argv) {
    if (argc <= 1) {
        RunAllSuites();
        return EXIT_SUCCESS;
    }

    for (int index = 1; index < argc; ++index) {
        if (!RunSuite(argv[index])) {
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
