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

#include <filesystem>
#include <system_error>
#include <utility>
#include <vector>

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
    kb::tests::Require(source.Prefabs().RegisteredCount() == 0, "Capturing a prefab value should not register it in engine storage");

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

    const kb::scene::ScenePrefabHandle registered = source.Prefabs().CaptureRegistered(root, "CapturedRoot");
    kb::tests::Require(registered.IsValid(), "Captured prefab was not registered by the engine");
    kb::tests::Require(source.Prefabs().Contains(registered), "Captured prefab handle was not retained by the engine");
    kb::tests::Require(source.Prefabs().RegisteredCount() == 1, "CaptureRegistered should add exactly one prefab to engine storage");
    const kb::scene::ScenePrefabInstance registeredInstance = source.Prefabs().Instantiate(registered);
    kb::tests::Require(registeredInstance.ObjectCount() == 3, "Registered captured prefab did not instantiate from engine storage");
    kb::tests::Require(source.Hierarchy().ChildEntities(registeredInstance.ObjectAt(0).Entity()).size() == 1, "Registered captured prefab root did not expose its child");
}

void RunPrefabAssetRoundTripTest() {
    const std::filesystem::path prefabPath = std::filesystem::temp_directory_path() / "21kb_engine_prefab_roundtrip.kbprefab";
    std::error_code removeError;
    std::filesystem::remove(prefabPath, removeError);

    kb::scene::Scene source;
    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Asset Root",
        .nestedPrefabGuid = "nested-template-guid",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 3.0F, 0.0F, 0.0F },
        },
        .components = kb::scene::ScenePrefabNodeComponents{
            .meshRenderer = kb::scene::MeshRendererComponent{
                .meshAssetId = 101,
                .materialAssetId = 202,
                .castsShadow = false,
                .receivesShadow = true,
            },
            .input = kb::scene::InputComponent{
                .mappingContextAssetId = 303,
                .priority = -4,
                .enabled = false,
            },
            .rigidbody = kb::scene::RigidbodyComponent{
                .bodyType = kb::scene::RigidbodyBodyType::Kinematic,
                .mass = 8.0F,
                .linearVelocity = kb::scene::Vec3{ 1.0F, 2.0F, 3.0F },
                .angularVelocity = kb::scene::Vec3{ 4.0F, 5.0F, 6.0F },
                .gravityScale = 0.25F,
                .useGravity = false,
                .lockRotation = true,
            },
            .collider = kb::scene::ColliderComponent{
                .shape = kb::scene::ColliderShape::Capsule,
                .center = kb::scene::Vec3{ 0.1F, 0.2F, 0.3F },
                .boxSize = kb::scene::Vec3{ 2.0F, 3.0F, 4.0F },
                .radius = 1.25F,
                .height = 5.0F,
                .trigger = true,
            },
        },
    });
    kb::scene::SetTagsText(prefab.TryGetMutableNode(rootNode)->components.tags.emplace(), "Player, Runtime");
    const std::uint32_t childNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Asset Child\\Escaped",
        .parentNode = rootNode,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 4.0F, 0.0F },
        },
        .visibility = kb::scene::VisibilityComponent{
            .visible = false,
        },
        .components = kb::scene::ScenePrefabNodeComponents{
            .light = kb::scene::LightComponent{
                .kind = kb::scene::LightKind::Spot,
                .intensity = 6.0F,
                .range = 12.0F,
                .areaWidth = 2.0F,
                .areaHeight = 0.5F,
                .contactShadowLength = 0.3F,
                .volumetricScattering = 0.2F,
                .castsShadow = false,
            },
            .behaviour = kb::scene::BehaviourComponent{
                .behaviourAssetId = 404,
                .backend = kb::scene::BehaviourBackend::Lua,
                .enabled = false,
                .tickGroup = kb::scene::BehaviourTickGroup::Physics,
                .executionOrder = -12,
            },
            .audioSource = kb::scene::AudioSourceComponent{
                .clipAssetId = 505,
                .volume = 0.35F,
                .pitch = 1.25F,
                .loop = true,
                .spatial = false,
                .autoplay = true,
                .enabled = false,
                .mute = true,
                .pan = -0.25F,
                .spatialBlend = 0.5F,
                .attenuationModel = kb::audio::AudioAttenuationModel::Linear,
                .minDistance = 2.0F,
                .maxDistance = 80.0F,
                .rolloff = 0.75F,
                .dopplerFactor = 0.4F,
            },
            .audioListener = kb::scene::AudioListenerComponent{
                .primary = false,
                .enabled = false,
            },
        },
    });

    const kb::scene::ScenePrefabHandle savedHandle = source.Prefabs().Register("RoundTrip\\Prefab", std::move(prefab));
    kb::tests::Require(savedHandle.IsValid(), "Prefab asset round-trip setup failed to register prefab");
    kb::tests::Require(source.Prefabs().Save(savedHandle, prefabPath), "Prefab asset save failed");
    kb::tests::Require(std::filesystem::exists(prefabPath), "Prefab asset save did not create a file");
    kb::tests::Require(!source.Prefabs().Save(kb::scene::ScenePrefabHandle{}, prefabPath), "Prefab asset save accepted an invalid handle");

    kb::scene::Scene target;
    const kb::scene::ScenePrefabHandle loadedHandle = target.Prefabs().Load(prefabPath);
    kb::tests::Require(loadedHandle.IsValid(), "Prefab asset load did not return a valid handle");
    kb::tests::Require(target.Prefabs().RegisteredCount() == 1, "Prefab asset load did not register exactly one prefab");
    const kb::scene::ScenePrefab loadedPrefab = target.Prefabs().Get(loadedHandle);
    kb::tests::Require(!loadedPrefab.Empty(), "Prefab asset get did not return loaded data");
    kb::tests::Require(loadedPrefab.Nodes()[rootNode].nestedPrefabGuid == "nested-template-guid", "Prefab asset did not preserve nested template guid");

    const kb::scene::ScenePrefabInstance instance = target.Prefabs().Instantiate(loadedHandle);
    kb::tests::Require(instance.ObjectCount() == 2, "Loaded prefab did not instantiate all nodes");
    kb::tests::Require(target.Entities().Name(instance.ObjectAt(rootNode)) == "Asset Root", "Loaded prefab root name was not preserved");
    kb::tests::Require(target.Entities().Name(instance.ObjectAt(childNode)) == "Asset Child\\Escaped", "Loaded prefab escaped child name was not preserved");
    kb::tests::Require(target.Hierarchy().Parent(instance.ObjectAt(childNode).Entity()) == instance.ObjectAt(rootNode).Entity(), "Loaded prefab hierarchy was not preserved");
    kb::tests::Require(!target.Components().Visibility().Get(instance.ObjectAt(childNode).Entity()).visible, "Loaded prefab visibility was not preserved");

    const kb::scene::MeshRendererComponent* meshRenderer = target.Components().MeshRenderers().TryGet(instance.ObjectAt(rootNode).Entity());
    const kb::scene::InputComponent* input = target.Components().Inputs().TryGet(instance.ObjectAt(rootNode).Entity());
    const kb::scene::RigidbodyComponent* rigidbody = target.Components().Rigidbodies().TryGet(instance.ObjectAt(rootNode).Entity());
    const kb::scene::ColliderComponent* collider = target.Components().Colliders().TryGet(instance.ObjectAt(rootNode).Entity());
    const kb::scene::TagsComponent* tags = target.Components().Tags().TryGet(instance.ObjectAt(rootNode).Entity());
    const kb::scene::LightComponent* light = target.Components().Lights().TryGet(instance.ObjectAt(childNode).Entity());
    const kb::scene::BehaviourComponent* behaviour = target.Components().Behaviours().TryGet(instance.ObjectAt(childNode).Entity());
    const kb::scene::AudioSourceComponent* audioSource = target.Components().AudioSources().TryGet(instance.ObjectAt(childNode).Entity());
    const kb::scene::AudioListenerComponent* audioListener = target.Components().AudioListeners().TryGet(instance.ObjectAt(childNode).Entity());
    kb::tests::Require(meshRenderer != nullptr && meshRenderer->meshAssetId == 101 && !meshRenderer->castsShadow, "Loaded prefab mesh renderer was not preserved");
    kb::tests::Require(input != nullptr && input->mappingContextAssetId == 303 && input->priority == -4 && !input->enabled, "Loaded prefab input component was not preserved");
    kb::tests::Require(rigidbody != nullptr && rigidbody->bodyType == kb::scene::RigidbodyBodyType::Kinematic && kb::tests::NearlyEqual(rigidbody->mass, 8.0F) && kb::tests::NearlyEqual(rigidbody->linearVelocity.z, 3.0F) && !rigidbody->useGravity && rigidbody->lockRotation, "Loaded prefab rigidbody was not preserved");
    kb::tests::Require(collider != nullptr && collider->shape == kb::scene::ColliderShape::Capsule && kb::tests::NearlyEqual(collider->center.y, 0.2F) && kb::tests::NearlyEqual(collider->boxSize.z, 4.0F) && kb::tests::NearlyEqual(collider->radius, 1.25F) && collider->trigger, "Loaded prefab collider was not preserved");
    kb::tests::Require(tags != nullptr && kb::scene::TagsText(*tags) == "Player, Runtime", "Loaded prefab tags were not preserved");
    kb::tests::Require(light != nullptr && light->kind == kb::scene::LightKind::Spot && kb::tests::NearlyEqual(light->intensity, 6.0F), "Loaded prefab light was not preserved");
    kb::tests::Require(light != nullptr && kb::tests::NearlyEqual(light->areaWidth, 2.0F) && kb::tests::NearlyEqual(light->areaHeight, 0.5F), "Loaded prefab light area size was not preserved");
    kb::tests::Require(light != nullptr && kb::tests::NearlyEqual(light->contactShadowLength, 0.3F) && kb::tests::NearlyEqual(light->volumetricScattering, 0.2F), "Loaded prefab light production controls were not preserved");
    kb::tests::Require(light != nullptr && !light->castsShadow, "Loaded prefab light shadow flag was not preserved");
    kb::tests::Require(behaviour != nullptr && behaviour->behaviourAssetId == 404 && behaviour->backend == kb::scene::BehaviourBackend::Lua && !behaviour->enabled && behaviour->tickGroup == kb::scene::BehaviourTickGroup::Physics && behaviour->executionOrder == -12, "Loaded prefab behaviour was not preserved");
    kb::tests::Require(audioSource != nullptr && audioSource->clipAssetId == 505 && kb::tests::NearlyEqual(audioSource->volume, 0.35F) && kb::tests::NearlyEqual(audioSource->pitch, 1.25F) && audioSource->loop && !audioSource->spatial && audioSource->autoplay && !audioSource->enabled && audioSource->mute && audioSource->attenuationModel == kb::audio::AudioAttenuationModel::Linear && kb::tests::NearlyEqual(audioSource->maxDistance, 80.0F), "Loaded prefab audio source was not preserved");
    kb::tests::Require(audioListener != nullptr && !audioListener->primary && !audioListener->enabled, "Loaded prefab audio listener was not preserved");

    [[maybe_unused]] const bool progressed = target.Runtime().Update(0.016F);
    const kb::scene::TransformComponent childTransform = target.Transforms().Get(instance.ObjectAt(childNode));
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.x, 3.0F), "Loaded prefab world X was not rebuilt");
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.y, 4.0F), "Loaded prefab world Y was not rebuilt");

    std::filesystem::remove(prefabPath, removeError);
}

