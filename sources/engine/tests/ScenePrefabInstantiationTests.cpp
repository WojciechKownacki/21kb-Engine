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

#include <filesystem>
#include <system_error>
#include <utility>
#include <vector>

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
    kb::tests::Require(scene.Prefabs().RootInstance(instance.ObjectAt(rootNode)) == instance.Handle(), "Registered prefab root should own the prefab instance handle");
    kb::tests::Require(!scene.Prefabs().RootInstance(instance.ObjectAt(childNode)).IsValid(), "Registered prefab child must not own a separate prefab instance handle");
    std::uint32_t rootContainingNode = 99;
    std::uint32_t childContainingNode = 99;
    kb::tests::Require(scene.Prefabs().ContainingInstance(instance.ObjectAt(rootNode), rootContainingNode) == instance.Handle(), "Registered prefab root should map to the tracked prefab instance");
    kb::tests::Require(scene.Prefabs().ContainingInstance(instance.ObjectAt(childNode), childContainingNode) == instance.Handle(), "Registered prefab child should remain tracked inside the parent prefab instance");
    kb::tests::Require(rootContainingNode == rootNode, "Registered prefab root should map to the root prefab node");
    kb::tests::Require(childContainingNode == childNode, "Registered prefab child should map to the child prefab node");
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

void RunRegisteredPrefabFullComponentOverrideLifecycleTest() {
    kb::scene::Scene scene;

    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Full Component Root",
        .components = kb::scene::ScenePrefabNodeComponents{
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
    kb::tests::Require(kb::scene::HasPrefabOverride(flags, kb::scene::ScenePrefabOverrideFlag::AudioSource), "Prefab override detector missed audio source override");
    kb::tests::Require(kb::scene::HasPrefabOverride(flags, kb::scene::ScenePrefabOverrideFlag::AudioListener), "Prefab override detector missed audio listener override");

    bool foundAudioVolume = false;
    bool foundColliderRemoval = false;
    for (const kb::scene::ScenePrefabPropertyOverride& property : report.properties) {
        foundAudioVolume = foundAudioVolume || property.propertyPath == "audioSource.volume";
        foundColliderRemoval = foundColliderRemoval || (property.propertyPath == "collider" && property.value == "null");
    }
    kb::tests::Require(foundAudioVolume, "Prefab override detector missed audio source property delta");
    kb::tests::Require(foundColliderRemoval, "Prefab override detector missed collider removal delta");

    kb::tests::Require(scene.Prefabs().RevertOverrides(instance.Handle()), "Full component prefab override revert failed");
    const kb::scene::InputComponent* revertedInput = scene.Components().Inputs().TryGet(entity);
    const kb::scene::ColliderComponent* revertedCollider = scene.Components().Colliders().TryGet(entity);
    const kb::scene::TagsComponent* revertedTags = scene.Components().Tags().TryGet(entity);
    const kb::scene::AudioSourceComponent* revertedAudioSource = scene.Components().AudioSources().TryGet(entity);
    kb::tests::Require(revertedInput != nullptr && revertedInput->priority == 1 && revertedInput->enabled, "Full component prefab revert did not restore input");
    kb::tests::Require(revertedCollider != nullptr && revertedCollider->shape == kb::scene::ColliderShape::Box, "Full component prefab revert did not restore collider");
    kb::tests::Require(revertedTags != nullptr && kb::scene::TagsText(*revertedTags) == "Prefab, Base", "Full component prefab revert did not restore tags");
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

} // namespace

namespace kb::tests {

void RunScenePrefabInstantiationTests() {
    RunPrefabInstantiationTest();
    RunInvalidPrefabInstantiationTest();
    RunRegisteredPrefabInstantiationTest();
    RunRegisteredPrefabOverrideLifecycleTest();
    RunMissingPrefabInstanceObjectOverrideTest();
    RunRegisteredPrefabFullComponentOverrideLifecycleTest();
    RunPrefabApplyRejectsDetachedTrackedChildTest();
    RunPrefabVariantInstantiationTest();
    RunPrefabApplyOverrideToAssetTest();
    RunNestedPrefabCompositionTest();
    RunNestedPrefabCaptureAndRefreshTest();
}

} // namespace kb::tests
