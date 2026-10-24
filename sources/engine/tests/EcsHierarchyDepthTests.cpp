#include "TestSupport.hpp"
#include "engine/ecs/World.hpp"
#include "ecs/relation/HierarchyDepthGuard.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneHistory.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <iostream>
#include <array>
#include <vector>
#include <stdexcept>

namespace kb::tests {

void RunEcsHierarchyDepthTests() {
    kb::ecs::World world;
    constexpr std::size_t depthLimit = 8192U;
    kb::tests::Require(kb::ecs::HierarchyDepthLimit() == depthLimit, "The supported entity hierarchy depth must remain 8192 parent links");
    std::vector<kb::ecs::Entity> chain;
    chain.reserve(depthLimit + 1U);
    chain.push_back(world.CreateEntity());
    for (std::size_t depth = 1U; depth <= depthLimit; ++depth) {
        const auto child = world.CreateEntity();
        world.SetParent(child, chain.back());
        kb::tests::Require(world.Parent(child) == chain.back(), "The supported hierarchy depth must remain usable");
        chain.push_back(child);
    }
    std::cout << "ecs_hierarchy_depth supported=" << depthLimit << std::endl;
    const auto extra = world.CreateEntity();
    world.SetParent(extra, chain.back());
    kb::tests::Require(!world.Parent(extra).IsValid(), "Excessive hierarchy depth must be rejected before backend mutation");
    world.SetParent(extra, chain.front());
    const auto above = world.CreateEntity();
    world.SetParent(chain.front(), above);
    kb::tests::Require(!world.Parent(chain.front()).IsValid(), "Hierarchy depth validation must include the child's descendants");
    const std::array children{extra, chain.front()};
    const std::array parents{chain.back(), above};
    world.SetParents(children, parents);
    kb::tests::Require(world.Parent(extra) == chain.front() && !world.Parent(chain.front()).IsValid(),
        "Rejected batch parenting must preserve old parents and account for descendants");
    const auto branch = world.CreateEntity();
    const auto branchLeaf = world.CreateEntity();
    world.SetParent(branchLeaf, branch);
    const auto retiredChild = world.CreateEntity();
    world.SetParent(retiredChild, branchLeaf);
    world.DestroyEntity(retiredChild);
    world.SetParent(branch, chain[depthLimit - 2U]);
    kb::tests::Require(world.Parent(branch) == chain[depthLimit - 2U] && world.Parent(branchLeaf) == branch,
        "A subtree ending exactly at the supported depth must remain usable");
    world.SetParent(branch, chain[depthLimit - 1U]);
    kb::tests::Require(world.Parent(branch) == chain[depthLimit - 2U],
        "Excessive subtree depth must preserve the previously assigned parent");
    const std::array mixedChildren{extra, branch};
    const std::array mixedParents{chain.back(), chain[depthLimit - 3U]};
    world.SetParents(mixedChildren, mixedParents);
    kb::tests::Require(world.Parent(extra) == chain.front() && world.Parent(branch) == chain[depthLimit - 3U],
        "Batch depth validation must retain supported edges and refuse excessive edges in order");
    world.DestroyEntity(branchLeaf);
    world.DestroyEntity(branch);
    world.DestroyEntity(extra);
    world.DestroyEntity(above);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) world.DestroyEntity(*it);
}