void RunPrefabCreateAssetRegistersSourceInstanceTest() {
    const std::filesystem::path prefabPath = std::filesystem::temp_directory_path() / "21kb_engine_prefab_create_asset_instance.kbprefab";
    std::error_code removeError;
    std::filesystem::remove(prefabPath, removeError);

    kb::scene::Scene scene;
    const kb::scene::SceneObject root = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Prefab Root" });
    const kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Prefab Child", .parent = root });

    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().CreateAsset(root, "CreatedPrefab", prefabPath);
    kb::tests::Require(handle.IsValid(), "CreateAsset should return a valid prefab handle");
    kb::tests::Require(std::filesystem::exists(prefabPath), "CreateAsset should write the prefab asset");

    const kb::scene::ScenePrefabInstanceHandle rootInstance = scene.Prefabs().RootInstance(root);
    kb::tests::Require(rootInstance.IsValid(), "CreateAsset should register the source root as a prefab instance");
    kb::tests::Require(scene.Prefabs().IsInstance(rootInstance), "CreateAsset source instance should be tracked by ScenePrefabs");

    std::uint32_t rootNodeIndex = 99;
    const kb::scene::ScenePrefabInstanceHandle containingRoot = scene.Prefabs().ContainingInstance(root, rootNodeIndex);
    kb::tests::Require(containingRoot == rootInstance && rootNodeIndex == 0, "CreateAsset source root should map to prefab node 0");

    std::uint32_t childNodeIndex = 99;
    const kb::scene::ScenePrefabInstanceHandle containingChild = scene.Prefabs().ContainingInstance(child, childNodeIndex);
    kb::tests::Require(containingChild == rootInstance && childNodeIndex == 1, "CreateAsset source child should be tracked as part of the prefab instance");

    const kb::scene::ScenePrefabOverrideReport overrides = scene.Prefabs().Overrides(rootInstance);
    kb::tests::Require(overrides.properties.empty(), "Newly created source prefab instance should not report immediate property overrides");

    std::filesystem::remove(prefabPath, removeError);
}

