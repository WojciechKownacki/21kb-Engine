#include "ScenePrefabTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/System.hpp"
#include "engine/ecs/World.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneHistory.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabPrivateScene.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

void TracePrefabTest(const char* name) {
#if defined(_MSC_VER)
    std::size_t length = 0;
    getenv_s(&length, nullptr, 0, "KB_TEST_TRACE");
    if (length > 0U) {
#else
    if (std::getenv("KB_TEST_TRACE") != nullptr) {
#endif
        std::cerr << name << '\n';
    }
}

class PrivateSceneRejectedSystem final : public kb::scene::SceneSystem {};

class PrivateSceneRejectedEcsSystem final : public kb::ecs::System {
public:
    [[nodiscard]] kb::ecs::SystemAccess DeclareAccess(kb::ecs::World& world) const override {
        static_cast<void>(world);
        return {};
    }
};

[[nodiscard]] bool ThrowsPrivateSceneRuntimeRegistration(kb::scene::Scene& scene) {
    try {
        scene.Runtime().AddSceneSystem(std::make_unique<PrivateSceneRejectedSystem>());
    } catch (const std::logic_error&) {
        return true;
    }
    return false;
}

[[nodiscard]] bool ThrowsPrivateSceneEcsRuntimeRegistration(kb::scene::Scene& scene) {
    try {
        scene.Runtime().AddSystem(std::make_unique<PrivateSceneRejectedEcsSystem>());
    } catch (const std::logic_error&) {
        return true;
    }
    return false;
}

[[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    kb::tests::Require(input.good(), "Text file could not be opened for reading");
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    kb::tests::Require(output.good(), "Text file could not be opened for writing");
    output << text;
    kb::tests::Require(output.good(), "Text file write failed");
}

[[nodiscard]] std::string RemoveNodeStableIdFields(const std::string& assetText) {
    std::string migrated;
    std::string line;
    bool insideNode = false;
    bool removedAnyStableId = false;
    for (const char ch : assetText) {
        if (ch != '\n') {
            line.push_back(ch);
            continue;
        }

        if (line == "node") {
            insideNode = true;
        }
        if (insideNode && line.rfind("id=", 0U) == 0U) {
            removedAnyStableId = true;
        } else {
            migrated += line;
            migrated.push_back('\n');
        }
        if (line == "endnode") {
            insideNode = false;
        }
        line.clear();
    }

    if (!line.empty()) {
        if (line == "node") {
            insideNode = true;
        }
        if (insideNode && line.rfind("id=", 0U) == 0U) {
            removedAnyStableId = true;
        } else {
            migrated += line;
        }
    }

    kb::tests::Require(removedAnyStableId, "Prefab asset fixture did not contain a node stable id to remove");
    return migrated;
}

void RunPrefabInstantiationTest() {
    kb::scene::Scene scene;
    kb::tests::Require(scene.Prefabs().RegisteredCount() == 0, "Loose prefab instantiation scene should start without registered prefabs");

    kb::scene::SceneObject externalParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "External Parent",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 5.0F, 0.0F, 0.0F },
        },
    });

    kb::scene::ScenePrefab prefab;
    prefab.Reserve(3);
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Root",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
        },
        .components = kb::scene::ScenePrefabNodeComponents{
            .meshRenderer = kb::scene::MeshRendererComponent{
                .meshAssetId = 7,
                .materialAssetId = 11,
            },
        },
    });
    const std::uint32_t cameraNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Camera",
        .parentNode = rootNode,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 2.0F, -4.0F },
        },
        .components = kb::scene::ScenePrefabNodeComponents{
            .camera = kb::scene::CameraComponent{
                .primary = true,
            },
        },
    });
    const std::uint32_t lightNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Light",
        .parentNode = rootNode,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 3.0F, 0.0F },
        },
        .components = kb::scene::ScenePrefabNodeComponents{
            .light = kb::scene::LightComponent{
                .kind = kb::scene::LightKind::Directional,
                .intensity = 3.0F,
            },
        },
    });

    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(
        prefab,
        kb::scene::ScenePrefabInstantiationSettings{
            .parent = externalParent,
            .namePrefix = "Prefab/",
        });

    kb::tests::Require(instance.ObjectCount() == 3, "Prefab did not instantiate all nodes");
    kb::tests::Require(!instance.Handle().IsValid(), "Loose prefab instance should not create an engine-tracked prefab instance handle");
    kb::tests::Require(instance.RootObject().IsValid(), "Prefab root object is invalid");
    kb::tests::Require(scene.Entities().Name(instance.ObjectAt(rootNode)) == "Prefab/Root", "Prefab root name was not assigned");
    kb::tests::Require(scene.Hierarchy().Parent(instance.ObjectAt(rootNode).Entity()) == externalParent.Entity(), "Prefab root was not attached to external parent");
    kb::tests::Require(scene.Hierarchy().Parent(instance.ObjectAt(cameraNode).Entity()) == instance.ObjectAt(rootNode).Entity(), "Prefab camera parent was not assigned");
    kb::tests::Require(scene.Hierarchy().Parent(instance.ObjectAt(lightNode).Entity()) == instance.ObjectAt(rootNode).Entity(), "Prefab light parent was not assigned");
    kb::tests::Require(scene.Hierarchy().ChildEntities(externalParent.Entity()).size() == 1, "External parent does not expose prefab root child");
    kb::tests::Require(scene.Hierarchy().ChildEntities(instance.ObjectAt(rootNode).Entity()).size() == 2, "Prefab root does not expose its prefab children");
    kb::tests::Require(scene.Prefabs().RegisteredCount() == 0, "Instantiating a loose prefab value should not register it in engine storage");

    kb::tests::Require(scene.Components().MeshRenderers().Has(instance.ObjectAt(rootNode).Entity()), "Prefab mesh renderer component was not assigned");
    kb::tests::Require(scene.Components().Cameras().Has(instance.ObjectAt(cameraNode).Entity()), "Prefab camera component was not assigned");
    kb::tests::Require(scene.Components().Lights().Has(instance.ObjectAt(lightNode).Entity()), "Prefab light component was not assigned");

    [[maybe_unused]] const bool progressed = scene.Runtime().Update(0.016F);
    const kb::scene::TransformComponent cameraTransform = scene.Transforms().Get(instance.ObjectAt(cameraNode));
    kb::tests::Require(kb::tests::NearlyEqual(cameraTransform.worldPosition.x, 6.0F), "Prefab world transform did not include external and prefab parent X");
    kb::tests::Require(kb::tests::NearlyEqual(cameraTransform.worldPosition.y, 2.0F), "Prefab world transform did not include prefab parent Y");
    kb::tests::Require(kb::tests::NearlyEqual(cameraTransform.worldPosition.z, -4.0F), "Prefab world transform did not include prefab parent Z");
}

void RunInvalidPrefabInstantiationTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab invalidPrefab;
    [[maybe_unused]] const std::uint32_t invalidNode = invalidPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Invalid",
        .parentNode = 1,
    });

    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(invalidPrefab);
    kb::tests::Require(instance.Empty(), "Invalid prefab should not instantiate");
    kb::tests::Require(scene.Entities().Count() == 0, "Invalid prefab should not create entities");
}

void RunRegisteredPrefabInstantiationTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Registered Root",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F },
        },
    });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Registered Child",
        .parentNode = rootNode,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 3.0F, 0.0F },
        },
    });
    const std::uint64_t rootNodeId = prefab.TryGetNode(rootNode)->stableId;
    const std::uint64_t childNodeId = prefab.TryGetNode(childNode)->stableId;

    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().Register("RegisteredPrefab", std::move(prefab));
    kb::tests::Require(handle.IsValid(), "Registered prefab did not return a valid engine handle");
    kb::tests::Require(scene.Prefabs().Contains(handle), "Registered prefab handle was not stored by the engine");
    kb::tests::Require(scene.Prefabs().RegisteredCount() == 1, "Registered prefab count was not updated");

    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(handle);
    kb::tests::Require(instance.ObjectCount() == 2, "Registered prefab did not instantiate all nodes");
    kb::tests::Require(instance.Handle().IsValid(), "Registered prefab instance did not return an engine instance handle");
    kb::tests::Require(scene.Prefabs().IsInstance(instance.Handle()), "Registered prefab instance handle was not tracked by the engine");
    kb::tests::Require(scene.Prefabs().RootInstance(instance.ObjectAt(rootNode)) == instance.Handle(), "Registered prefab root should own the prefab instance handle");
    kb::tests::Require(!scene.Prefabs().RootInstance(instance.ObjectAt(childNode)).IsValid(), "Registered prefab child must not own a separate prefab instance handle");
    std::uint32_t rootContainingNode = 99;
    std::uint32_t childContainingNode = 99;
    std::uint64_t rootContainingNodeId = 0;
    std::uint64_t childContainingNodeId = 0;
    kb::tests::Require(scene.Prefabs().ContainingInstance(instance.ObjectAt(rootNode), rootContainingNode) == instance.Handle(), "Registered prefab root should map to the tracked prefab instance");
    kb::tests::Require(scene.Prefabs().ContainingInstance(instance.ObjectAt(childNode), childContainingNode) == instance.Handle(), "Registered prefab child should remain tracked inside the parent prefab instance");
    kb::tests::Require(scene.Prefabs().ContainingInstance(instance.ObjectAt(rootNode), rootContainingNode, rootContainingNodeId) == instance.Handle(), "Registered prefab root should expose its stable node mapping");
    kb::tests::Require(scene.Prefabs().ContainingInstance(instance.ObjectAt(childNode), childContainingNode, childContainingNodeId) == instance.Handle(), "Registered prefab child should expose its stable node mapping");
    kb::tests::Require(rootContainingNode == rootNode, "Registered prefab root should map to the root prefab node");
    kb::tests::Require(childContainingNode == childNode, "Registered prefab child should map to the child prefab node");
    kb::tests::Require(rootContainingNodeId == rootNodeId, "Registered prefab root stable node mapping is wrong");
    kb::tests::Require(childContainingNodeId == childNodeId, "Registered prefab child stable node mapping is wrong");
    kb::tests::Require(scene.Prefabs().Overrides(instance.Handle()).Empty(), "Fresh registered prefab instance should not report overrides");
    kb::tests::Require(scene.Entities().Name(instance.ObjectAt(rootNode)) == "Registered Root", "Registered prefab root name was not assigned");
    kb::tests::Require(scene.Hierarchy().Parent(instance.ObjectAt(childNode).Entity()) == instance.ObjectAt(rootNode).Entity(), "Registered prefab child parent was not assigned");
    kb::tests::Require(scene.Hierarchy().ChildEntities(instance.ObjectAt(rootNode).Entity()).size() == 1, "Registered prefab root does not expose its child through engine hierarchy");
    kb::tests::Require(scene.Prefabs().RegisteredCount() == 1, "Instantiating a registered prefab should not duplicate registry entries");

    [[maybe_unused]] const bool progressed = scene.Runtime().Update(0.016F);
    const kb::scene::TransformComponent childTransform = scene.Transforms().Get(instance.ObjectAt(childNode));
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.x, 2.0F), "Registered prefab child world X was not rebuilt");
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.y, 3.0F), "Registered prefab child world Y was not rebuilt");
}

void RunRegisteredPrefabBatchInstantiationTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Registered Batch Root" });
    static_cast<void>(prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Registered Batch Child",
        .parentNode = rootNode,
    }));

    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().Register("RegisteredBatchPrefab", std::move(prefab));
    kb::tests::Require(handle.IsValid(), "Registered prefab batch setup failed to register a prefab");

    kb::scene::ScenePrefabInstantiationSettings settings;
    settings.assignNames = false;
    const kb::scene::ScenePrefabInstantiationStats stats = scene.Prefabs().InstantiateBatch(handle, 4, settings);

    kb::tests::Require(stats.requestedInstances == 4, "Registered prefab batch recorded invalid requested count");
    kb::tests::Require(stats.instantiatedInstances == 4, "Registered prefab batch recorded invalid instantiated count");
    kb::tests::Require(stats.entitiesCreated == 8, "Registered prefab batch created an unexpected entity count");
    kb::tests::Require(stats.prefabArchetypesTouched == 1, "Registered prefab batch did not use the baked archetype cache");
    kb::tests::Require(stats.bulkCreateCommands == 1, "Registered prefab batch did not create through the baked bulk path");
    kb::tests::Require(stats.componentSetCommands == 0, "Registered prefab batch used per-component set commands");
    kb::tests::Require(stats.componentSourceBytesRead > 0, "Registered prefab batch did not report source component bytes");
    kb::tests::Require(stats.componentSourceBytesRead < stats.componentBytesCopied, "Registered prefab batch did not use pattern source reads");
    kb::tests::Require(stats.registeredInstanceCount == 0, "Registered prefab batch should not track instance records");
    kb::tests::Require(stats.entityCreateNanoseconds > 0, "Registered prefab batch did not report entity create time");
    kb::tests::Require(stats.prefabBakeNanoseconds == 0, "Registered prefab batch rebaked an already cached prefab");
    kb::tests::Require(stats.componentPayloadBuildNanoseconds > 0, "Registered prefab batch did not report component payload build time");
    kb::tests::Require(stats.entityBulkCreateNanoseconds > 0, "Registered prefab batch did not report bulk create time");
    kb::tests::Require(stats.hasContiguousGeneratedEntityRuns, "Registered prefab batch did not report contiguous entity runs");
    kb::tests::Require(stats.entityPrefabOrderMapNanoseconds > 0, "Registered prefab batch did not report prefab-order map time");
    kb::tests::Require(stats.instanceObjectSlabNanoseconds == 0, "Registered prefab batch stats-only path built instance object slabs");
    kb::tests::Require(stats.hierarchyRecordNanoseconds > 0, "Registered prefab batch did not report hierarchy record time");
    kb::tests::Require(stats.nameAssignmentNanoseconds == 0, "Registered prefab batch reported disabled name assignment time");
    kb::tests::Require(stats.registryResolveNanoseconds > 0, "Registered prefab batch did not report registry resolve time");
    kb::tests::Require(stats.historyRecordNanoseconds == 0, "Registered prefab batch should not report instance record time");
    kb::tests::Require(scene.Entities().Count() == 8, "Registered prefab batch did not create scene entities");
    kb::tests::Require(scene.Hierarchy().RootEntities().size() == 4, "Registered prefab batch did not expose scene hierarchy roots");
}

void RunBulkPrefabInstantiationTest() {
    kb::scene::Scene scene;
    kb::scene::SceneObject externalParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Bulk Parent",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 10.0F, 0.0F, 0.0F },
        },
    });

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Bulk Root",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
        },
        .components = kb::scene::ScenePrefabNodeComponents{
            .meshRenderer = kb::scene::MeshRendererComponent{
                .meshAssetId = 42,
                .materialAssetId = 84,
            },
            .tags = [] {
                kb::scene::TagsComponent tags;
                kb::scene::SetTagsText(tags, "bulk");
                return tags;
            }(),
        },
    });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Bulk Child",
        .parentNode = rootNode,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 2.0F, 0.0F },
        },
        .components = kb::scene::ScenePrefabNodeComponents{
            .camera = kb::scene::CameraComponent{
                .primary = true,
            },
            .light = kb::scene::LightComponent{
                .kind = kb::scene::LightKind::Point,
                .intensity = 5.0F,
            },
        },
    });

    const std::vector<kb::scene::ScenePrefabInstance> instances = scene.Prefabs().InstantiateMany(
        prefab,
        3,
        kb::scene::ScenePrefabInstantiationSettings{
            .parent = externalParent,
            .namePrefix = "Bulk/",
            .syncWorldHierarchy = true,
        });

    kb::tests::Require(instances.size() == 3, "Bulk prefab instantiation did not return every requested instance");
    kb::tests::Require(scene.Entities().Count() == 7, "Bulk prefab instantiation created an unexpected entity count");
    const kb::scene::ScenePrefabInstantiationStats spawnStats = scene.Prefabs().LastInstantiationStats();
    kb::tests::Require(spawnStats.requestedInstances == 3, "Bulk prefab stats recorded invalid requested instance count");
    kb::tests::Require(spawnStats.instantiatedInstances == 3, "Bulk prefab stats recorded invalid instantiated instance count");
    kb::tests::Require(spawnStats.nodesPerInstance == 2, "Bulk prefab stats recorded invalid node count");
    kb::tests::Require(spawnStats.entitiesCreated == 6, "Bulk prefab stats recorded invalid entity create count");
    kb::tests::Require(spawnStats.prefabArchetypesTouched == 2, "Bulk prefab stats recorded invalid archetype count");
    kb::tests::Require(spawnStats.bulkCreateCommands == 2, "Bulk prefab stats recorded invalid batch create command count");
    kb::tests::Require(spawnStats.parentCommands == 6, "Bulk prefab stats recorded invalid parent command count");
    kb::tests::Require(spawnStats.componentBytesCopied > 0, "Bulk prefab stats did not report copied component bytes");
    kb::tests::Require(spawnStats.componentSourceBytesRead > 0, "Backend-synced bulk prefab stats did not report source component bytes");
    kb::tests::Require(spawnStats.componentSourceBytesRead < spawnStats.componentBytesCopied, "Backend-synced bulk prefab pattern telemetry did not reduce source component bytes");
    kb::tests::Require(spawnStats.componentCopyBytesPerSecond > 0.0, "Bulk prefab stats did not report component copy throughput");
    kb::tests::Require(spawnStats.componentSourceBytesPerSecond > 0.0, "Bulk prefab stats did not report component source throughput");
    kb::tests::Require(spawnStats.entityCreateEntitiesPerSecond > 0.0, "Bulk prefab stats did not report entity create throughput");
    kb::tests::Require(spawnStats.registeredInstanceCount == 0, "Loose bulk prefab stats reported registered instances");
    kb::tests::Require(spawnStats.entityCreateNanoseconds > 0, "Loose bulk prefab stats did not report entity create time");
    kb::tests::Require(spawnStats.prefabBakeNanoseconds > 0, "Loose bulk prefab stats did not report prefab bake time");
    kb::tests::Require(spawnStats.componentPayloadBuildNanoseconds > 0, "Loose bulk prefab stats did not report component payload build time");
    kb::tests::Require(spawnStats.entityBulkCreateNanoseconds > 0, "Loose bulk prefab stats did not report bulk create time");
    kb::tests::Require(spawnStats.entityPrefabOrderMapNanoseconds > 0, "Loose bulk prefab stats did not report prefab-order map time");
    kb::tests::Require(spawnStats.instanceObjectSlabNanoseconds > 0, "Loose bulk prefab stats did not report instance object slab time");
    kb::tests::Require(spawnStats.commandBuildNanoseconds > 0, "Loose bulk prefab stats did not report command build time");
    kb::tests::Require(spawnStats.commandPlaybackNanoseconds > 0, "Loose bulk prefab stats did not report command playback time");
    kb::tests::Require(spawnStats.commandPlaybackCreateNanoseconds > 0, "Loose bulk prefab stats did not report command playback create time");
    kb::tests::Require(spawnStats.commandPlaybackApplyNanoseconds > 0, "Loose bulk prefab stats did not report command playback apply time");
    kb::tests::Require(spawnStats.commandPlaybackParentNanoseconds > 0, "Loose bulk prefab stats did not report command playback parent time");
    kb::tests::Require(spawnStats.hierarchyRecordNanoseconds > 0, "Loose bulk prefab stats did not report hierarchy record time");
    kb::tests::Require(spawnStats.nameAssignmentNanoseconds > 0, "Loose named bulk prefab stats did not report name assignment time");
    kb::tests::Require(spawnStats.registryResolveNanoseconds == 0, "Loose bulk prefab stats reported registry resolve time");
    kb::tests::Require(spawnStats.historyRecordNanoseconds == 0, "Loose bulk prefab stats reported instance record time");
    const kb::ecs::World& world = scene.Runtime().EcsWorld();
    kb::tests::Require(world.NativeStorageStats().liveEntities == scene.Entities().Count(), "Bulk prefab spawn created objects outside the native ECS entity model");
    const std::vector<kb::ecs::ComponentId> rootArchetype{
        world.Component<kb::scene::TransformComponent>(),
        world.Component<kb::scene::VisibilityComponent>(),
        world.Component<kb::scene::MeshRendererComponent>(),
        world.Component<kb::scene::TagsComponent>(),
    };
    const std::vector<kb::ecs::ComponentId> childArchetype{
        world.Component<kb::scene::TransformComponent>(),
        world.Component<kb::scene::VisibilityComponent>(),
        world.Component<kb::scene::CameraComponent>(),
        world.Component<kb::scene::LightComponent>(),
    };
    for (const kb::scene::ScenePrefabInstance& instance : instances) {
        kb::tests::Require(instance.ObjectCount() == 2, "Bulk prefab instance did not contain every node");
        kb::tests::Require(!instance.Handle().IsValid(), "Loose bulk prefab instance should not be tracked");
        kb::tests::Require(scene.Entities().IsAlive(instance.ObjectAt(rootNode)) && scene.Entities().IsAlive(instance.ObjectAt(childNode)), "Bulk prefab returned a non-live ECS entity object");
        kb::tests::Require(scene.Entities().Name(instance.ObjectAt(rootNode)) == "Bulk/Bulk Root", "Bulk prefab root name was not assigned");
        kb::tests::Require(world.Name(instance.ObjectAt(rootNode).Entity()).empty(), "Bulk prefab fast name path synchronized a repeated name to the backend");
        kb::tests::Require(scene.Hierarchy().Parent(instance.ObjectAt(rootNode).Entity()) == externalParent.Entity(), "Bulk prefab root parent was not assigned");
        kb::tests::Require(scene.Hierarchy().Parent(instance.ObjectAt(childNode).Entity()) == instance.ObjectAt(rootNode).Entity(), "Bulk prefab child parent was not assigned");
        kb::tests::Require(scene.Components().MeshRenderers().Has(instance.ObjectAt(rootNode).Entity()), "Bulk prefab mesh component was not assigned");
        kb::tests::Require(scene.Components().Tags().Has(instance.ObjectAt(rootNode).Entity()), "Bulk prefab tags component was not assigned");
        kb::tests::Require(scene.Components().Cameras().Has(instance.ObjectAt(childNode).Entity()), "Bulk prefab camera component was not assigned");
        kb::tests::Require(scene.Components().Lights().Has(instance.ObjectAt(childNode).Entity()), "Bulk prefab light component was not assigned");
        kb::tests::Require(world.NativeStorage().EntityArchetypeMatches(instance.ObjectAt(rootNode).Entity(), std::span<const kb::ecs::ComponentId>{ rootArchetype }), "Bulk prefab root was not baked into the expected native archetype");
        kb::tests::Require(world.NativeStorage().EntityArchetypeMatches(instance.ObjectAt(childNode).Entity(), std::span<const kb::ecs::ComponentId>{ childArchetype }), "Bulk prefab child was not baked into the expected native archetype");
    }

    [[maybe_unused]] const bool progressed = scene.Runtime().Update(0.016F);
    const kb::scene::TransformComponent childTransform = scene.Transforms().Get(instances.front().ObjectAt(childNode));
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.x, 11.0F), "Bulk prefab world transform did not include external parent X");
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.y, 2.0F), "Bulk prefab world transform did not include prefab parent Y");
}

