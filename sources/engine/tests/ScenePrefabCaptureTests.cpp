#include "ScenePrefabTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabCaptureSettings.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

namespace {

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
    kb::tests::Require(prefab.NodeCount() == 3, "Captured prefab did not include the full hierarchy");

    kb::scene::Scene target;
    const kb::scene::ScenePrefabInstance instance = target.Prefabs().Instantiate(prefab);
    kb::tests::Require(instance.ObjectCount() == 3, "Captured prefab did not instantiate all captured nodes");
    kb::tests::Require(target.Entities().Name(instance.ObjectAt(0)) == "Root", "Captured root name was not preserved");
    kb::tests::Require(target.Entities().Name(instance.ObjectAt(1)) == "Child", "Captured child name was not preserved");
    kb::tests::Require(target.Entities().Name(instance.ObjectAt(2)) == "Grandchild", "Captured grandchild name was not preserved");
    kb::tests::Require(target.Hierarchy().Parent(instance.ObjectAt(1).Entity()) == instance.ObjectAt(0).Entity(), "Captured child parent was not preserved");
    kb::tests::Require(target.Hierarchy().Parent(instance.ObjectAt(2).Entity()) == instance.ObjectAt(1).Entity(), "Captured grandchild parent was not preserved");
    kb::tests::Require(!target.Components().Visibility().Get(instance.ObjectAt(0).Entity()).visible, "Captured visibility was not preserved");
    const kb::scene::MeshRendererComponent* capturedMeshRenderer = target.Components().MeshRenderers().TryGet(instance.ObjectAt(0).Entity());
    const kb::scene::CameraComponent* capturedCamera = target.Components().Cameras().TryGet(instance.ObjectAt(1).Entity());
    const kb::scene::LightComponent* capturedLight = target.Components().Lights().TryGet(instance.ObjectAt(2).Entity());
    kb::tests::Require(capturedMeshRenderer != nullptr && capturedMeshRenderer->meshAssetId == 17, "Captured mesh renderer was not preserved");
    kb::tests::Require(capturedCamera != nullptr && capturedCamera->orthographicHeight == 12.0F, "Captured camera was not preserved");
    kb::tests::Require(capturedLight != nullptr && capturedLight->intensity == 9.0F, "Captured light was not preserved");

    [[maybe_unused]] const bool progressed = target.Runtime().Update(0.016F);
    const kb::scene::TransformComponent capturedGrandchildTransform = target.Transforms().Get(instance.ObjectAt(2));
    kb::tests::Require(kb::tests::NearlyEqual(capturedGrandchildTransform.worldPosition.x, 4.0F), "Captured prefab world X was not rebuilt");
    kb::tests::Require(kb::tests::NearlyEqual(capturedGrandchildTransform.worldPosition.y, 5.0F), "Captured prefab world Y was not rebuilt");
    kb::tests::Require(kb::tests::NearlyEqual(capturedGrandchildTransform.worldPosition.z, 6.0F), "Captured prefab world Z was not rebuilt");

    const kb::scene::ScenePrefab rootOnlyPrefab = source.Prefabs().Capture(root, kb::scene::ScenePrefabCaptureSettings{
        .includeChildren = false,
    });
    kb::tests::Require(rootOnlyPrefab.NodeCount() == 1, "Root-only capture should not include children");

    kb::scene::Scene unrelatedScene;
    const kb::scene::ScenePrefab crossScenePrefab = unrelatedScene.Prefabs().Capture(root);
    kb::tests::Require(crossScenePrefab.Empty(), "Capture should reject objects from a different scene");
}

} // namespace

namespace kb::tests {

void RunScenePrefabCaptureTests() {
    RunPrefabCaptureTest();
}

} // namespace kb::tests