void RunPrefabVariantAssetRoundTripTest() {
    const std::filesystem::path basePath = std::filesystem::temp_directory_path() / "21kb_engine_prefab_variant_base.kbprefab";
    const std::filesystem::path variantPath = std::filesystem::temp_directory_path() / "21kb_engine_prefab_variant_roundtrip.kbprefab";
    std::error_code removeError;
    std::filesystem::remove(basePath, removeError);
    std::filesystem::remove(variantPath, removeError);

    kb::scene::Scene source;
    kb::scene::ScenePrefab basePrefab;
    const std::uint32_t rootNode = basePrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Variant Asset Base",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F } },
        .visibility = kb::scene::VisibilityComponent{ .visible = true },
    });
    const kb::scene::ScenePrefabHandle baseHandle = source.Prefabs().Register("VariantAssetBase", std::move(basePrefab));
    kb::tests::Require(baseHandle.IsValid(), "Variant asset base registration failed");

    std::vector<kb::scene::ScenePrefabPropertyOverride> overrides{
        kb::scene::ScenePrefabPropertyOverride{
            .nodeIndex = rootNode,
            .propertyPath = "name",
            .value = "Variant Asset Root",
            .flag = kb::scene::ScenePrefabOverrideFlag::Name,
        },
        kb::scene::ScenePrefabPropertyOverride{
            .nodeIndex = rootNode,
            .propertyPath = "transform.localPosition",
            .value = "9 0 0",
            .flag = kb::scene::ScenePrefabOverrideFlag::Transform,
        },
    };
    const kb::scene::ScenePrefabHandle variantHandle = source.Prefabs().RegisterVariant("VariantAsset", baseHandle, std::move(overrides));
    kb::tests::Require(variantHandle.IsValid(), "Variant asset registration failed");
    kb::tests::Require(source.Prefabs().Save(baseHandle, basePath), "Variant base asset save failed");
    kb::tests::Require(source.Prefabs().Save(variantHandle, variantPath), "Variant asset save failed");

    kb::scene::Scene target;
    const kb::scene::ScenePrefabHandle loadedBase = target.Prefabs().Load(basePath);
    const kb::scene::ScenePrefabHandle loadedVariant = target.Prefabs().Load(variantPath);
    kb::tests::Require(loadedBase.IsValid(), "Variant base asset load failed");
    kb::tests::Require(loadedVariant.IsValid(), "Variant asset load failed");

    const kb::scene::ScenePrefabInstance instance = target.Prefabs().Instantiate(loadedVariant);
    kb::tests::Require(instance.ObjectCount() == 1, "Loaded variant asset did not instantiate");
    kb::tests::Require(target.Entities().Name(instance.ObjectAt(rootNode)) == "Variant Asset Root", "Loaded variant asset did not preserve name override");
    const kb::scene::TransformComponent transform = target.Transforms().Get(instance.ObjectAt(rootNode));
    kb::tests::Require(kb::tests::NearlyEqual(transform.localPosition.x, 9.0F), "Loaded variant asset did not preserve transform override");

    std::filesystem::remove(basePath, removeError);
    std::filesystem::remove(variantPath, removeError);
}