void RunSceneHierarchyDepthTests() {
    constexpr std::uint32_t depthLimit = 8192U;
    kb::scene::ScenePrefab prefab;
    prefab.Reserve(depthLimit + 1U);
    auto previous = prefab.AddNode({});
    for (std::uint32_t depth = 1U; depth <= depthLimit; ++depth) previous = prefab.AddNode({.parentNode = previous});
    for (const bool mirrored : {true, false}) {
        kb::ecs::WorldConfig config;
        config.mirrorEntitiesToBackend = mirrored;
        config.mirrorNativeComponentChangesToBackend = mirrored;
        kb::scene::Scene scene{config};
        const auto instance = scene.Prefabs().Instantiate(prefab, {.assignNames = false, .syncWorldHierarchy = mirrored});
        kb::tests::Require(instance.ObjectCount() == depthLimit + 1U, "Scene depth fixture must instantiate every public prefab node");
        const auto root = instance.RootObject();
        const auto leaf = instance.ObjectAt(depthLimit);
        const auto leafParent = instance.ObjectAt(depthLimit - 1U);
        kb::tests::Require(scene.Hierarchy().Parent(leaf.Entity()) == leafParent.Entity(), "Scene depth fixture must retain its deepest parent");
        const auto backendParent = kb::scene::SceneAccess::State(scene).world.Parent(leaf.Entity());
        kb::tests::Require(mirrored ? backendParent == leafParent.Entity() : !backendParent.IsValid(),
            "Depth fixture must exercise the requested actual backend or native-only hierarchy");
        std::cout << "scene_hierarchy_depth supported=" << depthLimit << " mirrored=" << mirrored << std::endl;
        const auto above = scene.Entities().CreateObject();
        kb::tests::Require(scene.History().Record(), "Scene depth history fixture must open a command");
        const auto undoCount = scene.History().UndoCount();
        const auto bytes = scene.History().RecordedBytes();
        const auto count = scene.Entities().Count();
        const auto roots = scene.Hierarchy().RootEntities();
        for (int form = 0; form < 3; ++form) {
            bool rejected = false;
            try {
                if (form == 0) static_cast<void>(scene.Entities().CreateObject({.parent = leaf}));
                if (form == 1) static_cast<void>(scene.Entities().CreateEntity({.parent = leaf}));
                if (form == 2) {
                    const std::array<kb::scene::SceneObjectDesc, 2U> descs{{{.parent = above}, {.parent = leaf}}};
                    static_cast<void>(scene.Entities().CreateObjects(descs));
                }
            } catch (const std::invalid_argument&) { rejected = true; }
            kb::tests::Require(rejected && scene.Entities().Count() == count && scene.Hierarchy().RootEntities() == roots,
                "Excessive scene creation depth must refuse the whole request before allocating entities");
        }
        kb::tests::Require(!scene.Hierarchy().SetParent(root, above) && !scene.Hierarchy().Parent(root.Entity()).IsValid(),
            "Scene reparenting must include subtree height in the depth limit");
        kb::tests::Require(scene.History().UndoCount() == undoCount && scene.History().RecordedBytes() == bytes,
            "Rejected scene depth changes must not record history");
        scene.History().Commit();
        const auto supported = scene.Entities().CreateObject({.parent = leafParent});
        kb::tests::Require(scene.Hierarchy().Parent(supported.Entity()) == leafParent.Entity(),
            "Scene creation at exactly the supported depth must remain usable");
        scene.Entities().Destroy(supported);
        const auto branch = scene.Entities().CreateObject();
        const auto branchLeaf = scene.Entities().CreateObject({.parent = branch});
        kb::tests::Require(scene.Hierarchy().SetParent(branch, instance.ObjectAt(depthLimit - 2U)),
            "A scene subtree ending exactly at the supported depth must remain movable");
        kb::tests::Require(!scene.Hierarchy().SetParent(branch, leafParent) &&
            scene.Hierarchy().Parent(branch.Entity()) == instance.ObjectAt(depthLimit - 2U).Entity() &&
            scene.Hierarchy().Parent(branchLeaf.Entity()) == branch.Entity(),
            "An excessive scene subtree move must preserve every existing link");
        scene.Entities().Destroy(root);
        kb::tests::Require(scene.Entities().Count() == 1U && scene.Entities().IsAlive(above.Entity()) && !scene.Entities().IsAlive(leaf.Entity()),
            "Destroying a supported deep scene subtree must retain unrelated roots");
    }
}