void RunBulkPrefabSceneHierarchyOnlyInstantiationTest() {
    kb::scene::Scene scene;
    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Scene Hierarchy Root" });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Scene Hierarchy Child",
        .parentNode = rootNode,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 2.0F, 0.0F },
        },
        .visibility = kb::scene::VisibilityComponent{
            .visible = true,
        },
    });

    kb::scene::ScenePrefabInstantiationSettings settings;
    settings.assignNames = true;
    const std::vector<kb::scene::ScenePrefabInstance> instances = scene.Prefabs().InstantiateMany(prefab, 4, settings);

    kb::tests::Require(instances.size() == 4, "Scene-hierarchy-only prefab spawn did not return every requested instance");
    const kb::scene::ScenePrefabInstantiationStats spawnStats = scene.Prefabs().LastInstantiationStats();
    kb::tests::Require(spawnStats.parentCommands == 0, "Scene-hierarchy-only prefab spawn queued backend parent commands");
    kb::tests::Require(spawnStats.entitiesCreated == 8, "Scene-hierarchy-only prefab spawn created an unexpected entity count");
    kb::tests::Require(spawnStats.componentBytesCopied > 0, "Scene-hierarchy-only prefab spawn did not report logical component bytes");
    kb::tests::Require(spawnStats.componentSourceBytesRead > 0, "Scene-hierarchy-only prefab spawn did not report source component bytes");
    kb::tests::Require(spawnStats.componentSourceBytesRead < spawnStats.componentBytesCopied, "Scene-hierarchy-only prefab pattern telemetry did not reduce source component bytes");
    kb::tests::Require(spawnStats.componentCopyBytesPerSecond > 0.0, "Scene-hierarchy-only prefab spawn did not report component copy throughput");
    kb::tests::Require(spawnStats.componentSourceBytesPerSecond > 0.0, "Scene-hierarchy-only prefab spawn did not report component source throughput");
    kb::tests::Require(spawnStats.entityCreateEntitiesPerSecond > 0.0, "Scene-hierarchy-only prefab spawn did not report entity create throughput");
    kb::tests::Require(spawnStats.entityCreateNanoseconds > 0, "Scene-hierarchy-only prefab spawn did not report entity create time");
    kb::tests::Require(spawnStats.commandBuildNanoseconds == 0, "Scene-hierarchy-only prefab spawn reported command build time");
    kb::tests::Require(spawnStats.commandPlaybackNanoseconds == 0, "Scene-hierarchy-only prefab spawn reported command playback time");
    kb::tests::Require(spawnStats.commandPlaybackParentNanoseconds == 0, "Scene-hierarchy-only prefab spawn reported command parent playback time");
    kb::tests::Require(spawnStats.hierarchyRecordNanoseconds > 0, "Scene-hierarchy-only prefab spawn did not report hierarchy record time");
    kb::tests::Require(spawnStats.nameAssignmentNanoseconds > 0, "Scene-hierarchy-only named prefab spawn did not report name assignment time");

    const kb::ecs::World& world = scene.Runtime().EcsWorld();
    for (const kb::scene::ScenePrefabInstance& instance : instances) {
        const kb::scene::SceneObject root = instance.ObjectAt(rootNode);
        const kb::scene::SceneObject child = instance.ObjectAt(childNode);
        kb::tests::Require(scene.Hierarchy().Parent(child.Entity()) == root.Entity(), "Scene hierarchy cache did not expose the prefab child parent");
        kb::tests::Require(scene.Hierarchy().ChildEntities(root.Entity()).size() == 1U, "Scene hierarchy cache did not expose the prefab child");
        kb::tests::Require(!world.Parent(child.Entity()).IsValid(), "Scene-hierarchy-only prefab spawn synchronized a backend parent relation");
        kb::tests::Require(scene.Entities().Name(root) == "Scene Hierarchy Root", "Scene-hierarchy-only prefab spawn did not expose cached root name");
        kb::tests::Require(scene.Entities().Name(child) == "Scene Hierarchy Child", "Scene-hierarchy-only prefab spawn did not expose cached child name");
        kb::tests::Require(world.Name(root.Entity()).empty(), "Scene-hierarchy-only prefab spawn synchronized a backend root name");
        kb::tests::Require(world.Name(child.Entity()).empty(), "Scene-hierarchy-only prefab spawn synchronized a backend child name");
    }

    const kb::scene::SceneObject mutableChild = instances.front().ObjectAt(childNode);
    kb::scene::TransformComponent transform = scene.Transforms().Get(mutableChild);
    transform.localPosition.y = 7.0F;
    scene.Transforms().Set(mutableChild, transform);
    scene.Entities().SetName(mutableChild, "Scene Hierarchy Child Edited");
    scene.Components().Visibility().Set(mutableChild.Entity(), kb::scene::VisibilityComponent{ .visible = false });

    kb::tests::Require(scene.Entities().Name(mutableChild) == "Scene Hierarchy Child Edited", "Scene-hierarchy-only prefab edit did not update cached child name");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(mutableChild).localPosition.y, 7.0F), "Scene-hierarchy-only prefab edit did not update native child transform");
    const kb::scene::VisibilityComponent* editedVisibility = scene.Components().Visibility().TryGet(mutableChild.Entity());
    kb::tests::Require(editedVisibility != nullptr && !editedVisibility->visible, "Scene-hierarchy-only prefab edit did not update native visibility");
    kb::tests::Require(world.Name(mutableChild.Entity()).empty(), "Scene-hierarchy-only prefab edit synchronized a backend child name");
}

void RunBulkPrefabBatchStatsOnlyInstantiationTest() {
    kb::scene::Scene scene;
    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Batch Stats Root" });
    static_cast<void>(prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Batch Stats Child",
        .parentNode = rootNode,
    }));

    kb::scene::ScenePrefabInstantiationSettings settings;
    settings.assignNames = false;
    const kb::scene::ScenePrefabInstantiationStats stats = scene.Prefabs().InstantiateBatch(prefab, 5, settings);

    kb::tests::Require(stats.requestedInstances == 5, "Batch stats-only prefab spawn recorded invalid requested count");
    kb::tests::Require(stats.instantiatedInstances == 5, "Batch stats-only prefab spawn recorded invalid instantiated count");
    kb::tests::Require(stats.entitiesCreated == 10, "Batch stats-only prefab spawn recorded invalid entity count");
    kb::tests::Require(stats.parentCommands == 0, "Batch stats-only prefab spawn queued backend hierarchy commands");
    kb::tests::Require(stats.entityCreateNanoseconds > 0, "Batch stats-only prefab spawn did not report entity create time");
    kb::tests::Require(stats.prefabBakeNanoseconds > 0, "Batch stats-only prefab spawn did not report prefab bake time");
    kb::tests::Require(stats.componentPayloadBuildNanoseconds > 0, "Batch stats-only prefab spawn did not report component payload build time");
    kb::tests::Require(stats.entityBulkCreateNanoseconds > 0, "Batch stats-only prefab spawn did not report bulk create time");
    kb::tests::Require(stats.hasContiguousGeneratedEntityRuns, "Batch stats-only prefab spawn did not report contiguous entity runs");
    kb::tests::Require(stats.entityPrefabOrderMapNanoseconds > 0, "Batch stats-only prefab spawn did not report prefab-order map time");
    kb::tests::Require(stats.instanceObjectSlabNanoseconds == 0, "Batch stats-only prefab spawn built instance object slabs");
    kb::tests::Require(stats.componentCopyBytesPerSecond > 0.0, "Batch stats-only prefab spawn did not report component copy throughput");
    kb::tests::Require(stats.componentSourceBytesPerSecond > 0.0, "Batch stats-only prefab spawn did not report component source throughput");
    kb::tests::Require(stats.entityCreateEntitiesPerSecond > 0.0, "Batch stats-only prefab spawn did not report entity create throughput");
    kb::tests::Require(stats.hierarchyRecordNanoseconds > 0, "Batch stats-only prefab spawn did not report hierarchy record time");
    kb::tests::Require(stats.nameAssignmentNanoseconds == 0, "Batch stats-only prefab spawn reported disabled name assignment time");
    kb::tests::Require(scene.Prefabs().LastInstantiationStats().entitiesCreated == stats.entitiesCreated, "Batch stats-only prefab spawn did not update last stats");
    kb::tests::Require(scene.Entities().Count() == 10, "Batch stats-only prefab spawn created an unexpected scene entity count");

    const std::vector<kb::scene::SceneEntity> roots = scene.Hierarchy().RootEntities();
    kb::tests::Require(roots.size() == 5, "Batch stats-only prefab spawn did not expose scene hierarchy roots");
    std::vector<kb::scene::SceneEntity> firstRootChildren;
    for (const kb::scene::SceneEntity root : roots) {
        const std::vector<kb::scene::SceneEntity> children = scene.Hierarchy().ChildEntities(root);
        kb::tests::Require(children.size() == 1U, "Batch stats-only prefab spawn did not expose scene hierarchy children");
        kb::tests::Require(scene.Hierarchy().Parent(children.front()) == root, "Batch stats-only prefab spawn did not expose child parent");
        if (firstRootChildren.empty()) {
            firstRootChildren = children;
        }
    }
    kb::tests::Require(roots.size() >= 2U && !firstRootChildren.empty(), "Batch stats-only prefab spawn reparent setup failed");
    kb::tests::Require(scene.Hierarchy().SetParent(firstRootChildren.front(), roots[1]), "Batch stats-only prefab spawn dense hierarchy reparent failed");
    kb::tests::Require(scene.Hierarchy().Parent(firstRootChildren.front()) == roots[1], "Batch stats-only prefab spawn dense hierarchy parent cache did not update");
    kb::tests::Require(scene.Hierarchy().ChildEntities(roots[0]).empty(), "Batch stats-only prefab spawn dense hierarchy did not remove old child link");
    kb::tests::Require(scene.Hierarchy().ChildEntities(roots[1]).size() == 2U, "Batch stats-only prefab spawn dense hierarchy did not append new child link");
}

void RunBulkPrefabMultiArchetypeNodeOrderTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Multi Archetype Root",
        .components = kb::scene::ScenePrefabNodeComponents{
            .tags = [] {
                kb::scene::TagsComponent tags;
                kb::scene::SetTagsText(tags, "root");
                return tags;
            }(),
        },
    });
    const std::uint32_t cameraNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Multi Archetype Camera",
        .parentNode = rootNode,
        .components = kb::scene::ScenePrefabNodeComponents{
            .camera = kb::scene::CameraComponent{ .primary = true },
        },
    });
    const std::uint32_t meshNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Multi Archetype Mesh",
        .parentNode = rootNode,
        .components = kb::scene::ScenePrefabNodeComponents{
            .meshRenderer = kb::scene::MeshRendererComponent{ .meshAssetId = 31, .materialAssetId = 62 },
        },
    });
    const std::uint32_t lightNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Multi Archetype Light",
        .parentNode = meshNode,
        .components = kb::scene::ScenePrefabNodeComponents{
            .light = kb::scene::LightComponent{ .kind = kb::scene::LightKind::Point, .intensity = 2.5F },
        },
    });

    const std::vector<kb::scene::ScenePrefabInstance> instances = scene.Prefabs().InstantiateMany(prefab, 2);
    kb::tests::Require(instances.size() == 2U, "Multi-archetype bulk prefab did not create all instances");
    for (const kb::scene::ScenePrefabInstance& instance : instances) {
        kb::tests::Require(instance.ObjectCount() == 4U, "Multi-archetype bulk prefab instance lost nodes");
        kb::tests::Require(scene.Entities().Name(instance.ObjectAt(rootNode)) == "Multi Archetype Root", "Multi-archetype bulk prefab root mapping is wrong");
        kb::tests::Require(scene.Entities().Name(instance.ObjectAt(cameraNode)) == "Multi Archetype Camera", "Multi-archetype bulk prefab camera mapping is wrong");
        kb::tests::Require(scene.Entities().Name(instance.ObjectAt(meshNode)) == "Multi Archetype Mesh", "Multi-archetype bulk prefab mesh mapping is wrong");
        kb::tests::Require(scene.Entities().Name(instance.ObjectAt(lightNode)) == "Multi Archetype Light", "Multi-archetype bulk prefab light mapping is wrong");
        kb::tests::Require(scene.Components().Tags().Has(instance.ObjectAt(rootNode).Entity()), "Multi-archetype bulk prefab root components are wrong");
        kb::tests::Require(scene.Components().Cameras().Has(instance.ObjectAt(cameraNode).Entity()), "Multi-archetype bulk prefab camera components are wrong");
        kb::tests::Require(scene.Components().MeshRenderers().Has(instance.ObjectAt(meshNode).Entity()), "Multi-archetype bulk prefab mesh components are wrong");
        kb::tests::Require(scene.Components().Lights().Has(instance.ObjectAt(lightNode).Entity()), "Multi-archetype bulk prefab light components are wrong");
        kb::tests::Require(scene.Hierarchy().Parent(instance.ObjectAt(lightNode).Entity()) == instance.ObjectAt(meshNode).Entity(), "Multi-archetype bulk prefab hierarchy mapping is wrong");
    }
}

void RunLargePrefabHierarchyTransformTest() {
    kb::scene::Scene scene;
    constexpr std::uint32_t kDepth = 128U;
    constexpr std::size_t kInstanceCount = 4U;

    kb::scene::ScenePrefab prefab;
    std::uint32_t parentNode = kb::scene::ScenePrefabNodeDesc::NoParent;
    for (std::uint32_t node = 0U; node < kDepth; ++node) {
        parentNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
            .name = "Large Hierarchy Node",
            .parentNode = parentNode,
            .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F } },
        });
    }

    const std::vector<kb::scene::ScenePrefabInstance> instances = scene.Prefabs().InstantiateMany(prefab, kInstanceCount);
    kb::tests::Require(instances.size() == kInstanceCount, "Large prefab hierarchy bulk instantiation lost instances");
    for (const kb::scene::ScenePrefabInstance& instance : instances) {
        kb::tests::Require(instance.ObjectCount() == kDepth, "Large prefab hierarchy instance has an unexpected node count");
        kb::tests::Require(scene.Hierarchy().Parent(instance.ObjectAt(1U).Entity()) == instance.ObjectAt(0U).Entity(), "Large prefab hierarchy first child parent is invalid");
        kb::tests::Require(scene.Hierarchy().Parent(instance.ObjectAt(kDepth - 1U).Entity()) == instance.ObjectAt(kDepth - 2U).Entity(), "Large prefab hierarchy deepest parent is invalid");
    }

    static_cast<void>(scene.Runtime().Update(0.016F));
    const std::span<const kb::scene::SceneEntity> dirtyRenderEntities = scene.Runtime().TransformRenderProxyUpdateEntities();
    kb::tests::Require(dirtyRenderEntities.size() >= kDepth * kInstanceCount, "Large prefab hierarchy did not populate transform render update cache");

    const kb::scene::ScenePrefabInstance& first = instances.front();
    const kb::scene::TransformComponent parentTransform = scene.Transforms().Get(first.ObjectAt(kDepth - 2U));
    const kb::scene::TransformComponent deepestTransform = scene.Transforms().Get(first.ObjectAt(kDepth - 1U));
    kb::tests::Require(kb::tests::NearlyEqual(deepestTransform.worldPosition.x, static_cast<float>(kDepth)), "Large prefab hierarchy did not propagate deepest world transform");
    kb::tests::Require(deepestTransform.localVersion == 1U, "Large prefab hierarchy changed local version during world sync");
    kb::tests::Require(deepestTransform.parentVersion == parentTransform.worldVersion, "Large prefab hierarchy did not record parent world version");
    kb::tests::Require(deepestTransform.worldVersion > 0U, "Large prefab hierarchy did not initialize world version");
}

void RunRegisteredBulkPrefabInstantiationTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Registered Bulk Root" });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Registered Bulk Child",
        .parentNode = rootNode,
        .components = kb::scene::ScenePrefabNodeComponents{
            .rigidbody = kb::scene::RigidbodyComponent{
                .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
                .mass = 3.0F,
            },
            .collider = kb::scene::ColliderComponent{
                .shape = kb::scene::ColliderShape::Sphere,
                .radius = 2.0F,
            },
            .audioSource = kb::scene::AudioSourceComponent{
                .clipAssetId = 9,
                .volume = 0.5F,
            },
            .audioListener = kb::scene::AudioListenerComponent{
                .primary = false,
            },
        },
    });
    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().Register("RegisteredBulkPrefab", std::move(prefab));

    kb::scene::ScenePrefabInstantiationSettings settings;
    settings.syncWorldHierarchy = true;
    const std::vector<kb::scene::ScenePrefabInstance> instances = scene.Prefabs().InstantiateMany(handle, 4, settings);
    kb::tests::Require(instances.size() == 4, "Registered bulk prefab instantiation did not return every requested instance");
    const kb::scene::ScenePrefabInstantiationStats spawnStats = scene.Prefabs().LastInstantiationStats();
    kb::tests::Require(spawnStats.requestedInstances == 4, "Registered bulk prefab stats recorded invalid requested instance count");
    kb::tests::Require(spawnStats.instantiatedInstances == 4, "Registered bulk prefab stats recorded invalid instantiated instance count");
    kb::tests::Require(spawnStats.nodesPerInstance == 2, "Registered bulk prefab stats recorded invalid node count");
    kb::tests::Require(spawnStats.entitiesCreated == 8, "Registered bulk prefab stats recorded invalid entity create count");
    kb::tests::Require(spawnStats.prefabArchetypesTouched == 2, "Registered bulk prefab stats recorded invalid archetype count");
    kb::tests::Require(spawnStats.bulkCreateCommands == 2, "Registered bulk prefab stats recorded invalid batch create command count");
    kb::tests::Require(spawnStats.parentCommands == 4, "Registered bulk prefab stats recorded invalid parent command count");
    kb::tests::Require(spawnStats.componentBytesCopied > 0, "Registered bulk prefab stats did not report copied component bytes");
    kb::tests::Require(spawnStats.registeredInstanceCount == 4, "Registered bulk prefab stats recorded invalid registered instance count");
    kb::tests::Require(spawnStats.entityCreateNanoseconds > 0, "Registered bulk prefab stats did not report entity create time");
    kb::tests::Require(spawnStats.prefabBakeNanoseconds == 0, "Registered bulk prefab spawn rebaked an already cached prefab");
    kb::tests::Require(spawnStats.componentPayloadBuildNanoseconds > 0, "Registered bulk prefab stats did not report component payload build time");
    kb::tests::Require(spawnStats.entityBulkCreateNanoseconds > 0, "Registered bulk prefab stats did not report bulk create time");
    kb::tests::Require(spawnStats.entityPrefabOrderMapNanoseconds > 0, "Registered bulk prefab stats did not report prefab-order map time");
    kb::tests::Require(spawnStats.instanceObjectSlabNanoseconds > 0, "Registered bulk prefab stats did not report instance object slab time");
    kb::tests::Require(spawnStats.commandPlaybackParentNanoseconds > 0, "Registered bulk prefab stats did not report command parent playback time");
    kb::tests::Require(spawnStats.hierarchyRecordNanoseconds > 0, "Registered bulk prefab stats did not report hierarchy record time");
    kb::tests::Require(spawnStats.nameAssignmentNanoseconds > 0, "Registered named bulk prefab stats did not report name assignment time");
    kb::tests::Require(spawnStats.registryResolveNanoseconds > 0, "Registered bulk prefab stats did not report registry resolve time");
    kb::tests::Require(spawnStats.historyRecordNanoseconds > 0, "Registered bulk prefab stats did not report instance record time");
    for (const kb::scene::ScenePrefabInstance& instance : instances) {
        kb::tests::Require(instance.Handle().IsValid(), "Registered bulk prefab instance was not tracked");
        kb::tests::Require(scene.Prefabs().RootInstance(instance.ObjectAt(rootNode)) == instance.Handle(), "Registered bulk prefab root was not tracked");
        std::uint32_t childContainingNode = 99;
        kb::tests::Require(scene.Prefabs().ContainingInstance(instance.ObjectAt(childNode), childContainingNode) == instance.Handle(), "Registered bulk prefab child was not tracked");
        kb::tests::Require(childContainingNode == childNode, "Registered bulk prefab child mapped to the wrong node");
        kb::tests::Require(scene.Components().Rigidbodies().Has(instance.ObjectAt(childNode).Entity()), "Registered bulk prefab rigidbody component was not assigned");
        kb::tests::Require(scene.Components().Colliders().Has(instance.ObjectAt(childNode).Entity()), "Registered bulk prefab collider component was not assigned");
        kb::tests::Require(scene.Components().AudioSources().Has(instance.ObjectAt(childNode).Entity()), "Registered bulk prefab audio source component was not assigned");
        kb::tests::Require(scene.Components().AudioListeners().Has(instance.ObjectAt(childNode).Entity()), "Registered bulk prefab audio listener component was not assigned");
    }
}

