#include "ScenePrefabTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <utility>

namespace {

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

    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().Register("RegisteredPrefab", std::move(prefab));
    kb::tests::Require(handle.IsValid(), "Registered prefab did not return a valid engine handle");
    kb::tests::Require(scene.Prefabs().Contains(handle), "Registered prefab handle was not stored by the engine");
    kb::tests::Require(scene.Prefabs().RegisteredCount() == 1, "Registered prefab count was not updated");

    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(handle);
    kb::tests::Require(instance.ObjectCount() == 2, "Registered prefab did not instantiate all nodes");
    kb::tests::Require(instance.Handle().IsValid(), "Registered prefab instance did not return an engine instance handle");
    kb::tests::Require(scene.Prefabs().IsInstance(instance.Handle()), "Registered prefab instance handle was not tracked by the engine");
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
    kb::tests::Require(report.nodes[0].nodeIndex == childNode, "Prefab override detector reported the wrong node");
    kb::tests::Require(kb::scene::HasPrefabOverride(report.nodes[0].flags, kb::scene::ScenePrefabOverrideFlag::Name), "Prefab override detector missed name override");
    kb::tests::Require(kb::scene::HasPrefabOverride(report.nodes[0].flags, kb::scene::ScenePrefabOverrideFlag::Transform), "Prefab override detector missed transform override");
    kb::tests::Require(kb::scene::HasPrefabOverride(report.nodes[0].flags, kb::scene::ScenePrefabOverrideFlag::Visibility), "Prefab override detector missed visibility override");

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
    kb::tests::Require(!scene.Prefabs().ApplyOverrides(instanceHandle), "Prefab override apply should fail when an instance object is missing");
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

} // namespace

namespace kb::tests {

void RunScenePrefabInstantiationTests() {
    RunPrefabInstantiationTest();
    RunInvalidPrefabInstantiationTest();
    RunRegisteredPrefabInstantiationTest();
    RunRegisteredPrefabOverrideLifecycleTest();
    RunMissingPrefabInstanceObjectOverrideTest();
    RunPrefabApplyRejectsDetachedTrackedChildTest();
}

} // namespace kb::tests