void RunPrefabHierarchyDepthTests() {
    constexpr std::uint32_t depthLimit = 8192U;
    kb::scene::ScenePrefab base;
    base.Reserve(depthLimit);
    auto previous = base.AddNode({});
    for (std::uint32_t depth = 1U; depth < depthLimit; ++depth) previous = base.AddNode({.parentNode = previous});
    kb::scene::ScenePrefab prefab;
    const auto prefabRoot = prefab.AddNode({});
    static_cast<void>(prefab.AddNode({.parentNode = prefabRoot}));
    struct Mode { bool mirrored; bool sync; };
    for (const auto mode : std::array<Mode, 3U>{{{true, false}, {true, true}, {false, false}}}) {
        kb::ecs::WorldConfig config;
        config.mirrorEntitiesToBackend = mode.mirrored;
        config.mirrorNativeComponentChangesToBackend = mode.mirrored;
        kb::scene::Scene scene{config};
        const auto instance = scene.Prefabs().Instantiate(base, {.assignNames = false, .syncWorldHierarchy = mode.sync});
        kb::tests::Require(instance.ObjectCount() == depthLimit, "Prefab depth fixture must retain its complete supported hierarchy");
        const auto registered = scene.Prefabs().Register("DepthBoundary", prefab);
        kb::tests::Require(registered.IsValid(), "A shallow prefab must remain registerable");
        const auto spawn = [&](int form, std::size_t count, const kb::scene::ScenePrefabInstantiationSettings& settings) -> std::size_t {
            switch (form) {
            case 0: return scene.Prefabs().Instantiate(prefab, settings).ObjectCount();
            case 1: return 2U * scene.Prefabs().InstantiateMany(prefab, count, settings).size();
            case 2: return scene.Prefabs().InstantiateBatch(prefab, count, settings).entitiesCreated;
            case 3: return scene.Prefabs().Instantiate(registered, settings).ObjectCount();
            case 4: return 2U * scene.Prefabs().InstantiateMany(registered, count, settings).size();
            case 5: return scene.Prefabs().InstantiateBatch(registered, count, settings).entitiesCreated;
            }
            return 0U;
        };
        kb::scene::ScenePrefabInstantiationSettings settings{.parent = instance.ObjectAt(depthLimit - 1U),
            .assignNames = false, .syncWorldHierarchy = mode.sync};
        std::cout << "prefab_hierarchy_depth supported=" << depthLimit << " mirrored=" << mode.mirrored << " sync=" << mode.sync << std::endl;
        kb::tests::Require(scene.History().Record(), "Prefab depth history fixture must open a command");
        const auto count = scene.Entities().Count();
        const auto roots = scene.Hierarchy().RootEntities();
        const auto handles = kb::scene::SceneAccess::State(scene).prefabInstances.Handles();
        const auto bytes = scene.History().RecordedBytes();
        const auto undoCount = scene.History().UndoCount();
        for (int form = 0; form < 6; ++form) {
            kb::tests::Require(spawn(form, 2U, settings) == 0U && scene.Entities().Count() == count &&
                scene.Hierarchy().RootEntities() == roots && scene.Hierarchy().ChildCount(settings.parent.Entity()) == 0U &&
                kb::scene::SceneAccess::State(scene).prefabInstances.Handles() == handles,
                "Combined prefab and placement depth must be refused before creating entities or instance records");
        }
        for (const int form : {1, 2, 4, 5}) kb::tests::Require(spawn(form, 0U, settings) == 0U,
            "Zero-count prefab creation must remain a no-op even at an excessive placement depth");
        kb::tests::Require(scene.History().RecordedBytes() == bytes && scene.History().UndoCount() == undoCount,
            "Refused prefab placement must not record history");
        scene.History().Commit();
        settings.parent = instance.ObjectAt(depthLimit - 2U);
        for (int form = 0; form < 6; ++form) {
            const auto before = scene.Entities().Count();
            const auto expected = form % 3 == 0 ? 2U : 4U;
            kb::tests::Require(spawn(form, 2U, settings) == expected && scene.Entities().Count() == before + expected,
                "Every prefab creation form must support subtrees ending exactly at the hierarchy limit");
        }
        scene.Entities().Destroy(instance.RootObject());
        kb::tests::Require(scene.Entities().Count() == 0U, "A supported combined prefab hierarchy must remain removable");
    }
    static_cast<void>(base.AddNode({.parentNode = previous}));
    static_cast<void>(base.AddNode({.parentNode = depthLimit}));
    kb::scene::Scene scene;
    kb::tests::Require(!scene.Prefabs().Register("ExcessiveDepth", base).IsValid(),
        "A prefab exceeding the standalone hierarchy depth limit must be refused during registration");
}

} // namespace kb::tests