void RunRegisteredBulkPrefabInvalidatesQueryCachePerBatchTest() {
    constexpr std::size_t kInstanceCount = 128U;

    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Batch Invalidation Root" });
    [[maybe_unused]] const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Batch Invalidation Child",
        .parentNode = rootNode,
        .components = kb::scene::ScenePrefabNodeComponents{
            .rigidbody = kb::scene::RigidbodyComponent{
                .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
                .mass = 2.0F,
            },
            .collider = kb::scene::ColliderComponent{
                .shape = kb::scene::ColliderShape::Box,
                .boxSize = kb::scene::Vec3{ 1.0F, 1.0F, 1.0F },
            },
        },
    });
    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().Register("RegisteredBatchInvalidationPrefab", std::move(prefab));

    kb::scene::ScenePrefabInstantiationSettings settings;
    settings.syncWorldHierarchy = true;
    kb::ecs::World& world = scene.Runtime().EcsWorld();
    world.ResetTelemetryFrameCounters();

    const std::vector<kb::scene::ScenePrefabInstance> instances = scene.Prefabs().InstantiateMany(handle, kInstanceCount, settings);
    const kb::scene::ScenePrefabInstantiationStats spawnStats = scene.Prefabs().LastInstantiationStats();
    const kb::ecs::WorldTelemetrySnapshot telemetry = world.TelemetrySnapshot();

    kb::tests::Require(instances.size() == kInstanceCount, "Registered prefab cache-invalidation spawn did not return every instance");
    kb::tests::Require(spawnStats.entitiesCreated == kInstanceCount * 2U, "Registered prefab cache-invalidation spawn created an invalid entity count");
    kb::tests::Require(spawnStats.prefabArchetypesTouched == 2U, "Registered prefab cache-invalidation setup did not exercise two archetypes");
    kb::tests::Require(spawnStats.bulkCreateCommands == spawnStats.prefabArchetypesTouched, "Registered prefab cache-invalidation spawn did not use one create command per archetype");
    kb::tests::Require(spawnStats.parentCommands == kInstanceCount, "Registered prefab cache-invalidation spawn did not exercise the bulk parent lane");

    const std::uint64_t expectedMaxInvalidations =
        static_cast<std::uint64_t>(spawnStats.prefabArchetypesTouched + (spawnStats.parentCommands > 0U ? 1U : 0U));
    kb::tests::Require(
        telemetry.archetypeTransitionInvalidationsSinceReset <= expectedMaxInvalidations,
        "Registered prefab spawn invalidated query plans per entity instead of per archetype/parent batch");
    kb::tests::Require(
        telemetry.archetypeTransitionInvalidationsSinceReset < spawnStats.entitiesCreated,
        "Registered prefab spawn query invalidation scaled with created entity count");
    kb::tests::Require(
        telemetry.structuralChangesSinceReset <= expectedMaxInvalidations,
        "Registered prefab spawn recorded structural telemetry per entity instead of per structural batch");
}

void RunRegisteredBulkPrefabInstancesUseSharedObjectSlabTest() {
    constexpr std::size_t kInstanceCount = 8U;

    kb::scene::Scene scene;
    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Shared Slab Root" });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Shared Slab Child",
        .parentNode = rootNode,
    });
    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().Register("SharedSlabPrefab", std::move(prefab));
    kb::tests::Require(handle.IsValid(), "Shared slab prefab setup did not register the prefab");

    const std::vector<kb::scene::ScenePrefabInstance> instances = scene.Prefabs().InstantiateMany(handle, kInstanceCount);
    kb::tests::Require(instances.size() == kInstanceCount, "Shared slab prefab spawn did not return every instance");
    const kb::scene::ScenePrefabInstantiationStats spawnStats = scene.Prefabs().LastInstantiationStats();
    kb::tests::Require(spawnStats.registeredInstanceCount == kInstanceCount, "Shared slab prefab spawn did not register every instance");
    kb::tests::Require(spawnStats.hasGeneratedEntityIndexRange, "Shared slab prefab spawn did not use the dense created-instance registry path");
    kb::tests::Require(spawnStats.maxGeneratedEntityIndex >= kInstanceCount * 2U - 1U, "Shared slab prefab dense range did not cover every spawned object");

    const std::shared_ptr<const std::vector<kb::scene::SceneObject>> sharedSlab = instances.front().SharedObjectSlab();
    kb::tests::Require(sharedSlab != nullptr, "Registered bulk prefab instances should use one shared object slab");
    kb::tests::Require(sharedSlab->size() == kInstanceCount * 2U, "Registered bulk prefab object slab has invalid size");

    for (std::size_t index = 0; index < instances.size(); ++index) {
        const kb::scene::ScenePrefabInstance& instance = instances[index];
        kb::tests::Require(instance.SharedObjectSlab() == sharedSlab, "Registered bulk prefab instance did not reuse the shared object slab");
        kb::tests::Require(instance.SharedObjectOffset() == index * 2U, "Registered bulk prefab instance used the wrong slab offset");
        kb::tests::Require(instance.SharedObjectCount() == 2U, "Registered bulk prefab instance used the wrong slab count");
        kb::tests::Require(instance.ObjectAt(rootNode).Entity().Id() == (*sharedSlab)[index * 2U].Entity().Id(), "Registered bulk prefab root did not read from the slab segment");
        kb::tests::Require(instance.ObjectAt(childNode).Entity().Id() == (*sharedSlab)[index * 2U + 1U].Entity().Id(), "Registered bulk prefab child did not read from the slab segment");
        kb::tests::Require(scene.Prefabs().RootInstance(instance.ObjectAt(rootNode)) == instance.Handle(), "Shared slab prefab root was not indexed by the registry");
        std::uint32_t containingNode = 99U;
        kb::tests::Require(scene.Prefabs().ContainingInstance(instance.ObjectAt(childNode), containingNode) == instance.Handle(), "Shared slab prefab child was not indexed by the registry");
        kb::tests::Require(containingNode == childNode, "Shared slab prefab child mapped to the wrong node");
    }
}

void RunRegisteredBulkPrefabPooledSlabSurvivesReturnedInstancesTest() {
    constexpr std::size_t kInstanceCount = 16U;

    kb::scene::Scene scene;
    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Pooled Slab Root" });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Pooled Slab Child",
        .parentNode = rootNode,
    });
    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().Register("PooledSlabPrefab", std::move(prefab));
    kb::tests::Require(handle.IsValid(), "Pooled slab prefab setup did not register the prefab");

    std::vector<kb::scene::ScenePrefabInstanceHandle> handles;
    std::vector<kb::scene::SceneObject> roots;
    std::vector<kb::scene::SceneObject> children;
    {
        const std::vector<kb::scene::ScenePrefabInstance> instances = scene.Prefabs().InstantiateMany(handle, kInstanceCount);
        kb::tests::Require(instances.size() == kInstanceCount, "Pooled slab prefab spawn did not return every instance");
        handles.reserve(instances.size());
        roots.reserve(instances.size());
        children.reserve(instances.size());
        for (const kb::scene::ScenePrefabInstance& instance : instances) {
            handles.push_back(instance.Handle());
            roots.push_back(instance.ObjectAt(rootNode));
            children.push_back(instance.ObjectAt(childNode));
        }
    }

    for (std::size_t index = 0; index < handles.size(); ++index) {
        kb::tests::Require(scene.Prefabs().IsInstance(handles[index]), "Pooled slab prefab handle did not survive returned instance destruction");
        kb::tests::Require(scene.Prefabs().RootInstance(roots[index]) == handles[index], "Pooled slab prefab root mapping did not survive returned instance destruction");
        std::uint32_t containingNode = 99U;
        kb::tests::Require(scene.Prefabs().ContainingInstance(children[index], containingNode) == handles[index], "Pooled slab prefab child mapping did not survive returned instance destruction");
        kb::tests::Require(containingNode == childNode, "Pooled slab prefab child mapped to the wrong node after returned instance destruction");
    }
}

void RunRegisteredBulkPrefabReconnectKeepsSourceIdentityTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab sourcePrefab;
    const std::uint32_t sourceRootNode = sourcePrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Source Identity Root" });
    const std::uint32_t sourceChildNode = sourcePrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Source Identity Child",
        .parentNode = sourceRootNode,
    });
    const kb::scene::ScenePrefabHandle sourceHandle = scene.Prefabs().Register("SourceIdentityPrefab", std::move(sourcePrefab));
    kb::tests::Require(sourceHandle.IsValid(), "Registered bulk source identity setup did not register the source prefab");

    kb::scene::ScenePrefab wrongPrefab;
    static_cast<void>(wrongPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Wrong Source Root" }));
    const kb::scene::ScenePrefabHandle wrongHandle = scene.Prefabs().Register("WrongSourceIdentityPrefab", std::move(wrongPrefab));
    kb::tests::Require(wrongHandle.IsValid(), "Registered bulk source identity setup did not register the wrong source prefab");

    const std::vector<kb::scene::ScenePrefabInstance> instances = scene.Prefabs().InstantiateMany(sourceHandle, 3U);
    kb::tests::Require(instances.size() == 3U, "Registered bulk source identity setup did not instantiate the full batch");

    const kb::scene::ScenePrefabInstance& instance = instances.front();
    kb::tests::Require(!scene.Prefabs().Reconnect(instance.Handle(), wrongHandle), "Registered bulk instance accepted a different source identity");
    kb::tests::Require(scene.Prefabs().SourcePrefab(instance.Handle()) == sourceHandle, "Rejected source reconnect changed the instance source handle");
    kb::tests::Require(scene.Prefabs().RootInstance(instance.ObjectAt(sourceRootNode)) == instance.Handle(), "Rejected source reconnect changed the root mapping");
    kb::tests::Require(scene.Entities().Name(instance.ObjectAt(sourceChildNode)) == "Source Identity Child", "Rejected source reconnect changed child data");
}

void RunSceneHistoryRestoresRegisteredPrefabInstanceSnapshotTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Snapshot Registered Root",
    });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Snapshot Registered Child",
        .parentNode = rootNode,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 2.0F, 0.0F },
        },
        .visibility = kb::scene::VisibilityComponent{
            .visible = true,
        },
    });
    const std::uint64_t childNodeId = prefab.TryGetNode(childNode)->stableId;

    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("SnapshotRegisteredPrefab", std::move(prefab));
    kb::tests::Require(prefabHandle.IsValid(), "Snapshot registered prefab setup did not register prefab");
    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(prefabHandle);
    const kb::scene::ScenePrefabInstanceHandle instanceHandle = instance.Handle();
    kb::tests::Require(instanceHandle.IsValid(), "Snapshot registered prefab setup did not instantiate a tracked instance");

    kb::scene::TransformComponent changedTransform = scene.Transforms().Get(instance.ObjectAt(childNode));
    changedTransform.localPosition = kb::scene::Vec3{ 0.0F, 9.0F, 0.0F };
    scene.Transforms().Set(instance.ObjectAt(childNode), changedTransform);
    scene.Entities().SetName(instance.ObjectAt(childNode), "Snapshot Registered Child Override");
    scene.Components().Visibility().Set(instance.ObjectAt(childNode).Entity(), kb::scene::VisibilityComponent{ .visible = false });

    kb::tests::Require(scene.History().Record("registered prefab snapshot"), "Registered prefab snapshot was not recorded");
    scene.Entities().Destroy(instance.RootObject());

    kb::tests::Require(scene.History().Undo(), "Registered prefab snapshot restore failed");
    kb::tests::Require(scene.Prefabs().IsInstance(instanceHandle), "Registered prefab instance handle was not restored");
    kb::tests::Require(scene.Prefabs().SourcePrefab(instanceHandle) == prefabHandle, "Registered prefab source handle was not restored");

    const std::vector<kb::scene::SceneObject> roots = scene.Hierarchy().RootObjects();
    kb::tests::Require(roots.size() == 1U, "Registered prefab snapshot restored the wrong root count");
    const kb::scene::SceneObject restoredRoot = roots.front();
    kb::tests::Require(scene.Prefabs().RootInstance(restoredRoot) == instanceHandle, "Registered prefab root instance mapping was not restored");

    const std::vector<kb::scene::SceneObject> children = scene.Hierarchy().Children(restoredRoot);
    kb::tests::Require(children.size() == 1U, "Registered prefab snapshot restored the wrong child count");
    const kb::scene::SceneObject restoredChild = children.front();
    std::uint32_t restoredNode = 99U;
    std::uint64_t restoredNodeId = 0U;
    kb::tests::Require(scene.Prefabs().ContainingInstance(restoredChild, restoredNode, restoredNodeId) == instanceHandle, "Registered prefab child mapping was not restored");
    kb::tests::Require(restoredNode == childNode, "Registered prefab restored child mapped to the wrong node index");
    kb::tests::Require(restoredNodeId == childNodeId, "Registered prefab restored child mapped to the wrong stable node id");
    kb::tests::Require(scene.Entities().Name(restoredChild) == "Snapshot Registered Child Override", "Registered prefab snapshot did not restore child name override");
    kb::tests::Require(!scene.Components().Visibility().Get(restoredChild.Entity()).visible, "Registered prefab snapshot did not restore visibility override");

    const kb::scene::ScenePrefabOverrideReport report = scene.Prefabs().Overrides(instanceHandle);
    kb::tests::Require(!report.Empty(), "Registered prefab snapshot restore lost override reporting");
    bool foundNameDelta = false;
    bool foundVisibilityDelta = false;
    for (const kb::scene::ScenePrefabPropertyOverride& property : report.properties) {
        foundNameDelta = foundNameDelta || (property.nodeIndex == childNode && property.nodeId == childNodeId && property.propertyPath == "name" && property.value == "Snapshot Registered Child Override");
        foundVisibilityDelta = foundVisibilityDelta || (property.nodeIndex == childNode && property.nodeId == childNodeId && property.propertyPath == "visibility.visible" && property.value == "false");
    }
    kb::tests::Require(foundNameDelta, "Registered prefab snapshot restore lost name override metadata");
    kb::tests::Require(foundVisibilityDelta, "Registered prefab snapshot restore lost visibility override metadata");
}

void RunSceneHistoryRestoresNestedRegisteredPrefabInstanceSnapshotTest() {
    kb::scene::Scene scene;

    const kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Snapshot Plain Parent",
    });

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Snapshot Nested Root",
    });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Snapshot Nested Child",
        .parentNode = rootNode,
        .visibility = kb::scene::VisibilityComponent{
            .visible = true,
        },
    });
    const std::uint64_t childNodeId = prefab.TryGetNode(childNode)->stableId;

    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("SnapshotNestedPrefab", std::move(prefab));
    kb::scene::ScenePrefabInstantiationSettings settings;
    settings.parent = parent;
    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(prefabHandle, settings);
    const kb::scene::ScenePrefabInstanceHandle instanceHandle = instance.Handle();
    kb::tests::Require(instanceHandle.IsValid(), "Nested registered prefab setup did not instantiate a tracked instance");

    scene.Entities().SetName(instance.ObjectAt(childNode), "Snapshot Nested Child Override");
    scene.Components().Visibility().Set(instance.ObjectAt(childNode).Entity(), kb::scene::VisibilityComponent{ .visible = false });

    kb::tests::Require(scene.History().Record("nested registered prefab snapshot"), "Nested registered prefab snapshot was not recorded");
    scene.Entities().Destroy(parent);

    kb::tests::Require(scene.History().Undo(), "Nested registered prefab snapshot restore failed");
    kb::tests::Require(scene.Entities().Count() == 3U, "Nested registered prefab snapshot restored duplicate or missing objects");
    kb::tests::Require(scene.Prefabs().IsInstance(instanceHandle), "Nested registered prefab instance handle was not restored");
    kb::tests::Require(scene.Prefabs().SourcePrefab(instanceHandle) == prefabHandle, "Nested registered prefab source handle was not restored");

    const std::vector<kb::scene::SceneObject> roots = scene.Hierarchy().RootObjects();
    kb::tests::Require(roots.size() == 1U, "Nested registered prefab snapshot restored the wrong root count");
    const kb::scene::SceneObject restoredParent = roots.front();
    kb::tests::Require(scene.Entities().Name(restoredParent) == "Snapshot Plain Parent", "Nested registered prefab snapshot did not restore the plain parent");

    const std::vector<kb::scene::SceneObject> parentChildren = scene.Hierarchy().Children(restoredParent);
    kb::tests::Require(parentChildren.size() == 1U, "Nested registered prefab snapshot restored the wrong parent child count");
    const kb::scene::SceneObject restoredInstanceRoot = parentChildren.front();
    kb::tests::Require(scene.Prefabs().RootInstance(restoredInstanceRoot) == instanceHandle, "Nested registered prefab root mapping was not restored");
    kb::tests::Require(scene.Hierarchy().Parent(restoredInstanceRoot.Entity()) == restoredParent.Entity(), "Nested registered prefab parent relation was not restored");

    const std::vector<kb::scene::SceneObject> instanceChildren = scene.Hierarchy().Children(restoredInstanceRoot);
    kb::tests::Require(instanceChildren.size() == 1U, "Nested registered prefab snapshot restored the wrong instance child count");
    const kb::scene::SceneObject restoredChild = instanceChildren.front();
    std::uint32_t restoredNode = 99U;
    std::uint64_t restoredNodeId = 0U;
    kb::tests::Require(scene.Prefabs().ContainingInstance(restoredChild, restoredNode, restoredNodeId) == instanceHandle, "Nested registered prefab child mapping was not restored");
    kb::tests::Require(restoredNode == childNode && restoredNodeId == childNodeId, "Nested registered prefab child mapped to the wrong node identity");
    kb::tests::Require(scene.Entities().Name(restoredChild) == "Snapshot Nested Child Override", "Nested registered prefab snapshot did not restore child name override");
    kb::tests::Require(!scene.Components().Visibility().Get(restoredChild.Entity()).visible, "Nested registered prefab snapshot did not restore visibility override");
}

