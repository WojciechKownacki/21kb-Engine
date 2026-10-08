#include "ScenePrefabTestSuites.hpp"
#include "TestSuites.hpp"
#include "TestSupport.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "scene/prefab/io/ScenePrefabAssetReader.hpp"

#include <sstream>

namespace kb::tests {

void RunSceneStreamingTests();

namespace {

// A prefab file's node count is a claim the nodes have yet to back up. This one
// promises nearly half a million nodes and then ends; reading it must fail without
// first making room for all of them. Found by fuzzing (fuzz/corpus/prefab).
void RunPrefabDeclaredNodeCountIsNotReservedTest() {
    std::istringstream input{ "21kb.prefab.v1\nname=\nnodes=442949" };
    kb::scene::ScenePrefabAssetReadResult result;
    BeginAllocationTally();
    const bool read = kb::scene::ScenePrefabAssetReader::Read(input, result);
    const AllocationTally tally = EndAllocationTally();
    Require(!read, "A prefab that ends before its declared nodes was accepted");
    Require(tally.bytes < 32U * 1024U * 1024U, "A prefab's declared node count was reserved before its nodes were read");
}

// The same claim one level down: a node announcing 29 million nested overrides it
// never spells out (found by fuzzing, fuzz/corpus/prefab).
void RunPrefabDeclaredOverrideCountIsNotReservedTest() {
    std::istringstream input{ "21kb.prefab.v2\nkind=template\nguid=\nname=\nnodes=4\nnode\nname=\n"
                              "nestedOverrideCount=29467295\nendnode\n" };
    kb::scene::ScenePrefabAssetReadResult result;
    BeginAllocationTally();
    const bool read = kb::scene::ScenePrefabAssetReader::Read(input, result);
    const AllocationTally tally = EndAllocationTally();
    Require(!read, "A prefab node with undeclared overrides was accepted");
    Require(tally.bytes < 32U * 1024U * 1024U, "A prefab node's declared override count was reserved before its overrides were read");
}

} // namespace

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
    RunPrefabDeclaredNodeCountIsNotReservedTest();
    RunPrefabDeclaredOverrideCountIsNotReservedTest();
    RunScenePrefabInstantiationTests();
    RunScenePrefabCaptureTests();
}

} // namespace kb::tests