void RunNestedPrefabAssetRoundTripTest() {
    const std::filesystem::path innerPath = std::filesystem::temp_directory_path() / "21kb_engine_nested_inner.kbprefab";
    const std::filesystem::path outerPath = std::filesystem::temp_directory_path() / "21kb_engine_nested_outer.kbprefab";
    std::error_code removeError;
    std::filesystem::remove(innerPath, removeError);
    std::filesystem::remove(outerPath, removeError);

    kb::scene::Scene source;
    kb::scene::ScenePrefab innerPrefab;
    const std::uint32_t innerRoot = innerPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Nested Asset Inner" });
    const std::uint32_t innerChild = innerPrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Nested Asset Child",
        .parentNode = innerRoot,
    });
    const kb::scene::ScenePrefabHandle innerHandle = source.Prefabs().Register("NestedAssetInner", std::move(innerPrefab));
    kb::tests::Require(source.Prefabs().Save(innerHandle, innerPath), "Nested inner prefab save failed");

    kb::scene::SceneObject outerRoot = source.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Nested Asset Outer" });
    const kb::scene::ScenePrefabInstance nestedInstance = source.Prefabs().Instantiate(
        innerHandle,
        kb::scene::ScenePrefabInstantiationSettings{ .parent = outerRoot });
    source.Entities().SetName(nestedInstance.ObjectAt(innerChild), "Nested Asset Child Override");
    const kb::scene::ScenePrefabHandle outerHandle = source.Prefabs().CreateAsset(outerRoot, "NestedAssetOuter", outerPath);
    kb::tests::Require(outerHandle.IsValid(), "Nested outer prefab create asset failed");

    kb::scene::Scene target;
    const kb::scene::ScenePrefabHandle loadedInner = target.Prefabs().Load(innerPath);
    const kb::scene::ScenePrefabHandle loadedOuter = target.Prefabs().Load(outerPath);
    kb::tests::Require(loadedInner.IsValid(), "Nested inner prefab load failed");
    kb::tests::Require(loadedOuter.IsValid(), "Nested outer prefab load failed");
    const kb::scene::ScenePrefabInstance instance = target.Prefabs().Instantiate(loadedOuter);
    kb::tests::Require(instance.ObjectCount() == 3, "Loaded nested outer prefab did not compose inner hierarchy");
    kb::tests::Require(target.Entities().Name(instance.ObjectAt(2)) == "Nested Asset Child Override", "Loaded nested outer prefab did not preserve nested override");

    std::filesystem::remove(innerPath, removeError);
    std::filesystem::remove(outerPath, removeError);
}

} // namespace

namespace kb::tests {

void RunScenePrefabCaptureTests() {
    RunPrefabCaptureTest();
    RunPrefabAssetRoundTripTest();
    RunPrefabCreateAssetRegistersSourceInstanceTest();
    RunPrefabVariantAssetRoundTripTest();
    RunNestedPrefabAssetRoundTripTest();
}

} // namespace kb::tests