void RunSceneHistoryRestoresBulkPrefabArchetypesAndNodeMappingsTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Snapshot Bulk Root",
        .components = kb::scene::ScenePrefabNodeComponents{
            .tags = [] {
                kb::scene::TagsComponent tags;
                kb::scene::SetTagsText(tags, "snapshot-bulk-root");
                return tags;
            }(),
        },
    });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Snapshot Bulk Child",
        .parentNode = rootNode,
        .components = kb::scene::ScenePrefabNodeComponents{
            .camera = kb::scene::CameraComponent{
                .primary = true,
            },
            .light = kb::scene::LightComponent{
                .kind = kb::scene::LightKind::Point,
                .intensity = 2.0F,
            },
        },
    });
    const std::uint64_t rootNodeId = prefab.TryGetNode(rootNode)->stableId;
    const std::uint64_t childNodeId = prefab.TryGetNode(childNode)->stableId;

    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("SnapshotBulkPrefab", std::move(prefab));
    const std::vector<kb::scene::ScenePrefabInstance> instances = scene.Prefabs().InstantiateMany(prefabHandle, 3U);
    kb::tests::Require(instances.size() == 3U, "Snapshot bulk prefab setup did not create all registered instances");

    std::vector<kb::scene::ScenePrefabInstanceHandle> expectedHandles;
    expectedHandles.reserve(instances.size());
    for (const kb::scene::ScenePrefabInstance& instance : instances) {
        expectedHandles.push_back(instance.Handle());
    }

    kb::tests::Require(scene.History().Record("bulk prefab snapshot"), "Bulk prefab snapshot was not recorded");
    for (const kb::scene::ScenePrefabInstance& instance : instances) {
        scene.Entities().Destroy(instance.RootObject());
    }
    kb::tests::Require(scene.Entities().Count() == 0U, "Bulk prefab snapshot setup did not clear scene entities");

    kb::tests::Require(scene.History().Undo(), "Bulk prefab snapshot restore failed");
    kb::tests::Require(scene.Entities().Count() == instances.size() * 2U, "Bulk prefab snapshot restored the wrong entity count");

    const kb::ecs::World& world = scene.Runtime().EcsWorld();
    const std::vector<kb::ecs::ComponentId> rootArchetype{
        world.Component<kb::scene::TransformComponent>(),
        world.Component<kb::scene::VisibilityComponent>(),
        world.Component<kb::scene::TagsComponent>(),
    };
    const std::vector<kb::ecs::ComponentId> childArchetype{
        world.Component<kb::scene::TransformComponent>(),
        world.Component<kb::scene::VisibilityComponent>(),
        world.Component<kb::scene::CameraComponent>(),
        world.Component<kb::scene::LightComponent>(),
    };

    std::size_t matchedInstances = 0U;
    for (const kb::scene::SceneObject restoredRoot : scene.Hierarchy().RootObjects()) {
        const kb::scene::ScenePrefabInstanceHandle restoredHandle = scene.Prefabs().RootInstance(restoredRoot);
        kb::tests::Require(std::find(expectedHandles.begin(), expectedHandles.end(), restoredHandle) != expectedHandles.end(), "Bulk prefab snapshot restored an unexpected instance handle");
        kb::tests::Require(scene.Prefabs().SourcePrefab(restoredHandle) == prefabHandle, "Bulk prefab snapshot restored the wrong source prefab");

        std::uint32_t rootMapping = 99U;
        std::uint64_t rootMappingId = 0U;
        kb::tests::Require(scene.Prefabs().ContainingInstance(restoredRoot, rootMapping, rootMappingId) == restoredHandle, "Bulk prefab snapshot did not restore root node mapping");
        kb::tests::Require(rootMapping == rootNode && rootMappingId == rootNodeId, "Bulk prefab snapshot restored the wrong root node identity");
        kb::tests::Require(world.NativeStorage().EntityArchetypeMatches(restoredRoot.Entity(), std::span<const kb::ecs::ComponentId>{ rootArchetype }), "Bulk prefab snapshot restored root into the wrong archetype");

        const std::vector<kb::scene::SceneObject> children = scene.Hierarchy().Children(restoredRoot);
        kb::tests::Require(children.size() == 1U, "Bulk prefab snapshot restored the wrong child count");
        const kb::scene::SceneObject restoredChild = children.front();
        std::uint32_t childMapping = 99U;
        std::uint64_t childMappingId = 0U;
        kb::tests::Require(scene.Prefabs().ContainingInstance(restoredChild, childMapping, childMappingId) == restoredHandle, "Bulk prefab snapshot did not restore child node mapping");
        kb::tests::Require(childMapping == childNode && childMappingId == childNodeId, "Bulk prefab snapshot restored the wrong child node identity");
        kb::tests::Require(world.NativeStorage().EntityArchetypeMatches(restoredChild.Entity(), std::span<const kb::ecs::ComponentId>{ childArchetype }), "Bulk prefab snapshot restored child into the wrong archetype");
        ++matchedInstances;
    }
    kb::tests::Require(matchedInstances == expectedHandles.size(), "Bulk prefab snapshot did not restore every registered instance");
}

void RunSceneHistoryRestoresBulkPrefabResolvedBaselineTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "History Bulk Baseline Root" });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "History Bulk Baseline Child",
        .parentNode = rootNode,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 1.0F, 0.0F },
        },
    });
    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("HistoryBulkResolvedBaselinePrefab", std::move(prefab));
    kb::tests::Require(prefabHandle.IsValid(), "History bulk baseline setup did not register prefab");

    const std::vector<kb::scene::ScenePrefabInstance> instances = scene.Prefabs().InstantiateMany(prefabHandle, 3U);
    kb::tests::Require(instances.size() == 3U, "History bulk baseline setup did not instantiate the batch");

    kb::scene::TransformComponent localOverride = scene.Transforms().Get(instances[1U].ObjectAt(childNode));
    localOverride.localPosition = kb::scene::Vec3{ 0.0F, 42.0F, 0.0F };
    scene.Transforms().Set(instances[1U].ObjectAt(childNode), localOverride);

    kb::tests::Require(scene.History().Record("bulk resolved baseline"), "History bulk baseline snapshot was not recorded");

    kb::scene::ScenePrefabPrivateScene privateScene = scene.Prefabs().OpenPrivateScene(prefabHandle);
    kb::tests::Require(privateScene.IsValid(), "History bulk baseline private scene did not open");
    privateScene.EditScene().Entities().SetName(privateScene.ObjectAt(childNode), "History Bulk Baseline Child Updated");
    kb::tests::Require(privateScene.Apply(), "History bulk baseline private scene apply failed");
    kb::tests::Require(scene.Entities().Name(instances.front().ObjectAt(childNode)) == "History Bulk Baseline Child Updated", "History bulk baseline apply did not refresh inherited instance data");

    kb::tests::Require(scene.History().Undo(), "History bulk baseline undo failed");

    auto findRestoredRoot = [&scene](kb::scene::ScenePrefabInstanceHandle handle) {
        for (const kb::scene::SceneObject root : scene.Hierarchy().RootObjects()) {
            if (scene.Prefabs().RootInstance(root) == handle) {
                return root;
            }
        }
        return kb::scene::SceneObject{};
    };

    const kb::scene::SceneObject restoredRoot = findRestoredRoot(instances[1U].Handle());
    kb::tests::Require(restoredRoot.IsValid(), "History bulk baseline undo did not restore the tracked instance root");
    const std::vector<kb::scene::SceneObject> restoredChildren = scene.Hierarchy().Children(restoredRoot);
    kb::tests::Require(restoredChildren.size() == 1U, "History bulk baseline undo restored an invalid child count");
    const kb::scene::SceneObject restoredChild = restoredChildren.front();
    kb::tests::Require(scene.Entities().Name(restoredChild) == "History Bulk Baseline Child", "History bulk baseline undo did not restore original child data");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(restoredChild).localPosition.y, 42.0F), "History bulk baseline undo lost the local transform override");

    kb::tests::Require(scene.Prefabs().RefreshInstances(prefabHandle) == instances.size(), "History bulk baseline refresh did not process every restored batch instance");
    kb::tests::Require(scene.Entities().Name(restoredChild) == "History Bulk Baseline Child Updated", "History bulk baseline refresh treated inherited source data as a local override");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(restoredChild).localPosition.y, 42.0F), "History bulk baseline refresh lost the local transform override");
}

void RunRegisteredPrefabOverrideLifecycleTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Override Root",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
        },
    });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Override Child",
        .parentNode = rootNode,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 2.0F, 0.0F },
        },
        .visibility = kb::scene::VisibilityComponent{
            .visible = true,
        },
    });

    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("OverridePrefab", std::move(prefab));
    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(prefabHandle);
    const kb::scene::ScenePrefabInstanceHandle instanceHandle = instance.Handle();

    kb::scene::TransformComponent changedTransform = scene.Transforms().Get(instance.ObjectAt(childNode));
    changedTransform.localPosition = kb::scene::Vec3{ 0.0F, 8.0F, 0.0F };
    scene.Transforms().Set(instance.ObjectAt(childNode), changedTransform);
    scene.Entities().SetName(instance.ObjectAt(childNode), "Changed Child");
    scene.Components().Visibility().Set(instance.ObjectAt(childNode).Entity(), kb::scene::VisibilityComponent{ .visible = false });

    const kb::scene::ScenePrefabOverrideReport report = scene.Prefabs().Overrides(instanceHandle);
    kb::tests::Require(report.nodes.size() == 1, "Prefab override detector should report exactly one changed node");
    kb::tests::Require(report.properties.size() >= 3, "Prefab override detector should report property-level deltas");
    kb::tests::Require(report.nodes[0].nodeIndex == childNode, "Prefab override detector reported the wrong node");
    kb::tests::Require(kb::scene::HasPrefabOverride(report.nodes[0].flags, kb::scene::ScenePrefabOverrideFlag::Name), "Prefab override detector missed name override");
    kb::tests::Require(kb::scene::HasPrefabOverride(report.nodes[0].flags, kb::scene::ScenePrefabOverrideFlag::Transform), "Prefab override detector missed transform override");
    kb::tests::Require(kb::scene::HasPrefabOverride(report.nodes[0].flags, kb::scene::ScenePrefabOverrideFlag::Visibility), "Prefab override detector missed visibility override");
    bool foundNameDelta = false;
    bool foundPositionDelta = false;
    bool foundVisibilityDelta = false;
    for (const kb::scene::ScenePrefabPropertyOverride& property : report.properties) {
        foundNameDelta = foundNameDelta || (property.nodeIndex == childNode && property.propertyPath == "name" && property.value == "Changed Child" && property.target.Entity() == instance.ObjectAt(childNode).Entity());
        foundPositionDelta = foundPositionDelta || (property.nodeIndex == childNode && property.propertyPath == "transform.localPosition");
        foundVisibilityDelta = foundVisibilityDelta || (property.nodeIndex == childNode && property.propertyPath == "visibility.visible" && property.value == "false");
    }
    kb::tests::Require(foundNameDelta, "Prefab override detector missed name property delta");
    kb::tests::Require(foundPositionDelta, "Prefab override detector missed transform property delta");
    kb::tests::Require(foundVisibilityDelta, "Prefab override detector missed visibility property delta");

    kb::tests::Require(scene.Prefabs().RevertOverride(instanceHandle, childNode, "name"), "Prefab single property revert failed");
    kb::tests::Require(scene.Entities().Name(instance.ObjectAt(childNode)) == "Override Child", "Prefab single property revert did not restore name");
    scene.Entities().SetName(instance.ObjectAt(childNode), "Applied Name");
    kb::tests::Require(scene.Prefabs().ApplyOverride(instanceHandle, childNode, "name"), "Prefab single property apply failed");
    kb::tests::Require(scene.Prefabs().RevertOverride(instanceHandle, childNode, "name"), "Prefab single property revert after apply failed");
    kb::tests::Require(scene.Entities().Name(instance.ObjectAt(childNode)) == "Applied Name", "Prefab single property apply did not update the prefab baseline");
    scene.Entities().SetName(instance.ObjectAt(childNode), "Override Child");
    kb::tests::Require(scene.Prefabs().ApplyOverride(instanceHandle, childNode, "name"), "Prefab single property apply could not restore the baseline name");
    scene.Entities().SetName(instance.ObjectAt(childNode), "Changed Child");

    kb::tests::Require(scene.Prefabs().RevertOverrides(instanceHandle), "Prefab override revert failed");
    kb::tests::Require(scene.Prefabs().Overrides(instanceHandle).Empty(), "Prefab override revert did not restore the instance");
    kb::tests::Require(scene.Entities().Name(instance.ObjectAt(childNode)) == "Override Child", "Prefab override revert did not restore name");
    kb::tests::Require(scene.Components().Visibility().Get(instance.ObjectAt(childNode).Entity()).visible, "Prefab override revert did not restore visibility");

    kb::scene::SceneObject temporaryChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Temporary Added Child",
        .parent = instance.ObjectAt(rootNode),
    });
    const kb::scene::ScenePrefabOverrideReport addedChildReport = scene.Prefabs().Overrides(instanceHandle);
    kb::tests::Require(addedChildReport.nodes.size() == 1, "Prefab override detector should report added child on the parent node");
    kb::tests::Require(addedChildReport.nodes[0].nodeIndex == rootNode, "Prefab added child override should be reported on the parent node");
    kb::tests::Require(kb::scene::HasPrefabOverride(addedChildReport.nodes[0].flags, kb::scene::ScenePrefabOverrideFlag::AddedChild), "Prefab override detector missed added child override");
    kb::tests::Require(scene.Prefabs().RevertOverrides(instanceHandle), "Prefab added child revert failed");
    kb::tests::Require(!scene.Entities().IsAlive(temporaryChild), "Prefab added child revert should destroy the added object");
    kb::tests::Require(scene.Prefabs().Overrides(instanceHandle).Empty(), "Prefab added child revert did not restore clean instance state");

    changedTransform = scene.Transforms().Get(instance.ObjectAt(childNode));
    changedTransform.localPosition = kb::scene::Vec3{ 0.0F, 12.0F, 0.0F };
    scene.Transforms().Set(instance.ObjectAt(childNode), changedTransform);
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Applied Extra Child",
        .parent = instance.ObjectAt(rootNode),
    }));
    kb::tests::Require(scene.Prefabs().ApplyOverrides(instanceHandle), "Prefab override apply failed");
    kb::tests::Require(scene.Prefabs().Overrides(instanceHandle).Empty(), "Prefab override apply did not update the prefab baseline");

    const kb::scene::ScenePrefabInstance appliedInstance = scene.Prefabs().Instantiate(prefabHandle);
    kb::tests::Require(appliedInstance.ObjectCount() == 3, "Prefab override apply did not preserve added child in future instances");
    const kb::scene::TransformComponent appliedChildTransform = scene.Transforms().Get(appliedInstance.ObjectAt(childNode));
    kb::tests::Require(kb::tests::NearlyEqual(appliedChildTransform.localPosition.y, 12.0F), "Prefab override apply did not affect future instances");
    bool foundAppliedExtraChild = false;
    for (const kb::scene::SceneObject object : appliedInstance.Objects()) {
        foundAppliedExtraChild = foundAppliedExtraChild || scene.Entities().Name(object) == "Applied Extra Child";
    }
    kb::tests::Require(foundAppliedExtraChild, "Prefab override apply did not add the new child to the prefab baseline");
}

void RunRegisteredPrefabBatchApplyOverridesTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Batch Apply Root" });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Batch Apply Child",
        .parentNode = rootNode,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 1.0F, 0.0F },
        },
    });
    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("BatchApplyPrefab", std::move(prefab));

    const std::vector<kb::scene::ScenePrefabInstance> instances = scene.Prefabs().InstantiateMany(prefabHandle, 4U);
    kb::tests::Require(instances.size() == 4U, "Prefab batch apply setup did not instantiate all instances");

    std::vector<kb::scene::ScenePrefabInstanceHandle> handles;
    handles.reserve(instances.size());
    for (const kb::scene::ScenePrefabInstance& instance : instances) {
        handles.push_back(instance.Handle());
        scene.Entities().SetName(instance.ObjectAt(childNode), "Batch Applied Child");
        kb::scene::TransformComponent transform = scene.Transforms().Get(instance.ObjectAt(childNode));
        transform.localPosition.y = 9.0F;
        scene.Transforms().Set(instance.ObjectAt(childNode), transform);
    }

    kb::tests::Require(scene.Prefabs().ApplyOverrides(std::span<const kb::scene::ScenePrefabInstanceHandle>{ handles }), "Prefab batch apply rejected valid handles");
    for (const kb::scene::ScenePrefabInstance& instance : instances) {
        kb::tests::Require(scene.Prefabs().Overrides(instance.Handle()).Empty(), "Prefab batch apply left instance overrides dirty");
    }

    const kb::scene::ScenePrefabInstance futureInstance = scene.Prefabs().Instantiate(prefabHandle);
    kb::tests::Require(scene.Entities().Name(futureInstance.ObjectAt(childNode)) == "Batch Applied Child", "Prefab batch apply did not update future instance name");
    const kb::scene::TransformComponent futureTransform = scene.Transforms().Get(futureInstance.ObjectAt(childNode));
    kb::tests::Require(kb::tests::NearlyEqual(futureTransform.localPosition.y, 9.0F), "Prefab batch apply did not update future instance transform");

    for (const kb::scene::ScenePrefabInstance& instance : instances) {
        scene.Entities().SetName(instance.ObjectAt(childNode), "Batch Reverted Child");
    }
    kb::tests::Require(scene.Prefabs().RevertOverrides(std::span<const kb::scene::ScenePrefabInstanceHandle>{ handles }), "Prefab batch revert rejected valid handles");
    for (const kb::scene::ScenePrefabInstance& instance : instances) {
        kb::tests::Require(scene.Prefabs().Overrides(instance.Handle()).Empty(), "Prefab batch revert left instance overrides dirty");
        kb::tests::Require(scene.Entities().Name(instance.ObjectAt(childNode)) == "Batch Applied Child", "Prefab batch revert did not restore the baseline name");
    }

    for (std::size_t index = 0; index < instances.size(); ++index) {
        scene.Entities().SetName(instances[index].ObjectAt(childNode), "Distinct Batch Child " + std::to_string(index));
    }
    kb::tests::Require(scene.Prefabs().ApplyOverrides(std::span<const kb::scene::ScenePrefabInstanceHandle>{ handles }), "Prefab batch apply rejected distinct overrides");
    const kb::scene::ScenePrefabInstance distinctFutureInstance = scene.Prefabs().Instantiate(prefabHandle);
    kb::tests::Require(scene.Entities().Name(distinctFutureInstance.ObjectAt(childNode)) == "Distinct Batch Child 3", "Prefab batch apply did not preserve sequential distinct override semantics");
}

void RunRegisteredPrefabBatchApplyDirtyNodeSupersetRegressionTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Dirty Superset Root",
        .components = kb::scene::ScenePrefabNodeComponents{
            .meshRenderer = kb::scene::MeshRendererComponent{
                .meshAssetId = 10,
                .materialAssetId = 20,
            },
        },
    });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Dirty Superset Child",
        .parentNode = rootNode,
    });
    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("DirtySupersetBatchApplyPrefab", std::move(prefab));

    const std::vector<kb::scene::ScenePrefabInstance> instances = scene.Prefabs().InstantiateMany(prefabHandle, 2U);
    kb::tests::Require(instances.size() == 2U, "Dirty-node batch apply setup did not instantiate all instances");

    scene.Components().MeshRenderers().Set(
        instances[0].ObjectAt(rootNode).Entity(),
        kb::scene::MeshRendererComponent{
            .meshAssetId = 77,
            .materialAssetId = 88,
        });
    scene.Components().MeshRenderers().Set(
        instances[1].ObjectAt(rootNode).Entity(),
        kb::scene::MeshRendererComponent{
            .meshAssetId = 99,
            .materialAssetId = 111,
        });
    scene.Entities().SetName(instances[0].ObjectAt(childNode), "Dirty Superset Applied Child");
    scene.Entities().SetName(instances[1].ObjectAt(childNode), "Dirty Superset Applied Child");

    const std::array handles{
        instances[0].Handle(),
        instances[1].Handle(),
    };
    kb::tests::Require(scene.Prefabs().ApplyOverrides(std::span<const kb::scene::ScenePrefabInstanceHandle>{ handles }), "Dirty-node batch apply regression rejected valid handles");

    const kb::scene::ScenePrefabInstance futureInstance = scene.Prefabs().Instantiate(prefabHandle);
    const kb::scene::MeshRendererComponent* futureRootMesh = scene.Components().MeshRenderers().TryGet(futureInstance.ObjectAt(rootNode).Entity());
    kb::tests::Require(futureRootMesh != nullptr, "Dirty-node batch apply regression removed the future root renderer");
    kb::tests::Require(futureRootMesh->meshAssetId == 99 && futureRootMesh->materialAssetId == 111, "Dirty-node batch apply did not let the later conflicting component override win");
    kb::tests::Require(scene.Entities().Name(futureInstance.ObjectAt(childNode)) == "Dirty Superset Applied Child", "Dirty-node batch apply regression did not preserve the final child override");
}

void RunRegisteredPrefabBatchRevertTopologyDirtyRegressionTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Topology Dirty Root" });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Topology Dirty Child",
        .parentNode = rootNode,
    });
    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("TopologyDirtyBatchRevertPrefab", std::move(prefab));

    const std::vector<kb::scene::ScenePrefabInstance> instances = scene.Prefabs().InstantiateMany(prefabHandle, 2U);
    kb::tests::Require(instances.size() == 2U, "Topology dirty batch revert setup did not instantiate all instances");

    std::vector<kb::scene::SceneObject> untrackedChildren;
    untrackedChildren.reserve(instances.size());
    std::vector<kb::scene::ScenePrefabInstanceHandle> handles;
    handles.reserve(instances.size());
    for (const kb::scene::ScenePrefabInstance& instance : instances) {
        handles.push_back(instance.Handle());
        kb::scene::SceneObject extra = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Topology Dirty Extra",
            .parent = instance.RootObject(),
        });
        kb::tests::Require(extra.IsValid(), "Topology dirty batch revert setup failed to create an untracked child");
        kb::tests::Require(scene.Hierarchy().Parent(extra.Entity()) == instance.RootObject().Entity(), "Topology dirty batch revert setup did not parent the untracked child");
        scene.Entities().SetName(instance.ObjectAt(childNode), "Topology Dirty Override");
        untrackedChildren.push_back(extra);
    }

    kb::tests::Require(scene.Prefabs().RevertOverrides(std::span<const kb::scene::ScenePrefabInstanceHandle>{ handles }), "Topology dirty batch revert rejected valid handles");
    for (std::size_t index = 0; index < instances.size(); ++index) {
        kb::tests::Require(!scene.Entities().IsAlive(untrackedChildren[index]), "Topology dirty batch revert did not remove an untracked child");
        kb::tests::Require(scene.Entities().Name(instances[index].ObjectAt(childNode)) == "Topology Dirty Child", "Topology dirty batch revert did not restore the tracked child name");
    }
}

