#include "engine/ecs/System.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabCaptureSettings.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

struct SystemCounters {
    int created = 0;
    int updated = 0;
    int destroyed = 0;
    float lastDelta = 0.0F;
};

struct SceneSystemCounters {
    int created = 0;
    int updated = 0;
    int destroyed = 0;
};

class CountingSystem final : public kb::ecs::System {
public:
    explicit CountingSystem(SystemCounters& counters) noexcept
        : counters_(counters) {}

    void OnCreate(kb::ecs::World& world) override {
        static_cast<void>(world);
        ++counters_.created;
    }

    void OnUpdate(kb::ecs::World& world, float deltaSeconds) override {
        static_cast<void>(world);
        ++counters_.updated;
        counters_.lastDelta = deltaSeconds;
    }

    void OnDestroy(kb::ecs::World& world) override {
        static_cast<void>(world);
        ++counters_.destroyed;
    }

private:
    SystemCounters& counters_;
};

class MoveEntitySceneSystem final : public kb::scene::SceneSystem {
public:
    MoveEntitySceneSystem(SceneSystemCounters& counters, kb::scene::SceneEntity entity, kb::scene::Vec3 targetPosition) noexcept
        : counters_(counters)
        , entity_(entity)
        , targetPosition_(targetPosition) {}

    void OnCreate(kb::scene::SceneSystemContext& context) override {
        static_cast<void>(context);
        ++counters_.created;
    }

    void OnUpdate(kb::scene::SceneSystemContext& context) override {
        ++counters_.updated;
        kb::scene::TransformComponent transform = context.Transforms().Get(entity_);
        transform.localPosition = targetPosition_;
        context.Transforms().Set(entity_, transform);
    }

    void OnDestroy(kb::scene::SceneSystemContext& context) override {
        static_cast<void>(context);
        ++counters_.destroyed;
    }

private:
    SceneSystemCounters& counters_;
    kb::scene::SceneEntity entity_{};
    kb::scene::Vec3 targetPosition_{};
};

[[nodiscard]] bool NearlyEqual(float lhs, float rhs) noexcept {
    return std::fabs(lhs - rhs) <= 0.0001F;
}

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void RunSystemLifecycleTest() {
    SystemCounters counters;

    {
        kb::scene::Scene scene;
        scene.Runtime().AddSystem(std::make_unique<CountingSystem>(counters));
        Require(counters.created == 1, "System OnCreate was not called");

        [[maybe_unused]] const bool progressed = scene.Runtime().Update(0.25F);
        Require(counters.updated == 1, "System OnUpdate was not called");
        Require(NearlyEqual(counters.lastDelta, 0.25F), "System received invalid delta time");
    }

    Require(counters.destroyed == 1, "System OnDestroy was not called");
}

void RunTransformHierarchyTest() {
    kb::scene::Scene scene;

    kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Parent",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 10.0F, 1.0F, 0.0F },
        },
    });

    kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Child",
        .parent = parent,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 2.0F, 3.0F, 4.0F },
        },
    });

    [[maybe_unused]] const bool firstProgress = scene.Runtime().Update(0.016F);
    kb::scene::TransformComponent childTransform = scene.Transforms().Get(child);
    Require(NearlyEqual(childTransform.worldPosition.x, 12.0F), "Child world X was not composed with parent");
    Require(NearlyEqual(childTransform.worldPosition.y, 4.0F), "Child world Y was not composed with parent");
    Require(NearlyEqual(childTransform.worldPosition.z, 4.0F), "Child world Z was not composed with parent");
    Require(!childTransform.worldDirty, "Child transform stayed dirty after update");

    kb::scene::TransformComponent parentTransform = scene.Transforms().Get(parent);
    parentTransform.localPosition = kb::scene::Vec3{ 20.0F, 0.0F, 1.0F };
    scene.Transforms().Set(parent, parentTransform);

    [[maybe_unused]] const bool secondProgress = scene.Runtime().Update(0.016F);
    childTransform = scene.Transforms().Get(child);
    Require(NearlyEqual(childTransform.worldPosition.x, 22.0F), "Child world X did not react to parent change");
    Require(NearlyEqual(childTransform.worldPosition.y, 3.0F), "Child world Y did not react to parent change");
    Require(NearlyEqual(childTransform.worldPosition.z, 5.0F), "Child world Z did not react to parent change");

    Require(scene.Hierarchy().SetParent(child, {}), "Child could not detach from parent");
    [[maybe_unused]] const bool thirdProgress = scene.Runtime().Update(0.016F);
    childTransform = scene.Transforms().Get(child);
    Require(NearlyEqual(childTransform.worldPosition.x, 2.0F), "Detached child world X should match local X");
    Require(NearlyEqual(childTransform.worldPosition.y, 3.0F), "Detached child world Y should match local Y");
    Require(NearlyEqual(childTransform.worldPosition.z, 4.0F), "Detached child world Z should match local Z");
}

