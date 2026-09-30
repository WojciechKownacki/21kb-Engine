#include "ScenePrefabTestSuites.hpp"
#include "TestSuites.hpp"
#include "TestSupport.hpp"
#include "engine/scene/ScenePrefab.hpp"

namespace kb::tests {

void RunSceneStreamingTests();

void RunScenePrefabTests() {
    RunSceneStreamingTests();
    kb::scene::ScenePrefab prefab;
    prefab.Reserve(100002U);
    for (std::uint32_t index = 0U; index < 100000U; ++index) {
        static_cast<void>(prefab.AddNode({}));
    }
    Require(prefab.Nodes().back().stableId == 100000U, "Large prefab ids must remain unique");
    auto* node = prefab.TryGetMutableNode(0U);
    node->stableId = 200000U;
    static_cast<void>(prefab.AddNode({}));
    node->stableId = 300000U;
    static_cast<void>(prefab.AddNode({}));
    Require(prefab.Nodes().back().stableId == 300001U, "Retained mutable node edits must invalidate automatic ids");
    prefab.Clear();
    static_cast<void>(prefab.AddNode({}));
    Require(prefab.Nodes().front().stableId == 1U, "Cleared prefab must restart id allocation");
    RunScenePrefabInstantiationTests();
    RunScenePrefabCaptureTests();
}

} // namespace kb::tests