void RunMissingPrefabInstanceObjectOverrideTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Missing Root" });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Missing Child",
        .parentNode = rootNode,
    });

    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("MissingObjectPrefab", std::move(prefab));
    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(prefabHandle);
    const kb::scene::ScenePrefabInstanceHandle instanceHandle = instance.Handle();

    scene.Entities().Destroy(instance.ObjectAt(childNode));

    const kb::scene::ScenePrefabOverrideReport report = scene.Prefabs().Overrides(instanceHandle);
    kb::tests::Require(report.nodes.size() == 1, "Prefab missing object override should report the destroyed node");
    kb::tests::Require(report.nodes[0].nodeIndex == childNode, "Prefab missing object override reported the wrong node");
    kb::tests::Require(kb::scene::HasPrefabOverride(report.nodes[0].flags, kb::scene::ScenePrefabOverrideFlag::MissingObject), "Prefab override detector missed missing object");
    kb::tests::Require(!scene.Prefabs().RevertOverrides(instanceHandle), "Prefab override revert should fail when an instance object is missing");
    kb::tests::Require(scene.Prefabs().ApplyOverrides(instanceHandle), "Prefab override apply should remove a missing child node from the prefab baseline");
    kb::tests::Require(scene.Prefabs().Get(prefabHandle).NodeCount() == 1U, "Prefab missing child apply did not remove the prefab node");
    const kb::scene::ScenePrefabInstance childRemovedInstance = scene.Prefabs().Instantiate(prefabHandle);
    kb::tests::Require(childRemovedInstance.ObjectCount() == 1U, "Prefab missing child apply did not affect future instances");

    kb::scene::ScenePrefab rootMissingPrefab;
    static_cast<void>(rootMissingPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Missing Root Only" }));
    const kb::scene::ScenePrefabHandle rootMissingHandle = scene.Prefabs().Register("MissingRootPrefab", std::move(rootMissingPrefab));
    const kb::scene::ScenePrefabInstance rootMissingInstance = scene.Prefabs().Instantiate(rootMissingHandle);
    scene.Entities().Destroy(rootMissingInstance.ObjectAt(0U));
    kb::tests::Require(!scene.Prefabs().ApplyOverrides(rootMissingInstance.Handle()), "Prefab apply should reject a missing root object");
}

void RunRegisteredPrefabFullComponentOverrideLifecycleTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Full Component Root",
        .components = kb::scene::ScenePrefabNodeComponents{
            .meshRenderer = kb::scene::MeshRendererComponent{
                .meshAssetId = 40,
                .materialAssetId = 41,
                .materialSlotAssetIds = { 42 },
                .materialSlotOverrideCount = 1,
            },
            .input = kb::scene::InputComponent{
                .mappingContextAssetId = 10,
                .priority = 1,
                .enabled = true,
            },
            .rigidbody = kb::scene::RigidbodyComponent{
                .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
                .mass = 2.0F,
                .linearVelocity = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
            },
            .collider = kb::scene::ColliderComponent{
                .shape = kb::scene::ColliderShape::Box,
                .radius = 0.5F,
            },
            .behaviour = kb::scene::BehaviourComponent{
                .behaviourAssetId = 20,
                .backend = kb::scene::BehaviourBackend::Lua,
                .enabled = true,
                .tickGroup = kb::scene::BehaviourTickGroup::Gameplay,
                .executionOrder = 3,
            },
            .audioSource = kb::scene::AudioSourceComponent{
                .clipAssetId = 30,
                .volume = 0.75F,
                .enabled = true,
            },
            .audioListener = kb::scene::AudioListenerComponent{
                .primary = true,
                .enabled = true,
            },
        },
    });
    kb::scene::SetTagsText(prefab.TryGetMutableNode(rootNode)->components.tags.emplace(), "Prefab, Base");

    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("FullComponentOverridePrefab", std::move(prefab));
    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(prefabHandle);
    const kb::scene::SceneEntity entity = instance.ObjectAt(rootNode).Entity();

    scene.Components().Inputs().Set(entity, kb::scene::InputComponent{ .mappingContextAssetId = 10, .priority = 9, .enabled = false });
    scene.Components().Rigidbodies().Set(entity, kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Kinematic, .mass = 5.0F });
    scene.Components().Colliders().Remove(entity);
    kb::scene::TagsComponent changedTags;
    kb::scene::SetTagsText(changedTags, "Prefab, Changed");
    scene.Components().Tags().Set(entity, changedTags);
    scene.Components().Behaviours().Set(entity, kb::scene::BehaviourComponent{ .behaviourAssetId = 20, .backend = kb::scene::BehaviourBackend::Lua, .enabled = false, .tickGroup = kb::scene::BehaviourTickGroup::Camera, .executionOrder = -8 });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{ .meshAssetId = 40, .materialAssetId = 55, .materialSlotAssetIds = { 42, 66 }, .materialSlotOverrideCount = 2 });
    scene.Components().AudioSources().Set(entity, kb::scene::AudioSourceComponent{ .clipAssetId = 30, .volume = 0.2F, .enabled = false, .mute = true });
    scene.Components().AudioListeners().Set(entity, kb::scene::AudioListenerComponent{ .primary = false, .enabled = false });

    const kb::scene::ScenePrefabOverrideReport report = scene.Prefabs().Overrides(instance.Handle());
    kb::tests::Require(report.nodes.size() == 1, "Full component prefab override detector should report one changed node");
    const kb::scene::ScenePrefabOverrideFlag flags = report.nodes[0].flags;
    kb::tests::Require(kb::scene::HasPrefabOverride(flags, kb::scene::ScenePrefabOverrideFlag::Input), "Prefab override detector missed input override");
    kb::tests::Require(kb::scene::HasPrefabOverride(flags, kb::scene::ScenePrefabOverrideFlag::Rigidbody), "Prefab override detector missed rigidbody override");
    kb::tests::Require(kb::scene::HasPrefabOverride(flags, kb::scene::ScenePrefabOverrideFlag::Collider), "Prefab override detector missed collider override");
    kb::tests::Require(kb::scene::HasPrefabOverride(flags, kb::scene::ScenePrefabOverrideFlag::Tags), "Prefab override detector missed tags override");
    kb::tests::Require(kb::scene::HasPrefabOverride(flags, kb::scene::ScenePrefabOverrideFlag::Behaviour), "Prefab override detector missed behaviour override");
    kb::tests::Require(kb::scene::HasPrefabOverride(flags, kb::scene::ScenePrefabOverrideFlag::MeshRenderer), "Prefab override detector missed mesh renderer override");
    kb::tests::Require(kb::scene::HasPrefabOverride(flags, kb::scene::ScenePrefabOverrideFlag::AudioSource), "Prefab override detector missed audio source override");
    kb::tests::Require(kb::scene::HasPrefabOverride(flags, kb::scene::ScenePrefabOverrideFlag::AudioListener), "Prefab override detector missed audio listener override");

    bool foundAudioVolume = false;
    bool foundColliderRemoval = false;
    bool foundMeshMaterial = false;
    bool foundMeshMaterialSlot = false;
    for (const kb::scene::ScenePrefabPropertyOverride& property : report.properties) {
        foundAudioVolume = foundAudioVolume || property.propertyPath == "audioSource.volume";
        foundColliderRemoval = foundColliderRemoval || (property.propertyPath == "collider" && property.value == "null");
        foundMeshMaterial = foundMeshMaterial || (property.propertyPath == "meshRenderer.materialAssetId" && property.value == "55");
        foundMeshMaterialSlot = foundMeshMaterialSlot || (property.propertyPath == "meshRenderer.materialSlotAssetId.1" && property.value == "66");
    }
    kb::tests::Require(foundAudioVolume, "Prefab override detector missed audio source property delta");
    kb::tests::Require(foundColliderRemoval, "Prefab override detector missed collider removal delta");
    kb::tests::Require(foundMeshMaterial, "Prefab override detector missed mesh renderer material property delta");
    kb::tests::Require(foundMeshMaterialSlot, "Prefab override detector missed mesh renderer material slot property delta");

    kb::tests::Require(scene.Prefabs().RevertOverrides(instance.Handle()), "Full component prefab override revert failed");
    const kb::scene::InputComponent* revertedInput = scene.Components().Inputs().TryGet(entity);
    const kb::scene::ColliderComponent* revertedCollider = scene.Components().Colliders().TryGet(entity);
    const kb::scene::TagsComponent* revertedTags = scene.Components().Tags().TryGet(entity);
    const kb::scene::MeshRendererComponent* revertedMeshRenderer = scene.Components().MeshRenderers().TryGet(entity);
    const kb::scene::AudioSourceComponent* revertedAudioSource = scene.Components().AudioSources().TryGet(entity);
    kb::tests::Require(revertedInput != nullptr && revertedInput->priority == 1 && revertedInput->enabled, "Full component prefab revert did not restore input");
    kb::tests::Require(revertedCollider != nullptr && revertedCollider->shape == kb::scene::ColliderShape::Box, "Full component prefab revert did not restore collider");
    kb::tests::Require(revertedTags != nullptr && kb::scene::TagsText(*revertedTags) == "Prefab, Base", "Full component prefab revert did not restore tags");
    kb::tests::Require(revertedMeshRenderer != nullptr && revertedMeshRenderer->materialAssetId == 41 && revertedMeshRenderer->materialSlotOverrideCount == 1 && revertedMeshRenderer->materialSlotAssetIds[0] == 42, "Full component prefab revert did not restore mesh renderer material");
    kb::tests::Require(revertedAudioSource != nullptr && kb::tests::NearlyEqual(revertedAudioSource->volume, 0.75F) && !revertedAudioSource->mute, "Full component prefab revert did not restore audio source");

    scene.Components().AudioSources().Set(entity, kb::scene::AudioSourceComponent{ .clipAssetId = 30, .volume = 0.15F, .enabled = true });
    kb::tests::Require(scene.Prefabs().ApplyOverride(instance.Handle(), rootNode, "audioSource.volume"), "Full component prefab apply property failed");
    const kb::scene::ScenePrefabInstance appliedInstance = scene.Prefabs().Instantiate(prefabHandle);
    const kb::scene::AudioSourceComponent* appliedAudioSource = scene.Components().AudioSources().TryGet(appliedInstance.ObjectAt(rootNode).Entity());
    kb::tests::Require(appliedAudioSource != nullptr && kb::tests::NearlyEqual(appliedAudioSource->volume, 0.15F), "Full component prefab apply did not update future instances");
}

void RunPrefabApplyRejectsDetachedTrackedChildTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Detached Root" });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Detached Child",
        .parentNode = rootNode,
    });

    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("DetachedTrackedChildPrefab", std::move(prefab));
    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(prefabHandle);
    const kb::scene::ScenePrefabInstanceHandle instanceHandle = instance.Handle();
    kb::scene::SceneObject externalParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "External Parent" });

    kb::tests::Require(scene.Hierarchy().SetParent(instance.ObjectAt(childNode), externalParent), "Prefab child could not be detached to an external parent");
    const kb::scene::ScenePrefabOverrideReport report = scene.Prefabs().Overrides(instanceHandle);
    kb::tests::Require(report.nodes.size() == 1, "Detached prefab child should report one structural override");
    kb::tests::Require(report.nodes[0].nodeIndex == childNode, "Detached prefab child override reported the wrong node");
    kb::tests::Require(kb::scene::HasPrefabOverride(report.nodes[0].flags, kb::scene::ScenePrefabOverrideFlag::Parent), "Detached prefab child did not report a parent override");
    kb::tests::Require(!scene.Prefabs().ApplyOverrides(instanceHandle), "Prefab apply should reject tracked children moved outside the prefab root");
    kb::tests::Require(scene.Prefabs().RevertOverrides(instanceHandle), "Prefab revert should restore a detached tracked child");
    kb::tests::Require(scene.Hierarchy().Parent(instance.ObjectAt(childNode).Entity()) == instance.ObjectAt(rootNode).Entity(), "Prefab revert did not restore detached child parent");
}

void RunPrefabVariantInstantiationTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Base Root",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F } },
    });
    const kb::scene::ScenePrefabHandle baseHandle = scene.Prefabs().Register("BasePrefab", std::move(prefab));
    kb::scene::ScenePrefabPropertyOverride nameOverride{
        .nodeIndex = rootNode,
        .propertyPath = "name",
        .value = "Variant Root",
        .flag = kb::scene::ScenePrefabOverrideFlag::Name,
    };
    kb::scene::ScenePrefabPropertyOverride positionOverride{
        .nodeIndex = rootNode,
        .propertyPath = "transform.localPosition",
        .value = "5 0 0",
        .flag = kb::scene::ScenePrefabOverrideFlag::Transform,
    };

    std::vector<kb::scene::ScenePrefabPropertyOverride> overrides{ nameOverride, positionOverride };
    const kb::scene::ScenePrefabHandle variantHandle = scene.Prefabs().RegisterVariant("VariantPrefab", baseHandle, overrides);
    kb::tests::Require(variantHandle.IsValid(), "Prefab variant registration failed");

    const kb::scene::ScenePrefabInstance baseInstance = scene.Prefabs().Instantiate(baseHandle);
    const kb::scene::ScenePrefabInstance variantInstance = scene.Prefabs().Instantiate(variantHandle);
    kb::tests::Require(scene.Entities().Name(baseInstance.ObjectAt(rootNode)) == "Base Root", "Prefab variant mutated the base prefab");
    kb::tests::Require(scene.Entities().Name(variantInstance.ObjectAt(rootNode)) == "Variant Root", "Prefab variant did not apply name override");
    const kb::scene::TransformComponent transform = scene.Transforms().Get(variantInstance.ObjectAt(rootNode));
    kb::tests::Require(kb::tests::NearlyEqual(transform.localPosition.x, 5.0F), "Prefab variant did not apply transform override");

    scene.Components().Visibility().Set(baseInstance.ObjectAt(rootNode).Entity(), kb::scene::VisibilityComponent{ .visible = false });
    kb::tests::Require(scene.Prefabs().ApplyOverride(baseInstance.Handle(), rootNode, "visibility.visible"), "Prefab base apply did not accept visibility override");
    const kb::scene::ScenePrefabInstance refreshedVariantInstance = scene.Prefabs().Instantiate(variantHandle);
    kb::tests::Require(scene.Entities().Name(refreshedVariantInstance.ObjectAt(rootNode)) == "Variant Root", "Prefab variant lost its own name override after base refresh");
    kb::tests::Require(!scene.Components().Visibility().Get(refreshedVariantInstance.ObjectAt(rootNode).Entity()).visible, "Prefab variant did not inherit refreshed base data");
}

void RunPrefabVariantApplyUpdatesVariantOnlyTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Variant Apply Base" });
    const kb::scene::ScenePrefabHandle baseHandle = scene.Prefabs().Register("VariantApplyBase", std::move(prefab));
    const kb::scene::ScenePrefabHandle variantHandle = scene.Prefabs().RegisterVariant(
        "VariantApplyVariant",
        baseHandle,
        std::vector<kb::scene::ScenePrefabPropertyOverride>{
            kb::scene::ScenePrefabPropertyOverride{
                .nodeIndex = rootNode,
                .propertyPath = "name",
                .value = "Variant Apply Initial",
                .flag = kb::scene::ScenePrefabOverrideFlag::Name,
            },
        });
    kb::tests::Require(variantHandle.IsValid(), "Variant apply setup did not register variant");

    const kb::scene::ScenePrefabInstance variantInstance = scene.Prefabs().Instantiate(variantHandle);
    scene.Entities().SetName(variantInstance.ObjectAt(rootNode), "Variant Apply Changed");
    kb::tests::Require(scene.Prefabs().ApplyOverride(variantInstance.Handle(), rootNode, "name"), "Variant instance property apply failed");

    const kb::scene::ScenePrefab basePrefab = scene.Prefabs().Get(baseHandle);
    const kb::scene::ScenePrefab variantPrefab = scene.Prefabs().Get(variantHandle);
    kb::tests::Require(basePrefab.TryGetNode(rootNode)->name == "Variant Apply Base", "Applying a variant instance override mutated the base prefab");
    kb::tests::Require(variantPrefab.TryGetNode(rootNode)->name == "Variant Apply Changed", "Applying a variant instance override did not update the variant prefab");

    const kb::scene::ScenePrefabInstance baseInstance = scene.Prefabs().Instantiate(baseHandle);
    const kb::scene::ScenePrefabInstance refreshedVariantInstance = scene.Prefabs().Instantiate(variantHandle);
    kb::tests::Require(scene.Entities().Name(baseInstance.ObjectAt(rootNode)) == "Variant Apply Base", "Variant apply changed future base instances");
    kb::tests::Require(scene.Entities().Name(refreshedVariantInstance.ObjectAt(rootNode)) == "Variant Apply Changed", "Variant apply did not change future variant instances");
}

void RunPrefabBaseApplyRefreshesVariantInstancesPreservingLocalOverridesTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Base Refresh Variant Root" });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Base Refresh Variant Child",
        .parentNode = rootNode,
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 0.0F, 1.0F, 0.0F } },
    });
    const kb::scene::ScenePrefabHandle baseHandle = scene.Prefabs().Register("BaseRefreshVariantBase", std::move(prefab));
    const kb::scene::ScenePrefabHandle variantHandle = scene.Prefabs().RegisterVariant(
        "BaseRefreshVariant",
        baseHandle,
        std::vector<kb::scene::ScenePrefabPropertyOverride>{
            kb::scene::ScenePrefabPropertyOverride{
                .nodeIndex = childNode,
                .propertyPath = "name",
                .value = "Base Refresh Variant Override Child",
                .flag = kb::scene::ScenePrefabOverrideFlag::Name,
            },
        });
    kb::tests::Require(variantHandle.IsValid(), "Base refresh variant setup did not register variant");

    const kb::scene::ScenePrefabInstance baseSource = scene.Prefabs().Instantiate(baseHandle);
    const kb::scene::ScenePrefabInstance inheritedVariant = scene.Prefabs().Instantiate(variantHandle);
    const kb::scene::ScenePrefabInstance localOverrideVariant = scene.Prefabs().Instantiate(variantHandle);

    kb::scene::TransformComponent localTransform = scene.Transforms().Get(localOverrideVariant.ObjectAt(childNode));
    localTransform.localPosition = kb::scene::Vec3{ 0.0F, 99.0F, 0.0F };
    scene.Transforms().Set(localOverrideVariant.ObjectAt(childNode), localTransform);

    kb::scene::TransformComponent sourceTransform = scene.Transforms().Get(baseSource.ObjectAt(childNode));
    sourceTransform.localPosition = kb::scene::Vec3{ 0.0F, 8.0F, 0.0F };
    scene.Transforms().Set(baseSource.ObjectAt(childNode), sourceTransform);
    kb::tests::Require(scene.Prefabs().ApplyOverride(baseSource.Handle(), childNode, "transform.localPosition"), "Base prefab apply did not refresh variant descendants");

    const kb::scene::TransformComponent inheritedTransform = scene.Transforms().Get(inheritedVariant.ObjectAt(childNode));
    kb::tests::Require(kb::tests::NearlyEqual(inheritedTransform.localPosition.y, 8.0F), "Existing variant instance did not inherit refreshed base transform");
    kb::tests::Require(scene.Entities().Name(inheritedVariant.ObjectAt(childNode)) == "Base Refresh Variant Override Child", "Existing variant instance lost variant name override");

    const kb::scene::TransformComponent preservedTransform = scene.Transforms().Get(localOverrideVariant.ObjectAt(childNode));
    kb::tests::Require(kb::tests::NearlyEqual(preservedTransform.localPosition.y, 99.0F), "Existing variant instance lost its local transform override during base refresh");
    kb::tests::Require(scene.Entities().Name(localOverrideVariant.ObjectAt(childNode)) == "Base Refresh Variant Override Child", "Locally overridden variant instance lost non-local variant data");
}

void RunPrefabConnectionMetadataAndUnpackTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Connection Root" });
    const kb::scene::ScenePrefabHandle baseHandle = scene.Prefabs().Register("ConnectionBase", std::move(prefab));
    const kb::scene::ScenePrefabHandle variantHandle = scene.Prefabs().RegisterVariant(
        "ConnectionVariant",
        baseHandle,
        std::vector<kb::scene::ScenePrefabPropertyOverride>{
            kb::scene::ScenePrefabPropertyOverride{
                .nodeIndex = rootNode,
                .propertyPath = "name",
                .value = "Connection Variant Root",
                .flag = kb::scene::ScenePrefabOverrideFlag::Name,
            },
        });

    kb::tests::Require(scene.Prefabs().AssetType(baseHandle) == kb::scene::ScenePrefabAssetType::Template, "Base prefab should report template asset type");
    kb::tests::Require(scene.Prefabs().AssetType(variantHandle) == kb::scene::ScenePrefabAssetType::Variant, "Variant prefab should report variant asset type");
    kb::tests::Require(scene.Prefabs().AssetType(kb::scene::ScenePrefabHandle{}) == kb::scene::ScenePrefabAssetType::None, "Invalid prefab handle should report no asset type");

    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(variantHandle);
    kb::tests::Require(scene.Prefabs().InstanceStatus(instance.Handle()) == kb::scene::ScenePrefabInstanceStatus::Connected, "Fresh prefab instance should report connected status");
    kb::tests::Require(scene.Prefabs().SourcePrefab(instance.Handle()) == variantHandle, "Prefab instance source handle was not reported");
    kb::tests::Require(scene.Prefabs().SourcePrefab(instance.ObjectAt(rootNode)) == variantHandle, "Prefab object source handle was not reported");
    kb::tests::Require(scene.Prefabs().OriginalSourcePrefab(variantHandle) == baseHandle, "Variant original source should resolve to its base");
    kb::tests::Require(scene.Prefabs().OriginalSourcePrefab(instance.Handle()) == baseHandle, "Variant instance original source should resolve to its base");

    kb::tests::Require(scene.Prefabs().Unpack(instance.Handle()), "Prefab unpack failed");
    kb::tests::Require(scene.Prefabs().InstanceStatus(instance.Handle()) == kb::scene::ScenePrefabInstanceStatus::NotInstance, "Unpacked prefab should no longer report instance status");
    kb::tests::Require(!scene.Prefabs().RootInstance(instance.ObjectAt(rootNode)).IsValid(), "Unpacked prefab root should not keep a root instance link");
    kb::tests::Require(scene.Entities().IsAlive(instance.ObjectAt(rootNode)), "Unpack should keep the scene object alive");
}

void RunPrefabStaleHandleProtectionTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab firstPrefab;
    const std::uint32_t firstRootNode = firstPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Stale First Root" });
    static_cast<void>(firstRootNode);
    const kb::scene::ScenePrefabHandle stalePrefabHandle = scene.Prefabs().Register("StaleFirst", std::move(firstPrefab));
    kb::tests::Require(stalePrefabHandle.IsValid(), "Stale prefab setup did not register the first prefab");
    kb::tests::Require(scene.Prefabs().Unload(stalePrefabHandle), "Stale prefab setup did not unload the first prefab");
    kb::tests::Require(!scene.Prefabs().Contains(stalePrefabHandle), "Unloaded prefab handle should not remain contained");
    kb::tests::Require(scene.Prefabs().AssetType(stalePrefabHandle) == kb::scene::ScenePrefabAssetType::Missing, "Unloaded prefab handle should report missing asset status");

    kb::scene::ScenePrefab secondPrefab;
    const std::uint32_t secondRootNode = secondPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Stale Second Root" });
    static_cast<void>(secondRootNode);
    const kb::scene::ScenePrefabHandle replacementPrefabHandle = scene.Prefabs().Register("StaleSecond", std::move(secondPrefab));
    kb::tests::Require(replacementPrefabHandle.IsValid(), "Stale prefab setup did not register the replacement prefab");
    kb::tests::Require(replacementPrefabHandle != stalePrefabHandle, "Prefab registry reused an unloaded stale prefab handle");
    kb::tests::Require(!scene.Prefabs().Contains(stalePrefabHandle), "Stale prefab handle resolved after registering a new prefab");
    kb::tests::Require(scene.Prefabs().Contains(replacementPrefabHandle), "Replacement prefab handle was not retained");

    const kb::scene::ScenePrefabInstance firstInstance = scene.Prefabs().Instantiate(replacementPrefabHandle);
    const kb::scene::ScenePrefabInstanceHandle staleInstanceHandle = firstInstance.Handle();
    kb::tests::Require(staleInstanceHandle.IsValid(), "Stale instance setup did not create a tracked prefab instance");
    kb::tests::Require(scene.Prefabs().Unpack(staleInstanceHandle), "Stale instance setup did not unpack the first instance");
    kb::tests::Require(!scene.Prefabs().IsInstance(staleInstanceHandle), "Unpacked prefab instance handle should not remain active");
    kb::tests::Require(scene.Prefabs().InstanceStatus(staleInstanceHandle) == kb::scene::ScenePrefabInstanceStatus::NotInstance, "Unpacked prefab instance handle should report not-instance status");

    const kb::scene::ScenePrefabInstance replacementInstance = scene.Prefabs().Instantiate(replacementPrefabHandle);
    kb::tests::Require(replacementInstance.Handle().IsValid(), "Stale instance setup did not create a replacement instance");
    kb::tests::Require(replacementInstance.Handle() != staleInstanceHandle, "Prefab instance registry reused an unpacked stale instance handle");
    kb::tests::Require(!scene.Prefabs().IsInstance(staleInstanceHandle), "Stale instance handle resolved after creating a new instance");
    kb::tests::Require(scene.Prefabs().IsInstance(replacementInstance.Handle()), "Replacement prefab instance handle was not retained");
}

void RunPrefabMissingSourceUnloadUnpackReconnectTest() {
    const std::filesystem::path prefabPath = std::filesystem::temp_directory_path() / "21kb_engine_prefab_missing_source_reconnect.kbprefab";
    std::error_code removeError;
    std::filesystem::remove(prefabPath, removeError);

    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Missing Source Root" });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Missing Source Child",
        .parentNode = rootNode,
    });
    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("MissingSourcePrefab", std::move(prefab));
    kb::tests::Require(prefabHandle.IsValid(), "Missing source prefab setup failed");
    kb::tests::Require(scene.Prefabs().Save(prefabHandle, prefabPath), "Missing source prefab asset save failed");

    const kb::scene::ScenePrefabInstance unpackInstance = scene.Prefabs().Instantiate(prefabHandle);
    const kb::scene::ScenePrefabInstance reconnectInstance = scene.Prefabs().Instantiate(prefabHandle);
    kb::tests::Require(scene.Prefabs().InstanceStatus(unpackInstance.Handle()) == kb::scene::ScenePrefabInstanceStatus::Connected, "Missing source setup instance was not connected");
    kb::tests::Require(scene.Prefabs().InstanceStatus(reconnectInstance.Handle()) == kb::scene::ScenePrefabInstanceStatus::Connected, "Missing source reconnect setup instance was not connected");

    kb::tests::Require(scene.Prefabs().Unload(prefabHandle), "Prefab unload failed");
    kb::tests::Require(scene.Prefabs().AssetType(prefabHandle) == kb::scene::ScenePrefabAssetType::Missing, "Unloaded prefab should report missing asset type");
    kb::tests::Require(scene.Prefabs().IsInstance(unpackInstance.Handle()), "Missing source unload removed instance tracking");
    kb::tests::Require(scene.Prefabs().InstanceStatus(unpackInstance.Handle()) == kb::scene::ScenePrefabInstanceStatus::MissingAsset, "Unloaded prefab instance should report missing asset status");
    kb::tests::Require(scene.Prefabs().InstanceStatus(reconnectInstance.Handle()) == kb::scene::ScenePrefabInstanceStatus::MissingAsset, "Reconnect candidate should report missing asset status");

    kb::tests::Require(scene.Prefabs().Unpack(unpackInstance.Handle()), "Missing source unpack failed");
    kb::tests::Require(scene.Prefabs().InstanceStatus(unpackInstance.Handle()) == kb::scene::ScenePrefabInstanceStatus::NotInstance, "Unpacked missing source instance should not remain an instance");
    kb::tests::Require(scene.Entities().IsAlive(unpackInstance.ObjectAt(rootNode)) && scene.Entities().IsAlive(unpackInstance.ObjectAt(childNode)), "Missing source unpack destroyed scene objects");

    const kb::scene::ScenePrefabHandle loadedHandle = scene.Prefabs().Load(prefabPath);
    kb::tests::Require(loadedHandle.IsValid(), "Missing source reload failed");
    kb::tests::Require(scene.Prefabs().Reconnect(reconnectInstance.Handle(), loadedHandle), "Missing source reconnect failed");
    kb::tests::Require(scene.Prefabs().InstanceStatus(reconnectInstance.Handle()) == kb::scene::ScenePrefabInstanceStatus::Connected, "Reconnected prefab instance did not report connected status");
    kb::tests::Require(scene.Prefabs().SourcePrefab(reconnectInstance.Handle()) == loadedHandle, "Reconnected prefab instance did not update its source handle");
    kb::tests::Require(scene.Prefabs().RootInstance(reconnectInstance.ObjectAt(rootNode)) == reconnectInstance.Handle(), "Reconnected prefab root lost registry mapping");
    kb::tests::Require(scene.Entities().Name(reconnectInstance.ObjectAt(childNode)) == "Missing Source Child", "Reconnected prefab child did not refresh from loaded asset");

    std::filesystem::remove(prefabPath, removeError);
}

void RunPrefabUnpackNestedVariantModesTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab parentPrefab;
    const std::uint32_t parentRoot = parentPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Unpack Parent Root" });
    const kb::scene::ScenePrefabHandle parentHandle = scene.Prefabs().Register("UnpackParentPrefab", std::move(parentPrefab));

    kb::scene::ScenePrefab childBasePrefab;
    const std::uint32_t childRoot = childBasePrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Unpack Child Base" });
    const kb::scene::ScenePrefabHandle childBaseHandle = scene.Prefabs().Register("UnpackChildBase", std::move(childBasePrefab));
    const kb::scene::ScenePrefabHandle childVariantHandle = scene.Prefabs().RegisterVariant(
        "UnpackChildVariant",
        childBaseHandle,
        std::vector<kb::scene::ScenePrefabPropertyOverride>{
            kb::scene::ScenePrefabPropertyOverride{
                .nodeIndex = childRoot,
                .propertyPath = "name",
                .value = "Unpack Child Variant",
                .flag = kb::scene::ScenePrefabOverrideFlag::Name,
            },
        });
    kb::tests::Require(parentHandle.IsValid() && childVariantHandle.IsValid(), "Nested variant unpack setup failed");

    const kb::scene::ScenePrefabInstance rootOnlyParent = scene.Prefabs().Instantiate(parentHandle);
    const kb::scene::ScenePrefabInstance rootOnlyNested = scene.Prefabs().Instantiate(
        childVariantHandle,
        kb::scene::ScenePrefabInstantiationSettings{ .parent = rootOnlyParent.ObjectAt(parentRoot) });
    kb::tests::Require(scene.Prefabs().RootInstance(rootOnlyParent.ObjectAt(parentRoot)) == rootOnlyParent.Handle(), "Root-only unpack setup lost parent instance");
    kb::tests::Require(scene.Prefabs().RootInstance(rootOnlyNested.ObjectAt(childRoot)) == rootOnlyNested.Handle(), "Root-only unpack setup lost nested variant instance");

    kb::tests::Require(scene.Prefabs().Unpack(rootOnlyParent.Handle(), kb::scene::ScenePrefabUnpackMode::RootOnly), "Root-only unpack of parent prefab failed");
    kb::tests::Require(!scene.Prefabs().IsInstance(rootOnlyParent.Handle()), "Root-only unpack should remove only the parent instance link");
    kb::tests::Require(scene.Prefabs().IsInstance(rootOnlyNested.Handle()), "Root-only unpack removed a nested variant instance link");
    kb::tests::Require(scene.Prefabs().RootInstance(rootOnlyNested.ObjectAt(childRoot)) == rootOnlyNested.Handle(), "Root-only unpack did not preserve nested variant root link");
    kb::tests::Require(scene.Entities().IsAlive(rootOnlyParent.ObjectAt(parentRoot)) && scene.Entities().IsAlive(rootOnlyNested.ObjectAt(childRoot)), "Root-only unpack destroyed scene objects");

    const kb::scene::ScenePrefabInstance completeParent = scene.Prefabs().Instantiate(parentHandle);
    const kb::scene::ScenePrefabInstance completeNested = scene.Prefabs().Instantiate(
        childVariantHandle,
        kb::scene::ScenePrefabInstantiationSettings{ .parent = completeParent.ObjectAt(parentRoot) });

    kb::tests::Require(scene.Prefabs().Unpack(completeParent.Handle(), kb::scene::ScenePrefabUnpackMode::Complete), "Complete unpack of parent prefab failed");
    kb::tests::Require(!scene.Prefabs().IsInstance(completeParent.Handle()), "Complete unpack did not remove the parent instance link");
    kb::tests::Require(!scene.Prefabs().IsInstance(completeNested.Handle()), "Complete unpack did not remove the nested variant instance link");
    kb::tests::Require(!scene.Prefabs().RootInstance(completeParent.ObjectAt(parentRoot)).IsValid(), "Complete unpack left parent root link");
    kb::tests::Require(!scene.Prefabs().RootInstance(completeNested.ObjectAt(childRoot)).IsValid(), "Complete unpack left nested variant root link");
    kb::tests::Require(scene.Entities().IsAlive(completeParent.ObjectAt(parentRoot)) && scene.Entities().IsAlive(completeNested.ObjectAt(childRoot)), "Complete unpack destroyed scene objects");
}

void RunPrefabApplyRefreshesExistingInstancesTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Refresh Root" });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Refresh Child",
        .parentNode = rootNode,
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 0.0F, 1.0F, 0.0F } },
    });
    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("RefreshPrefab", std::move(prefab));

    const kb::scene::ScenePrefabInstance sourceInstance = scene.Prefabs().Instantiate(prefabHandle);
    const kb::scene::ScenePrefabInstance localOverrideInstance = scene.Prefabs().Instantiate(prefabHandle);
    const kb::scene::ScenePrefabInstance inheritedInstance = scene.Prefabs().Instantiate(prefabHandle);

    kb::scene::TransformComponent localTransform = scene.Transforms().Get(localOverrideInstance.ObjectAt(childNode));
    localTransform.localPosition = kb::scene::Vec3{ 0.0F, 99.0F, 0.0F };
    scene.Transforms().Set(localOverrideInstance.ObjectAt(childNode), localTransform);

    kb::scene::TransformComponent sourceTransform = scene.Transforms().Get(sourceInstance.ObjectAt(childNode));
    sourceTransform.localPosition = kb::scene::Vec3{ 0.0F, 8.0F, 0.0F };
    scene.Transforms().Set(sourceInstance.ObjectAt(childNode), sourceTransform);
    scene.Entities().SetName(sourceInstance.ObjectAt(childNode), "Refresh Child Applied");

    kb::tests::Require(scene.Prefabs().ApplyOverride(sourceInstance.Handle(), childNode, "name"), "Prefab name apply for refresh failed");
    kb::tests::Require(scene.Prefabs().ApplyOverride(sourceInstance.Handle(), childNode, "transform.localPosition"), "Prefab transform apply for refresh failed");

    kb::tests::Require(scene.Entities().Name(inheritedInstance.ObjectAt(childNode)) == "Refresh Child Applied", "Existing instance did not receive applied prefab name");
    const kb::scene::TransformComponent inheritedTransform = scene.Transforms().Get(inheritedInstance.ObjectAt(childNode));
    kb::tests::Require(kb::tests::NearlyEqual(inheritedTransform.localPosition.y, 8.0F), "Existing instance did not receive applied prefab transform");

    kb::tests::Require(scene.Entities().Name(localOverrideInstance.ObjectAt(childNode)) == "Refresh Child Applied", "Existing overridden instance did not receive non-overridden prefab name");
    const kb::scene::TransformComponent preservedTransform = scene.Transforms().Get(localOverrideInstance.ObjectAt(childNode));
    kb::tests::Require(kb::tests::NearlyEqual(preservedTransform.localPosition.y, 99.0F), "Existing instance local transform override was not preserved");
}

void RunPrefabApplyAddedChildRefreshesExistingInstancesTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Child Refresh Root" });
    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("ChildRefreshPrefab", std::move(prefab));

    const kb::scene::ScenePrefabInstance sourceInstance = scene.Prefabs().Instantiate(prefabHandle);
    const kb::scene::ScenePrefabInstance inheritedInstance = scene.Prefabs().Instantiate(prefabHandle);
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Applied Child Refresh",
        .parent = sourceInstance.ObjectAt(rootNode),
    }));

    kb::tests::Require(scene.Prefabs().ApplyOverrides(sourceInstance.Handle()), "Prefab added child apply for refresh failed");
    kb::tests::Require(inheritedInstance.ObjectCount() == 1, "ScenePrefabInstance value should remain an immutable spawn result");
    const kb::scene::ScenePrefab refreshedPrefab = scene.Prefabs().Get(prefabHandle);
    const kb::scene::ScenePrefabNodeDesc* addedPrefabNode = refreshedPrefab.TryGetNode(1U);
    kb::tests::Require(addedPrefabNode != nullptr && addedPrefabNode->stableId != kb::scene::ScenePrefabNodeDesc::InvalidStableId, "Applied child prefab node did not receive a stable id");

    std::uint32_t addedNodeIndex = 99;
    std::uint64_t rootNodeId = 0;
    const kb::scene::ScenePrefabInstanceHandle refreshedHandle = scene.Prefabs().ContainingInstance(inheritedInstance.ObjectAt(rootNode), addedNodeIndex);
    kb::tests::Require(refreshedHandle == inheritedInstance.Handle() && addedNodeIndex == rootNode, "Refreshed prefab root should keep its tracked mapping");
    kb::tests::Require(scene.Prefabs().ContainingInstance(inheritedInstance.ObjectAt(rootNode), addedNodeIndex, rootNodeId) == inheritedInstance.Handle(), "Refreshed prefab root should keep its stable node mapping");
    kb::tests::Require(rootNodeId == refreshedPrefab.TryGetNode(rootNode)->stableId, "Refreshed prefab root stable node mapping is wrong");

    bool foundAppliedChild = false;
    for (const kb::scene::SceneEntity child : scene.Hierarchy().ChildEntities(inheritedInstance.ObjectAt(rootNode).Entity())) {
        if (scene.Entities().Name(child) == "Applied Child Refresh") {
            std::uint32_t childNodeIndex = 99;
            std::uint64_t childNodeId = 0;
            foundAppliedChild = scene.Prefabs().ContainingInstance(child, childNodeIndex, childNodeId) == inheritedInstance.Handle()
                && childNodeIndex == 1
                && childNodeId == addedPrefabNode->stableId;
        }
    }
    kb::tests::Require(foundAppliedChild, "Existing instance did not receive the applied tracked child node stable mapping");
}

void RunPrefabAddedRemovedMissingNodesStableAfterRefreshAndSaveTest() {
    const std::filesystem::path prefabPath = std::filesystem::temp_directory_path() / "21kb_engine_prefab_node_identity_refresh_save.kbprefab";
    std::error_code removeError;
    std::filesystem::remove(prefabPath, removeError);

    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    constexpr std::uint64_t kRootStableId = 101U;
    constexpr std::uint64_t kRemovedStableId = 102U;
    constexpr std::uint64_t kTargetStableId = 103U;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = kRootStableId,
        .name = "Stable Refresh Root",
    });
    const std::uint32_t removedNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = kRemovedStableId,
        .name = "Stable Refresh Removed",
        .parentNode = rootNode,
    });
    const std::uint32_t targetNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = kTargetStableId,
        .name = "Stable Refresh Target",
        .parentNode = rootNode,
    });

    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("StableRefreshSavePrefab", std::move(prefab));
    kb::tests::Require(prefabHandle.IsValid(), "Stable refresh/save prefab did not register");
    kb::tests::Require(scene.Prefabs().Save(prefabHandle, prefabPath), "Stable refresh/save initial asset save failed");

    const kb::scene::ScenePrefabInstance sourceInstance = scene.Prefabs().Instantiate(prefabHandle);
    const kb::scene::ScenePrefabInstance inheritedInstance = scene.Prefabs().Instantiate(prefabHandle);
    const kb::scene::ScenePrefabInstance missingInstance = scene.Prefabs().Instantiate(prefabHandle);
    const kb::scene::SceneObject inheritedRemovedObject = inheritedInstance.ObjectAt(removedNode);
    const kb::scene::SceneObject inheritedTargetObject = inheritedInstance.ObjectAt(targetNode);

    scene.Entities().Destroy(missingInstance.ObjectAt(targetNode));
    scene.Entities().Destroy(sourceInstance.ObjectAt(removedNode));
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Stable Refresh Added",
        .parent = sourceInstance.ObjectAt(rootNode),
    }));

    kb::tests::Require(scene.Prefabs().ApplyOverrides(sourceInstance.Handle(), prefabPath), "Stable refresh/save apply-to-asset failed");

    const kb::scene::ScenePrefab refreshedPrefab = scene.Prefabs().Get(prefabHandle);
    kb::tests::Require(refreshedPrefab.NodeCount() == 3U, "Stable refresh/save prefab should contain root, target and added nodes");
    kb::tests::Require(refreshedPrefab.FindNodeIndexByStableId(kRemovedStableId) == kb::scene::ScenePrefabNodeDesc::NoParent, "Removed prefab node stable id survived in the refreshed prefab");
    const std::uint32_t refreshedTargetNode = refreshedPrefab.FindNodeIndexByStableId(kTargetStableId);
    kb::tests::Require(refreshedTargetNode != kb::scene::ScenePrefabNodeDesc::NoParent, "Target prefab node lost its stable id after refresh");
    kb::tests::Require(refreshedPrefab.TryGetNode(refreshedTargetNode)->name == "Stable Refresh Target", "Target prefab node changed after removed-node refresh");

    const kb::scene::SceneObject refreshedInheritedTarget = inheritedTargetObject;
    std::uint32_t mappedTargetNode = 0U;
    std::uint64_t mappedTargetNodeId = 0U;
    kb::tests::Require(!scene.Entities().IsAlive(inheritedRemovedObject), "Removed prefab node left an old inherited object alive after refresh");
    kb::tests::Require(scene.Prefabs().ContainingInstance(refreshedInheritedTarget, mappedTargetNode, mappedTargetNodeId) == inheritedInstance.Handle(), "Target object lost prefab instance mapping after removed-node refresh");
    kb::tests::Require(mappedTargetNode == refreshedTargetNode && mappedTargetNodeId == kTargetStableId, "Target object was remapped by index instead of stable id");
    kb::tests::Require(scene.Entities().Name(refreshedInheritedTarget) == "Stable Refresh Target", "Target object received removed-node state after refresh");

    const kb::scene::ScenePrefabOverrideReport missingReport = scene.Prefabs().Overrides(missingInstance.Handle());
    bool foundMissingTarget = false;
    for (const kb::scene::ScenePrefabNodeOverride& node : missingReport.nodes) {
        foundMissingTarget = foundMissingTarget
            || (node.nodeId == kTargetStableId && kb::scene::HasPrefabOverride(node.flags, kb::scene::ScenePrefabOverrideFlag::MissingObject));
    }
    kb::tests::Require(foundMissingTarget, "Missing object override did not survive stable-id refresh");

    bool foundAddedMapping = false;
    std::uint64_t addedStableId = kb::scene::ScenePrefabNodeDesc::InvalidStableId;
    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(refreshedPrefab.NodeCount()); ++nodeIndex) {
        const kb::scene::ScenePrefabNodeDesc* node = refreshedPrefab.TryGetNode(nodeIndex);
        if (node != nullptr && node->name == "Stable Refresh Added") {
            addedStableId = node->stableId;
        }
    }
    kb::tests::Require(addedStableId != kb::scene::ScenePrefabNodeDesc::InvalidStableId, "Added prefab node did not receive a stable id");
    for (const kb::scene::SceneEntity child : scene.Hierarchy().ChildEntities(inheritedInstance.ObjectAt(rootNode).Entity())) {
        if (scene.Entities().Name(child) != "Stable Refresh Added") {
            continue;
        }

        std::uint32_t addedNodeIndex = 0U;
        std::uint64_t addedNodeId = 0U;
        foundAddedMapping = scene.Prefabs().ContainingInstance(child, addedNodeIndex, addedNodeId) == inheritedInstance.Handle()
            && addedNodeId == addedStableId;
    }
    kb::tests::Require(foundAddedMapping, "Added prefab node did not receive stable mapping in refreshed instances");

    kb::scene::Scene loadedScene;
    const kb::scene::ScenePrefabHandle loadedHandle = loadedScene.Prefabs().Load(prefabPath);
    kb::tests::Require(loadedHandle.IsValid(), "Stable refresh/save prefab asset did not reload");
    const kb::scene::ScenePrefab loadedPrefab = loadedScene.Prefabs().Get(loadedHandle);
    kb::tests::Require(loadedPrefab.FindNodeIndexByStableId(kRemovedStableId) == kb::scene::ScenePrefabNodeDesc::NoParent, "Removed prefab node stable id was persisted after save");
    kb::tests::Require(loadedPrefab.FindNodeIndexByStableId(kTargetStableId) != kb::scene::ScenePrefabNodeDesc::NoParent, "Target prefab stable id was not persisted after save");
    const kb::scene::ScenePrefabInstance loadedInstance = loadedScene.Prefabs().Instantiate(loadedHandle);
    kb::tests::Require(loadedInstance.ObjectCount() == 3U, "Stable refresh/save loaded instance has invalid node count");
    bool loadedAdded = false;
    for (const kb::scene::SceneObject object : loadedInstance.Objects()) {
        loadedAdded = loadedAdded || loadedScene.Entities().Name(object) == "Stable Refresh Added";
    }
    kb::tests::Require(loadedAdded, "Added prefab node was not persisted after save");

    std::filesystem::remove(prefabPath, removeError);
}