void RunSceneSystemTransformSyncTest() {
    SceneSystemCounters counters;

    {
        kb::scene::Scene scene;

        kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Parent",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
            },
        });

        kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Child",
            .parent = parent,
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F },
            },
        });

        scene.Runtime().AddSceneSystem(std::make_unique<MoveEntitySceneSystem>(counters, parent.Entity(), kb::scene::Vec3{ 10.0F, 0.0F, 0.0F }));
        Require(counters.created == 1, "Scene system OnCreate was not called");

        [[maybe_unused]] const bool progressed = scene.Runtime().Update(0.016F);
        Require(counters.updated == 1, "Scene system OnUpdate was not called");

        const kb::scene::TransformComponent childTransform = scene.Transforms().Get(child);
        Require(NearlyEqual(childTransform.worldPosition.x, 12.0F), "Scene system transform changes were not synchronized in the same update");
    }

    Require(counters.destroyed == 1, "Scene system OnDestroy was not called");
}

void RunPrefabInstantiationTest() {
    kb::scene::Scene scene;

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

    Require(instance.ObjectCount() == 3, "Prefab did not instantiate all nodes");
    Require(instance.RootObject().IsValid(), "Prefab root object is invalid");
    Require(scene.Entities().Name(instance.ObjectAt(rootNode)) == "Prefab/Root", "Prefab root name was not assigned");
    Require(scene.Hierarchy().Parent(instance.ObjectAt(rootNode).Entity()) == externalParent.Entity(), "Prefab root was not attached to external parent");
    Require(scene.Hierarchy().Parent(instance.ObjectAt(cameraNode).Entity()) == instance.ObjectAt(rootNode).Entity(), "Prefab camera parent was not assigned");
    Require(scene.Hierarchy().Parent(instance.ObjectAt(lightNode).Entity()) == instance.ObjectAt(rootNode).Entity(), "Prefab light parent was not assigned");

    Require(scene.Components().MeshRenderers().Has(instance.ObjectAt(rootNode).Entity()), "Prefab mesh renderer component was not assigned");
    Require(scene.Components().Cameras().Has(instance.ObjectAt(cameraNode).Entity()), "Prefab camera component was not assigned");
    Require(scene.Components().Lights().Has(instance.ObjectAt(lightNode).Entity()), "Prefab light component was not assigned");

    [[maybe_unused]] const bool progressed = scene.Runtime().Update(0.016F);
    const kb::scene::TransformComponent cameraTransform = scene.Transforms().Get(instance.ObjectAt(cameraNode));
    Require(NearlyEqual(cameraTransform.worldPosition.x, 6.0F), "Prefab world transform did not include external and prefab parent X");
    Require(NearlyEqual(cameraTransform.worldPosition.y, 2.0F), "Prefab world transform did not include prefab parent Y");
    Require(NearlyEqual(cameraTransform.worldPosition.z, -4.0F), "Prefab world transform did not include prefab parent Z");
}

void RunInvalidPrefabInstantiationTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab invalidPrefab;
    [[maybe_unused]] const std::uint32_t invalidNode = invalidPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Invalid",
        .parentNode = 1,
    });

    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(invalidPrefab);
    Require(instance.Empty(), "Invalid prefab should not instantiate");
    Require(scene.Entities().Count() == 0, "Invalid prefab should not create entities");
}