void RunPrefabRefreshLargeInstanceSetTest() {
    kb::scene::Scene scene;
    constexpr std::size_t kInstanceCount = 1024U;
    constexpr std::size_t kUnrelatedInstanceCount = 128U;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Scale Refresh Root" });
    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("ScaleRefreshPrefab", std::move(prefab));

    kb::scene::ScenePrefab unrelatedPrefab;
    static_cast<void>(unrelatedPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Unrelated Refresh Root" }));
    const kb::scene::ScenePrefabHandle unrelatedHandle = scene.Prefabs().Register("UnrelatedRefreshPrefab", std::move(unrelatedPrefab));

    const std::vector<kb::scene::ScenePrefabInstance> instances = scene.Prefabs().InstantiateMany(prefabHandle, kInstanceCount);
    const std::vector<kb::scene::ScenePrefabInstance> unrelatedInstances = scene.Prefabs().InstantiateMany(unrelatedHandle, kUnrelatedInstanceCount);
    kb::tests::Require(instances.size() == kInstanceCount, "Large prefab refresh setup lost target instances");
    kb::tests::Require(unrelatedInstances.size() == kUnrelatedInstanceCount, "Large prefab refresh setup lost unrelated instances");

    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Scale Refresh Added Child",
        .parent = instances.front().ObjectAt(rootNode),
    }));
    kb::tests::Require(scene.Prefabs().ApplyOverrides(instances.front().Handle()), "Large prefab refresh apply failed");

    const std::size_t refreshedCount = scene.Prefabs().RefreshInstances(prefabHandle);
    kb::tests::Require(refreshedCount == kInstanceCount, "Large prefab refresh did not use the target instance set");

    const kb::scene::ScenePrefabInstance& sample = instances[kInstanceCount - 1U];
    kb::tests::Require(scene.Prefabs().RootInstance(sample.ObjectAt(rootNode)) == sample.Handle(), "Large prefab refresh root index is stale");

    bool foundAddedChild = false;
    for (const kb::scene::SceneEntity child : scene.Hierarchy().ChildEntities(sample.ObjectAt(rootNode).Entity())) {
        if (scene.Entities().Name(child) != "Scale Refresh Added Child") {
            continue;
        }

        std::uint32_t childNodeIndex = 0U;
        foundAddedChild = scene.Prefabs().ContainingInstance(child, childNodeIndex) == sample.Handle() && childNodeIndex == 1U;
    }
    kb::tests::Require(foundAddedChild, "Large prefab refresh did not index the refreshed child object");
    kb::tests::Require(scene.Prefabs().RootInstance(unrelatedInstances.front().ObjectAt(0U)) == unrelatedInstances.front().Handle(), "Large prefab refresh disturbed unrelated prefab instance indexes");
}

void RunPrefabApplyOverrideToAssetTest() {
    const std::filesystem::path prefabPath = std::filesystem::temp_directory_path() / "21kb_engine_prefab_apply_override.kbprefab";
    std::error_code removeError;
    std::filesystem::remove(prefabPath, removeError);

    kb::scene::Scene scene;
    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Asset Apply Root" });
    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("AssetApplyPrefab", std::move(prefab));
    kb::tests::Require(scene.Prefabs().Save(prefabHandle, prefabPath), "Initial prefab asset save failed");

    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(prefabHandle);
    scene.Entities().SetName(instance.ObjectAt(rootNode), "Applied Asset Root");
    kb::tests::Require(scene.Prefabs().ApplyOverride(instance.Handle(), rootNode, "name", prefabPath), "Prefab property apply-to-asset failed");

    kb::scene::Scene loadedScene;
    const kb::scene::ScenePrefabHandle loadedHandle = loadedScene.Prefabs().Load(prefabPath);
    kb::tests::Require(loadedHandle.IsValid(), "Applied prefab asset could not be loaded");
    const kb::scene::ScenePrefabInstance loadedInstance = loadedScene.Prefabs().Instantiate(loadedHandle);
    kb::tests::Require(loadedScene.Entities().Name(loadedInstance.ObjectAt(rootNode)) == "Applied Asset Root", "Prefab apply-to-asset did not persist the override");

    std::filesystem::remove(prefabPath, removeError);
}

void RunPrefabAssetLoadMigratesMissingNodeStableIdsTest() {
    const std::filesystem::path prefabPath = std::filesystem::temp_directory_path() / "21kb_engine_prefab_missing_node_ids.kbprefab";
    std::error_code removeError;
    std::filesystem::remove(prefabPath, removeError);

    kb::scene::Scene scene;
    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Migration Root" });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Migration Child",
        .parentNode = rootNode,
    });
    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("MigrationPrefab", std::move(prefab));
    kb::tests::Require(scene.Prefabs().Save(prefabHandle, prefabPath), "Migration prefab asset save failed");

    const std::string currentAsset = ReadTextFile(prefabPath);
    const std::string legacyAsset = RemoveNodeStableIdFields(currentAsset);
    WriteTextFile(prefabPath, legacyAsset);

    kb::scene::Scene loadedScene;
    const kb::scene::ScenePrefabHandle loadedHandle = loadedScene.Prefabs().Load(prefabPath);
    kb::tests::Require(loadedHandle.IsValid(), "Prefab asset without node stable ids did not load");

    const kb::scene::ScenePrefab loadedPrefab = loadedScene.Prefabs().Get(loadedHandle);
    const kb::scene::ScenePrefabNodeDesc* root = loadedPrefab.TryGetNode(rootNode);
    const kb::scene::ScenePrefabNodeDesc* child = loadedPrefab.TryGetNode(childNode);
    kb::tests::Require(root != nullptr && child != nullptr, "Migrated prefab asset did not preserve nodes");
    kb::tests::Require(root->stableId != kb::scene::ScenePrefabNodeDesc::InvalidStableId, "Migrated prefab root did not receive a stable id");
    kb::tests::Require(child->stableId != kb::scene::ScenePrefabNodeDesc::InvalidStableId, "Migrated prefab child did not receive a stable id");
    kb::tests::Require(root->stableId != child->stableId, "Migrated prefab node stable ids are not unique");

    const std::string migratedAsset = ReadTextFile(prefabPath);
    kb::tests::Require(migratedAsset.find("\nid=") != std::string::npos, "Migrated prefab asset was not written back with node stable ids");

    std::filesystem::remove(prefabPath, removeError);
}

void RunNestedPrefabCompositionTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab innerPrefab;
    const std::uint32_t innerRoot = innerPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Inner Root" });
    const std::uint32_t innerChild = innerPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Inner Child",
        .parentNode = innerRoot,
        .visibility = kb::scene::VisibilityComponent{ .visible = true },
    });
    const kb::scene::ScenePrefabHandle innerHandle = scene.Prefabs().Register("InnerPrefab", std::move(innerPrefab));
    const std::string innerGuid = scene.Prefabs().Guid(innerHandle);
    kb::tests::Require(!innerGuid.empty(), "Nested prefab inner asset did not receive a guid");

    kb::scene::ScenePrefab outerPrefab;
    const std::uint32_t outerRoot = outerPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Outer Root" });
    static_cast<void>(outerPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Nested Placeholder",
        .nestedPrefabGuid = innerGuid,
        .parentNode = outerRoot,
    }));
    const kb::scene::ScenePrefabHandle outerHandle = scene.Prefabs().Register("OuterPrefab", std::move(outerPrefab));

    const kb::scene::ScenePrefabInstance first = scene.Prefabs().Instantiate(outerHandle);
    kb::tests::Require(first.ObjectCount() == 3, "Nested prefab composition did not expand inner prefab nodes");
    kb::tests::Require(scene.Entities().Name(first.ObjectAt(1)) == "Inner Root", "Nested prefab composition did not use inner root data");
    kb::tests::Require(scene.Entities().Name(first.ObjectAt(2)) == "Inner Child", "Nested prefab composition did not use inner child data");
    kb::tests::Require(scene.Hierarchy().Parent(first.ObjectAt(1).Entity()) == first.ObjectAt(outerRoot).Entity(), "Nested prefab root was not parented under outer node");
    kb::tests::Require(scene.Hierarchy().Parent(first.ObjectAt(2).Entity()) == first.ObjectAt(1).Entity(), "Nested prefab child hierarchy was not preserved");

    const kb::scene::ScenePrefabInstance innerInstance = scene.Prefabs().Instantiate(innerHandle);
    scene.Components().Visibility().Set(innerInstance.ObjectAt(innerChild).Entity(), kb::scene::VisibilityComponent{ .visible = false });
    kb::tests::Require(scene.Prefabs().ApplyOverride(innerInstance.Handle(), innerChild, "visibility.visible"), "Inner prefab baseline update failed");
    const kb::scene::ScenePrefabInstance refreshedOuter = scene.Prefabs().Instantiate(outerHandle);
    kb::tests::Require(!scene.Components().Visibility().Get(refreshedOuter.ObjectAt(2).Entity()).visible, "Nested prefab composition did not resolve refreshed inner prefab data");
}

void RunNestedPrefabCaptureAndRefreshTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab innerPrefab;
    const std::uint32_t innerRoot = innerPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Captured Inner Root" });
    const std::uint32_t innerChild = innerPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Captured Inner Child",
        .parentNode = innerRoot,
        .visibility = kb::scene::VisibilityComponent{ .visible = true },
    });
    const kb::scene::ScenePrefabHandle innerHandle = scene.Prefabs().Register("CapturedInnerPrefab", std::move(innerPrefab));

    kb::scene::SceneObject outerRoot = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Captured Outer Root" });
    const kb::scene::ScenePrefabInstance nestedInstance = scene.Prefabs().Instantiate(
        innerHandle,
        kb::scene::ScenePrefabInstantiationSettings{ .parent = outerRoot });
    scene.Entities().SetName(nestedInstance.ObjectAt(innerChild), "Captured Inner Child Override");

    const kb::scene::ScenePrefabHandle outerHandle = scene.Prefabs().CaptureRegistered(outerRoot, "CapturedOuterPrefab");
    const kb::scene::ScenePrefab capturedOuter = scene.Prefabs().Get(outerHandle);
    kb::tests::Require(capturedOuter.NodeCount() == 3, "Captured outer prefab should preserve nested instance snapshot nodes");
    kb::tests::Require(!capturedOuter.Nodes()[1].nestedPrefabGuid.empty(), "Captured nested prefab root did not retain inner prefab guid");
    kb::tests::Require(!capturedOuter.Nodes()[1].nestedPrefabOverrides.empty(), "Captured nested prefab override deltas were not stored");

    const kb::scene::ScenePrefabInstance outerInstance = scene.Prefabs().Instantiate(outerHandle);
    kb::tests::Require(outerInstance.ObjectCount() == 3, "Captured nested prefab did not compose to the expected hierarchy");
    kb::tests::Require(scene.Entities().Name(outerInstance.ObjectAt(2)) == "Captured Inner Child Override", "Nested prefab composition did not apply stored child override");
}

void RunPrefabPrivateSceneApplyPreservesMainSceneOverridesTest() {
    kb::scene::Scene scene;
    constexpr std::size_t kInstanceCount = 10'000U;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Private Root" });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Private Child",
        .parentNode = rootNode,
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 0.0F, 1.0F, 0.0F } },
    });
    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("PrivateEditPrefab", std::move(prefab));
    kb::tests::Require(prefabHandle.IsValid(), "Private prefab edit setup failed to register source prefab");

    const std::vector<kb::scene::ScenePrefabInstance> instances = scene.Prefabs().InstantiateMany(prefabHandle, kInstanceCount);
    kb::tests::Require(instances.size() == kInstanceCount, "Private prefab edit setup did not create the full main-scene instance set");

    kb::scene::TransformComponent localOverride = scene.Transforms().Get(instances[123U].ObjectAt(childNode));
    localOverride.localPosition = kb::scene::Vec3{ 0.0F, 99.0F, 0.0F };
    scene.Transforms().Set(instances[123U].ObjectAt(childNode), localOverride);

    kb::scene::ScenePrefabPrivateScene privateScene = scene.Prefabs().OpenPrivateScene(prefabHandle);
    kb::tests::Require(privateScene.IsValid(), "Private prefab scene did not open");
    kb::tests::Require(privateScene.EditScene().IsPrefabPrivate(), "Private prefab scene did not use prefab-private mode");
    kb::tests::Require(ThrowsPrivateSceneRuntimeRegistration(privateScene.EditScene()), "Private prefab scene accepted a runtime scene system");
    kb::tests::Require(ThrowsPrivateSceneEcsRuntimeRegistration(privateScene.EditScene()), "Private prefab scene accepted an ECS runtime system");
    kb::tests::Require(!privateScene.EditScene().Runtime().Update(0.016F), "Private prefab scene advanced runtime world progress");
    kb::tests::Require(privateScene.EditScene().Runtime().LastFixedStepCount() == 0U, "Private prefab scene executed fixed runtime steps");
    kb::tests::Require(privateScene.SourcePrefab() == prefabHandle, "Private prefab scene lost source prefab identity");
    kb::tests::Require(privateScene.ObjectCount() == 2U, "Private prefab scene did not instantiate the source prefab");

    privateScene.EditScene().Entities().SetName(privateScene.ObjectAt(childNode), "Private Child Applied");
    kb::scene::TransformComponent privateTransform = privateScene.EditScene().Transforms().Get(privateScene.ObjectAt(childNode));
    privateTransform.localPosition = kb::scene::Vec3{ 0.0F, 7.0F, 0.0F };
    privateScene.EditScene().Transforms().Set(privateScene.ObjectAt(childNode), privateTransform);

    kb::tests::Require(!privateScene.Overrides().Empty(), "Private prefab scene did not track edit overrides");
    kb::tests::Require(scene.Prefabs().Overrides(instances[123U].Handle()).properties.size() == 1U, "Main scene override tracking changed before private apply");
    kb::tests::Require(privateScene.Apply(), "Private prefab scene apply failed");

    kb::tests::Require(scene.Entities().Name(instances.front().ObjectAt(childNode)) == "Private Child Applied", "Private prefab apply did not refresh inherited instance name");
    const kb::scene::TransformComponent inheritedTransform = scene.Transforms().Get(instances.front().ObjectAt(childNode));
    kb::tests::Require(kb::tests::NearlyEqual(inheritedTransform.localPosition.y, 7.0F), "Private prefab apply did not refresh inherited transform");

    const kb::scene::TransformComponent preservedTransform = scene.Transforms().Get(instances[123U].ObjectAt(childNode));
    kb::tests::Require(kb::tests::NearlyEqual(preservedTransform.localPosition.y, 99.0F), "Private prefab apply lost a local main-scene transform override");
    kb::tests::Require(scene.Entities().Name(instances[123U].ObjectAt(childNode)) == "Private Child Applied", "Private prefab apply did not update non-overridden properties on a locally overridden instance");
    kb::tests::Require(scene.Prefabs().RootInstance(instances.back().ObjectAt(rootNode)) == instances.back().Handle(), "Private prefab apply disturbed main-scene instance source tracking");
    kb::tests::Require(scene.Prefabs().Overrides(instances[123U].Handle()).properties.size() == 1U, "Private prefab apply changed main-scene override tracking scope");
}

void RunPrefabStableNodeIdentityOverridesTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Stable Root" });
    const std::uint32_t firstChildNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Stable First Child",
        .parentNode = rootNode,
    });
    const std::uint32_t targetChildNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Stable Target Child",
        .parentNode = rootNode,
    });

    const std::uint64_t targetNodeId = prefab.TryGetNode(targetChildNode)->stableId;
    const kb::scene::ScenePrefabHandle baseHandle = scene.Prefabs().Register("StableNodeBase", std::move(prefab));
    kb::tests::Require(baseHandle.IsValid(), "Stable node identity base prefab did not register");

    const kb::scene::ScenePrefabHandle variantHandle = scene.Prefabs().RegisterVariant(
        "StableNodeVariant",
        baseHandle,
        std::vector<kb::scene::ScenePrefabPropertyOverride>{
            kb::scene::ScenePrefabPropertyOverride{
                .nodeIndex = firstChildNode,
                .nodeId = targetNodeId,
                .propertyPath = "name",
                .value = "Stable Target Changed",
                .flag = kb::scene::ScenePrefabOverrideFlag::Name,
            },
        });
    kb::tests::Require(variantHandle.IsValid(), "Stable node identity variant did not register");

    const kb::scene::ScenePrefab variant = scene.Prefabs().Get(variantHandle);
    kb::tests::Require(variant.TryGetNode(firstChildNode)->name == "Stable First Child", "Stable node identity incorrectly applied override by stale node index");
    kb::tests::Require(variant.TryGetNode(targetChildNode)->name == "Stable Target Changed", "Stable node identity did not apply override by stable node id");

    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(variantHandle);
    kb::tests::Require(scene.Entities().Name(instance.ObjectAt(firstChildNode)) == "Stable First Child", "Stable node identity instance changed the wrong child");
    kb::tests::Require(scene.Entities().Name(instance.ObjectAt(targetChildNode)) == "Stable Target Changed", "Stable node identity instance did not use the stable node override");
}

void RunPrefabRemovedNodeOverrideDoesNotFallbackToIndexTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    constexpr std::uint64_t kRootNodeId = 1U;
    constexpr std::uint64_t kDeletedNodeId = 2U;
    constexpr std::uint64_t kSurvivorNodeId = 3U;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = kRootNodeId,
        .name = "Removed Stable Root",
    });
    const std::uint32_t survivorNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = kSurvivorNodeId,
        .name = "Removed Stable Survivor",
        .parentNode = rootNode,
    });

    const kb::scene::ScenePrefabHandle baseHandle = scene.Prefabs().Register("RemovedStableNodeBase", std::move(prefab));
    kb::tests::Require(baseHandle.IsValid(), "Removed stable node base prefab did not register");

    const kb::scene::ScenePrefabHandle variantHandle = scene.Prefabs().RegisterVariant(
        "RemovedStableNodeVariant",
        baseHandle,
        std::vector<kb::scene::ScenePrefabPropertyOverride>{
            kb::scene::ScenePrefabPropertyOverride{
                .nodeIndex = survivorNode,
                .nodeId = kDeletedNodeId,
                .propertyPath = "name",
                .value = "Wrong Survivor Override",
                .flag = kb::scene::ScenePrefabOverrideFlag::Name,
            },
        });
    kb::tests::Require(!variantHandle.IsValid(), "Removed stable node override should not fall back to a reused node index");

    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(baseHandle);
    kb::tests::Require(scene.Entities().Name(instance.ObjectAt(survivorNode)) == "Removed Stable Survivor", "Removed stable node override changed the survivor node");
}

void RunNestedPrefabOverrideSurvivesInnerReorderTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab innerPrefab;
    constexpr std::uint64_t kInnerRootNodeId = 1U;
    constexpr std::uint64_t kInnerFirstChildNodeId = 2U;
    constexpr std::uint64_t kInnerTargetChildNodeId = 3U;
    const std::uint32_t innerRoot = innerPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = kInnerRootNodeId,
        .name = "Nested Reorder Root",
    });
    const std::uint32_t reorderedTargetChild = innerPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = kInnerTargetChildNodeId,
        .name = "Nested Reorder Target",
        .parentNode = innerRoot,
    });
    const std::uint32_t reorderedFirstChild = innerPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .stableId = kInnerFirstChildNodeId,
        .name = "Nested Reorder First",
        .parentNode = innerRoot,
    });
    const kb::scene::ScenePrefabHandle innerHandle = scene.Prefabs().Register("NestedReorderedInnerPrefab", std::move(innerPrefab));
    const std::string innerGuid = scene.Prefabs().Guid(innerHandle);
    kb::tests::Require(!innerGuid.empty(), "Nested reorder inner prefab did not receive a guid");

    kb::scene::ScenePrefab outerPrefab;
    const std::uint32_t outerRoot = outerPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Nested Reorder Outer Root" });
    static_cast<void>(outerPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Nested Reorder Placeholder",
        .nestedPrefabGuid = innerGuid,
        .nestedPrefabOverrides = std::vector<kb::scene::ScenePrefabPropertyOverride>{
            kb::scene::ScenePrefabPropertyOverride{
                .nodeIndex = reorderedFirstChild,
                .nodeId = kInnerTargetChildNodeId,
                .propertyPath = "name",
                .value = "Nested Reorder Target Override",
                .flag = kb::scene::ScenePrefabOverrideFlag::Name,
            },
        },
        .parentNode = outerRoot,
    }));
    const kb::scene::ScenePrefabHandle outerHandle = scene.Prefabs().Register("NestedReorderedOuterPrefab", std::move(outerPrefab));
    kb::tests::Require(outerHandle.IsValid(), "Nested reorder outer prefab did not register");

    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(outerHandle);
    kb::tests::Require(instance.ObjectCount() == 4U, "Nested reorder prefab did not expand all nodes");
    kb::tests::Require(scene.Entities().Name(instance.ObjectAt(reorderedTargetChild + 1U)) == "Nested Reorder Target Override", "Nested reorder override did not apply by stable node id");
    kb::tests::Require(scene.Entities().Name(instance.ObjectAt(reorderedFirstChild + 1U)) == "Nested Reorder First", "Nested reorder override incorrectly applied by stale node index");
}

void RunPrefabClearDoesNotReuseStaleHandlesTest() {
    const std::filesystem::path prefabPath = std::filesystem::temp_directory_path() / "21kb_engine_prefab_clear_stale_handles.kbprefab";
    std::error_code removeError;
    std::filesystem::remove(prefabPath, removeError);

    kb::scene::Scene scene;
    kb::scene::ScenePrefab prefab;
    static_cast<void>(prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Clear Stale Root" }));
    const kb::scene::ScenePrefabHandle stalePrefab = scene.Prefabs().Register("ClearStalePrefab", std::move(prefab));
    kb::tests::Require(stalePrefab.IsValid(), "Clear stale prefab setup did not register prefab");
    kb::tests::Require(scene.Prefabs().Save(stalePrefab, prefabPath), "Clear stale prefab setup did not save prefab");

    const kb::scene::ScenePrefabInstance staleInstance = scene.Prefabs().Instantiate(stalePrefab);
    kb::tests::Require(staleInstance.Handle().IsValid(), "Clear stale prefab setup did not instantiate prefab");

    scene.Prefabs().Clear();

    kb::tests::Require(!scene.Prefabs().Contains(stalePrefab), "Stale prefab handle should be invalid immediately after prefab registry clear");
    kb::tests::Require(!scene.Prefabs().IsInstance(staleInstance.Handle()), "Stale prefab instance handle should be invalid immediately after instance registry clear");

    const kb::scene::ScenePrefabHandle reloadedPrefab = scene.Prefabs().Load(prefabPath);
    kb::tests::Require(reloadedPrefab.IsValid(), "Clear stale prefab reload failed");
    kb::tests::Require(reloadedPrefab != stalePrefab, "Reload after prefab registry clear reused a stale prefab handle");
    kb::tests::Require(!scene.Prefabs().Contains(stalePrefab), "Stale prefab handle resolved to a reloaded prefab");

    const kb::scene::ScenePrefabInstance reloadedInstance = scene.Prefabs().Instantiate(reloadedPrefab);
    kb::tests::Require(reloadedInstance.Handle().IsValid(), "Clear stale prefab reload did not instantiate");
    kb::tests::Require(reloadedInstance.Handle() != staleInstance.Handle(), "Instantiate after instance registry clear reused a stale instance handle");
    kb::tests::Require(!scene.Prefabs().IsInstance(staleInstance.Handle()), "Stale prefab instance handle resolved to a new instance");

    std::filesystem::remove(prefabPath, removeError);
}

void RunPrefabMappingsIgnoreDestroyedEntitiesTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Destroyed Mapping Root" });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Destroyed Mapping Child",
        .parentNode = rootNode,
    });
    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("DestroyedMappingPrefab", std::move(prefab));
    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(prefabHandle);
    kb::tests::Require(instance.Handle().IsValid(), "Destroyed mapping setup did not instantiate prefab");

    const kb::scene::SceneObject rootObject = instance.ObjectAt(rootNode);
    const kb::scene::SceneObject childObject = instance.ObjectAt(childNode);
    std::uint32_t nodeIndex = 99;
    std::uint64_t nodeId = 99;
    kb::tests::Require(scene.Prefabs().ContainingInstance(childObject, nodeIndex, nodeId) == instance.Handle(), "Destroyed mapping setup did not track child object");

    scene.Entities().Destroy(childObject);
    kb::tests::Require(!scene.Entities().IsAlive(childObject), "Destroyed mapping child was not destroyed");
    kb::tests::Require(scene.Prefabs().IsInstance(instance.Handle()), "Destroying a non-root prefab object should keep the instance active for missing-object overrides");
    const kb::scene::SceneObject replacement = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Destroyed Mapping Replacement" });
    scene.Entities().SetName(childObject, "Stale Prefab Object Rename");
    kb::scene::TagsComponent staleTags;
    kb::scene::SetTagsText(staleTags, "stale-prefab-object");
    scene.Components().Tags().Set(childObject.Entity(), staleTags);
    kb::tests::Require(scene.Entities().Name(replacement) == "Destroyed Mapping Replacement", "Stale prefab SceneObject renamed a replacement object");
    kb::tests::Require(!scene.Components().Tags().Has(replacement.Entity()), "Stale prefab SceneObject modified replacement components");
    nodeIndex = 99;
    nodeId = 99;
    kb::tests::Require(!scene.Prefabs().ContainingInstance(childObject, nodeIndex).IsValid(), "Destroyed child still resolved to a prefab instance");
    kb::tests::Require(!scene.Prefabs().ContainingInstance(childObject, nodeIndex, nodeId).IsValid(), "Destroyed child still resolved to a stable prefab node mapping");
    kb::tests::Require(nodeIndex == 0 && nodeId == kb::scene::ScenePrefabNodeDesc::InvalidStableId, "Destroyed child mapping did not reset output values");

    scene.Entities().Destroy(rootObject);
    kb::tests::Require(!scene.Entities().IsAlive(rootObject), "Destroyed mapping root was not destroyed");
    nodeIndex = 99;
    kb::tests::Require(!scene.Prefabs().IsInstance(instance.Handle()), "Destroying a prefab root should invalidate the prefab instance handle");
    kb::tests::Require(!scene.Prefabs().RootInstance(rootObject).IsValid(), "Destroyed root still resolved as prefab root");
    kb::tests::Require(!scene.Prefabs().ContainingInstance(rootObject, nodeIndex).IsValid(), "Destroyed root still resolved to a prefab instance");
    kb::tests::Require(!scene.Prefabs().SourcePrefab(rootObject).IsValid(), "Destroyed root still resolved to a source prefab");
}

void RunRegisteredBulkPrefabSpawnDestroyChurnTest() {
    constexpr std::size_t kRounds = 5;
    constexpr std::size_t kInstancesPerRound = 48;

    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Churn Root",
        .components = kb::scene::ScenePrefabNodeComponents{
            .meshRenderer = kb::scene::MeshRendererComponent{
                .meshAssetId = 17,
                .materialAssetId = 23,
            },
        },
    });
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Churn Child",
        .parentNode = rootNode,
        .components = kb::scene::ScenePrefabNodeComponents{
            .camera = kb::scene::CameraComponent{
                .primary = false,
            },
        },
    });
    const std::uint32_t lightNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Churn Light",
        .parentNode = childNode,
        .components = kb::scene::ScenePrefabNodeComponents{
            .light = kb::scene::LightComponent{
                .kind = kb::scene::LightKind::Point,
                .intensity = 3.0F,
            },
        },
    });
    const std::uint32_t taggedNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Churn Tagged",
        .parentNode = rootNode,
        .components = kb::scene::ScenePrefabNodeComponents{
            .tags = [] {
                kb::scene::TagsComponent tags;
                kb::scene::SetTagsText(tags, "churn");
                return tags;
            }(),
        },
    });
    const std::size_t nodesPerInstance = prefab.Nodes().size();
    const kb::scene::ScenePrefabHandle prefabHandle = scene.Prefabs().Register("RegisteredBulkChurnPrefab", std::move(prefab));

    std::vector<kb::scene::ScenePrefabInstanceHandle> staleHandles;
    std::vector<kb::scene::SceneObject> staleObjects;
    staleHandles.reserve(kRounds * kInstancesPerRound);
    staleObjects.reserve(kRounds * kInstancesPerRound * nodesPerInstance);

    for (std::size_t round = 0; round < kRounds; ++round) {
        const std::vector<kb::scene::ScenePrefabInstance> instances = scene.Prefabs().InstantiateMany(prefabHandle, kInstancesPerRound);
        const kb::scene::ScenePrefabInstantiationStats spawnStats = scene.Prefabs().LastInstantiationStats();
        kb::tests::Require(instances.size() == kInstancesPerRound, "Prefab churn spawn did not return every requested instance");
        kb::tests::Require(spawnStats.requestedInstances == kInstancesPerRound, "Prefab churn stats recorded invalid request count");
        kb::tests::Require(spawnStats.instantiatedInstances == kInstancesPerRound, "Prefab churn stats recorded invalid instance count");
        kb::tests::Require(spawnStats.nodesPerInstance == nodesPerInstance, "Prefab churn stats recorded invalid node count");
        kb::tests::Require(spawnStats.entitiesCreated == kInstancesPerRound * nodesPerInstance, "Prefab churn stats recorded invalid entity count");
        kb::tests::Require(spawnStats.registeredInstanceCount == kInstancesPerRound, "Prefab churn stats recorded invalid registered instance count");
        kb::tests::Require(scene.Entities().Count() == kInstancesPerRound * nodesPerInstance, "Prefab churn left unexpected live scene entities after spawn");
        kb::tests::Require(scene.Runtime().EcsWorld().NativeStorageStats().liveEntities == scene.Entities().Count(), "Prefab churn scene count diverged from native storage after spawn");

        std::vector<kb::scene::SceneObject> roots;
        roots.reserve(instances.size());
        for (const kb::scene::ScenePrefabInstance& instance : instances) {
            kb::tests::Require(instance.Handle().IsValid(), "Prefab churn returned an untracked registered instance");
            kb::tests::Require(scene.Prefabs().IsInstance(instance.Handle()), "Prefab churn registered handle did not resolve");
            kb::tests::Require(scene.Prefabs().RootInstance(instance.RootObject()) == instance.Handle(), "Prefab churn root mapping did not resolve");
            kb::tests::Require(scene.Prefabs().SourcePrefab(instance.RootObject()) == prefabHandle, "Prefab churn root source prefab did not resolve");
            kb::tests::Require(scene.Hierarchy().Parent(instance.ObjectAt(childNode).Entity()) == instance.ObjectAt(rootNode).Entity(), "Prefab churn child hierarchy was not linked");
            kb::tests::Require(scene.Hierarchy().Parent(instance.ObjectAt(lightNode).Entity()) == instance.ObjectAt(childNode).Entity(), "Prefab churn grandchild hierarchy was not linked");
            kb::tests::Require(scene.Hierarchy().Parent(instance.ObjectAt(taggedNode).Entity()) == instance.ObjectAt(rootNode).Entity(), "Prefab churn sibling hierarchy was not linked");
            roots.push_back(instance.RootObject());
            staleHandles.push_back(instance.Handle());
            staleObjects.insert(staleObjects.end(), instance.Objects().begin(), instance.Objects().end());
        }

        scene.Entities().Destroy(std::span<const kb::scene::SceneObject>{ roots });
        kb::tests::Require(scene.Entities().Count() == 0, "Prefab churn root destroy did not remove every instance object");
        kb::tests::Require(scene.Runtime().EcsWorld().NativeStorageStats().liveEntities == 0, "Prefab churn root destroy left native storage entities alive");
        for (const kb::scene::ScenePrefabInstanceHandle handle : staleHandles) {
            kb::tests::Require(!scene.Prefabs().IsInstance(handle), "Prefab churn kept a destroyed instance handle alive");
        }
        for (const kb::scene::SceneObject object : staleObjects) {
            std::uint32_t nodeIndex = 99;
            kb::tests::Require(!scene.Entities().IsAlive(object), "Prefab churn stale object stayed alive after root destroy");
            kb::tests::Require(!scene.Prefabs().RootInstance(object).IsValid(), "Prefab churn stale object still resolved as a root instance");
            kb::tests::Require(!scene.Prefabs().ContainingInstance(object, nodeIndex).IsValid(), "Prefab churn stale object still resolved as an instance member");
            kb::tests::Require(!scene.Prefabs().SourcePrefab(object).IsValid(), "Prefab churn stale object still resolved to a source prefab");
        }
    }

    const std::vector<kb::scene::ScenePrefabInstance> finalInstances = scene.Prefabs().InstantiateMany(prefabHandle, kInstancesPerRound);
    kb::tests::Require(finalInstances.size() == kInstancesPerRound, "Prefab churn final spawn did not return every requested instance");
    kb::tests::Require(scene.Entities().Count() == kInstancesPerRound * nodesPerInstance, "Prefab churn final spawn created an unexpected live entity count");
    for (const kb::scene::ScenePrefabInstance& instance : finalInstances) {
        kb::tests::Require(instance.Handle().IsValid(), "Prefab churn final spawn returned an untracked registered instance");
        kb::tests::Require(scene.Prefabs().IsInstance(instance.Handle()), "Prefab churn final handle did not resolve");
        kb::tests::Require(scene.Prefabs().RootInstance(instance.RootObject()) == instance.Handle(), "Prefab churn final root mapping did not resolve");
        kb::tests::Require(scene.Prefabs().SourcePrefab(instance.RootObject()) == prefabHandle, "Prefab churn final source prefab did not resolve");
    }
}

} // namespace

namespace kb::tests {

void RunScenePrefabInstantiationTests() {
    const auto run = [](const char* name, void (*test)()) {
        TracePrefabTest(name);
        test();
    };
    run("RunPrefabInstantiationTest", RunPrefabInstantiationTest);
    run("RunInvalidPrefabInstantiationTest", RunInvalidPrefabInstantiationTest);
    run("RunRegisteredPrefabInstantiationTest", RunRegisteredPrefabInstantiationTest);
    run("RunRegisteredPrefabBatchInstantiationTest", RunRegisteredPrefabBatchInstantiationTest);
    run("RunBulkPrefabInstantiationTest", RunBulkPrefabInstantiationTest);
    run("RunBulkPrefabSceneHierarchyOnlyInstantiationTest", RunBulkPrefabSceneHierarchyOnlyInstantiationTest);
    run("RunBulkPrefabBatchStatsOnlyInstantiationTest", RunBulkPrefabBatchStatsOnlyInstantiationTest);
    run("RunBulkPrefabMultiArchetypeNodeOrderTest", RunBulkPrefabMultiArchetypeNodeOrderTest);
    run("RunLargePrefabHierarchyTransformTest", RunLargePrefabHierarchyTransformTest);
    run("RunRegisteredBulkPrefabInstantiationTest", RunRegisteredBulkPrefabInstantiationTest);
    run("RunRegisteredBulkPrefabInvalidatesQueryCachePerBatchTest", RunRegisteredBulkPrefabInvalidatesQueryCachePerBatchTest);
    run("RunRegisteredBulkPrefabInstancesUseSharedObjectSlabTest", RunRegisteredBulkPrefabInstancesUseSharedObjectSlabTest);
    run("RunRegisteredBulkPrefabPooledSlabSurvivesReturnedInstancesTest", RunRegisteredBulkPrefabPooledSlabSurvivesReturnedInstancesTest);
    run("RunRegisteredBulkPrefabReconnectKeepsSourceIdentityTest", RunRegisteredBulkPrefabReconnectKeepsSourceIdentityTest);
    run("RunSceneHistoryRestoresRegisteredPrefabInstanceSnapshotTest", RunSceneHistoryRestoresRegisteredPrefabInstanceSnapshotTest);
    run("RunSceneHistoryRestoresNestedRegisteredPrefabInstanceSnapshotTest", RunSceneHistoryRestoresNestedRegisteredPrefabInstanceSnapshotTest);
    run("RunSceneHistoryRestoresBulkPrefabArchetypesAndNodeMappingsTest", RunSceneHistoryRestoresBulkPrefabArchetypesAndNodeMappingsTest);
    run("RunSceneHistoryRestoresBulkPrefabResolvedBaselineTest", RunSceneHistoryRestoresBulkPrefabResolvedBaselineTest);
    run("RunRegisteredPrefabOverrideLifecycleTest", RunRegisteredPrefabOverrideLifecycleTest);
    run("RunRegisteredPrefabBatchApplyOverridesTest", RunRegisteredPrefabBatchApplyOverridesTest);
    run("RunRegisteredPrefabBatchApplyDirtyNodeSupersetRegressionTest", RunRegisteredPrefabBatchApplyDirtyNodeSupersetRegressionTest);
    run("RunRegisteredPrefabBatchRevertTopologyDirtyRegressionTest", RunRegisteredPrefabBatchRevertTopologyDirtyRegressionTest);
    run("RunMissingPrefabInstanceObjectOverrideTest", RunMissingPrefabInstanceObjectOverrideTest);
    run("RunRegisteredPrefabFullComponentOverrideLifecycleTest", RunRegisteredPrefabFullComponentOverrideLifecycleTest);
    run("RunPrefabApplyRejectsDetachedTrackedChildTest", RunPrefabApplyRejectsDetachedTrackedChildTest);
    run("RunPrefabVariantInstantiationTest", RunPrefabVariantInstantiationTest);
    run("RunPrefabVariantApplyUpdatesVariantOnlyTest", RunPrefabVariantApplyUpdatesVariantOnlyTest);
    run("RunPrefabBaseApplyRefreshesVariantInstancesPreservingLocalOverridesTest", RunPrefabBaseApplyRefreshesVariantInstancesPreservingLocalOverridesTest);
    run("RunPrefabConnectionMetadataAndUnpackTest", RunPrefabConnectionMetadataAndUnpackTest);
    run("RunPrefabStaleHandleProtectionTest", RunPrefabStaleHandleProtectionTest);
    run("RunPrefabMissingSourceUnloadUnpackReconnectTest", RunPrefabMissingSourceUnloadUnpackReconnectTest);
    run("RunPrefabUnpackNestedVariantModesTest", RunPrefabUnpackNestedVariantModesTest);
    run("RunPrefabApplyRefreshesExistingInstancesTest", RunPrefabApplyRefreshesExistingInstancesTest);
    run("RunPrefabApplyAddedChildRefreshesExistingInstancesTest", RunPrefabApplyAddedChildRefreshesExistingInstancesTest);
    run("RunPrefabAddedRemovedMissingNodesStableAfterRefreshAndSaveTest", RunPrefabAddedRemovedMissingNodesStableAfterRefreshAndSaveTest);
    run("RunPrefabRefreshLargeInstanceSetTest", RunPrefabRefreshLargeInstanceSetTest);
    run("RunPrefabApplyOverrideToAssetTest", RunPrefabApplyOverrideToAssetTest);
    run("RunPrefabAssetLoadMigratesMissingNodeStableIdsTest", RunPrefabAssetLoadMigratesMissingNodeStableIdsTest);
    run("RunNestedPrefabCompositionTest", RunNestedPrefabCompositionTest);
    run("RunNestedPrefabCaptureAndRefreshTest", RunNestedPrefabCaptureAndRefreshTest);
    run("RunPrefabPrivateSceneApplyPreservesMainSceneOverridesTest", RunPrefabPrivateSceneApplyPreservesMainSceneOverridesTest);
    run("RunPrefabStableNodeIdentityOverridesTest", RunPrefabStableNodeIdentityOverridesTest);
    run("RunPrefabRemovedNodeOverrideDoesNotFallbackToIndexTest", RunPrefabRemovedNodeOverrideDoesNotFallbackToIndexTest);
    run("RunNestedPrefabOverrideSurvivesInnerReorderTest", RunNestedPrefabOverrideSurvivesInnerReorderTest);
    run("RunPrefabClearDoesNotReuseStaleHandlesTest", RunPrefabClearDoesNotReuseStaleHandlesTest);
    run("RunPrefabMappingsIgnoreDestroyedEntitiesTest", RunPrefabMappingsIgnoreDestroyedEntitiesTest);
    run("RunRegisteredBulkPrefabSpawnDestroyChurnTest", RunRegisteredBulkPrefabSpawnDestroyChurnTest);
}

} // namespace kb::tests