void RunPrefabCaptureTest() {
    kb::scene::Scene source;

    kb::scene::SceneObject root = source.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Root",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 4.0F, 0.0F, 0.0F },
        },
        .visibility = kb::scene::VisibilityComponent{
            .visible = false,
        },
    });

    kb::scene::SceneObject child = source.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Child",
        .parent = root,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 5.0F, 0.0F },
        },
    });

    kb::scene::SceneObject grandchild = source.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Grandchild",
        .parent = child,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 0.0F, 6.0F },
        },
    });

    source.Components().MeshRenderers().Set(root.Entity(), kb::scene::MeshRendererComponent{
        .meshAssetId = 17,
        .materialAssetId = 23,
    });
    source.Components().Cameras().Set(child.Entity(), kb::scene::CameraComponent{
        .projection = kb::scene::CameraProjection::Orthographic,
        .orthographicHeight = 12.0F,
        .primary = true,
    });
    source.Components().Lights().Set(grandchild.Entity(), kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Spot,
        .intensity = 9.0F,
    });

    const kb::scene::ScenePrefab prefab = source.Prefabs().Capture(root);
    Require(prefab.NodeCount() == 3, "Captured prefab did not include the full hierarchy");

    kb::scene::Scene target;
    const kb::scene::ScenePrefabInstance instance = target.Prefabs().Instantiate(prefab);
    Require(instance.ObjectCount() == 3, "Captured prefab did not instantiate all captured nodes");
    Require(target.Entities().Name(instance.ObjectAt(0)) == "Root", "Captured root name was not preserved");
    Require(target.Entities().Name(instance.ObjectAt(1)) == "Child", "Captured child name was not preserved");
    Require(target.Entities().Name(instance.ObjectAt(2)) == "Grandchild", "Captured grandchild name was not preserved");
    Require(target.Hierarchy().Parent(instance.ObjectAt(1).Entity()) == instance.ObjectAt(0).Entity(), "Captured child parent was not preserved");
    Require(target.Hierarchy().Parent(instance.ObjectAt(2).Entity()) == instance.ObjectAt(1).Entity(), "Captured grandchild parent was not preserved");
    Require(!target.Components().Visibility().Get(instance.ObjectAt(0).Entity()).visible, "Captured visibility was not preserved");
    const kb::scene::MeshRendererComponent* capturedMeshRenderer = target.Components().MeshRenderers().TryGet(instance.ObjectAt(0).Entity());
    const kb::scene::CameraComponent* capturedCamera = target.Components().Cameras().TryGet(instance.ObjectAt(1).Entity());
    const kb::scene::LightComponent* capturedLight = target.Components().Lights().TryGet(instance.ObjectAt(2).Entity());
    Require(capturedMeshRenderer != nullptr && capturedMeshRenderer->meshAssetId == 17, "Captured mesh renderer was not preserved");
    Require(capturedCamera != nullptr && capturedCamera->orthographicHeight == 12.0F, "Captured camera was not preserved");
    Require(capturedLight != nullptr && capturedLight->intensity == 9.0F, "Captured light was not preserved");

    [[maybe_unused]] const bool progressed = target.Runtime().Update(0.016F);
    const kb::scene::TransformComponent capturedGrandchildTransform = target.Transforms().Get(instance.ObjectAt(2));
    Require(NearlyEqual(capturedGrandchildTransform.worldPosition.x, 4.0F), "Captured prefab world X was not rebuilt");
    Require(NearlyEqual(capturedGrandchildTransform.worldPosition.y, 5.0F), "Captured prefab world Y was not rebuilt");
    Require(NearlyEqual(capturedGrandchildTransform.worldPosition.z, 6.0F), "Captured prefab world Z was not rebuilt");

    const kb::scene::ScenePrefab rootOnlyPrefab = source.Prefabs().Capture(root, kb::scene::ScenePrefabCaptureSettings{
        .includeChildren = false,
    });
    Require(rootOnlyPrefab.NodeCount() == 1, "Root-only capture should not include children");

    kb::scene::Scene unrelatedScene;
    const kb::scene::ScenePrefab crossScenePrefab = unrelatedScene.Prefabs().Capture(root);
    Require(crossScenePrefab.Empty(), "Capture should reject objects from a different scene");
}

} // namespace

int main() {
    RunSystemLifecycleTest();
    RunTransformHierarchyTest();
    RunSceneSystemTransformSyncTest();
    RunPrefabInstantiationTest();
    RunInvalidPrefabInstantiationTest();
    RunPrefabCaptureTest();
    return EXIT_SUCCESS;
}
