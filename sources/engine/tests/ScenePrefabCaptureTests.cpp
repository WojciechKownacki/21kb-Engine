#include "ScenePrefabTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabCaptureSettings.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

void TracePrefabCaptureTest(const char* name) {
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
        .layer = 0x00000008U,
    });
    source.Components().Cameras().Set(child.Entity(), kb::scene::CameraComponent{
        .projection = kb::scene::CameraProjection::Orthographic,
        .orthographicHeight = 12.0F,
        .primary = true,
        .viewportId = 3U,
        .priority = 5,
        .cullingMask = 0x00000005U,
        .clearMode = kb::scene::CameraClearMode::SolidColor,
        .clearColor = kb::scene::Vec3{ 0.9F, 0.1F, 0.2F },
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
    kb::tests::Require(capturedMeshRenderer != nullptr && capturedMeshRenderer->meshAssetId == 17 && capturedMeshRenderer->layer == 0x00000008U, "Captured mesh renderer was not preserved");
    kb::tests::Require(capturedCamera != nullptr && capturedCamera->orthographicHeight == 12.0F && capturedCamera->viewportId == 3U && capturedCamera->priority == 5
            && capturedCamera->cullingMask == 0x00000005U && capturedCamera->clearMode == kb::scene::CameraClearMode::SolidColor
            && capturedCamera->clearColor.x == 0.9F && capturedCamera->clearColor.y == 0.1F && capturedCamera->clearColor.z == 0.2F,
        "Captured camera was not preserved");
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
                .friction = 0.3F,
                .restitution = 0.7F,
                .layer = 8U,
            },
            .characterController = kb::scene::CharacterControllerComponent{
                .center = kb::scene::Vec3{ 0.4F, 0.5F, 0.6F },
                .radius = 0.35F,
                .height = 1.75F,
                .slopeLimitDegrees = 43.0F,
                .stepOffset = 0.25F,
                .gravityScale = 1.5F,
                .useGravity = false,
            },
        },
    });
    kb::scene::SetTagsText(prefab.TryGetMutableNode(rootNode)->components.tags.emplace(), "Player");
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
            .audioSource = [] {
                kb::scene::AudioSourceComponent audioSourceFixture{
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
                };
                // LIB-147: exercises the prefab TEXT writer/parser outputBus round-trip.
                kb::tests::Require(kb::scene::SetAudioSourceOutputBus(audioSourceFixture, "Ambience"),
                    "Prefab audio source bus fixture was invalid");
                return audioSourceFixture;
            }(),
            .audioListener = kb::scene::AudioListenerComponent{
                .priority = -6,
                .localUser = kb::input::LocalUserId{ 4U },
                .primary = false,
                .enabled = false,
            },
            .animator = kb::scene::Animator{
                .controllerAssetId = 606,
                .speed = 1.5F,
                .enabled = false,
                .rootMotionOwner = kb::scene::AnimatorRootMotionOwner::Rigidbody,
            },
        },
    });
    prefab.TryGetMutableNode(rootNode)->components.joint = kb::scene::ScenePrefabJointComponent{
        .type = kb::scene::JointType::Hinge,
        .connectedNodeStableId = prefab.Nodes()[childNode].stableId,
        .anchor = kb::scene::Vec3{ 0.1F, 0.2F, 0.3F },
        .connectedAnchor = kb::scene::Vec3{ 0.4F, 0.5F, 0.6F },
        .axis = kb::scene::Vec3{ 0.0F, 0.0F, 1.0F },
        .minLimit = -20.0F,
        .maxLimit = 35.0F,
        .enableLimit = true,
    };

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
    const auto& prefabJoint = loadedPrefab.Nodes()[rootNode].components.joint;
    kb::tests::Require(prefabJoint.has_value() && prefabJoint->type == kb::scene::JointType::Hinge && prefabJoint->connectedNodeStableId == loadedPrefab.Nodes()[childNode].stableId && kb::tests::NearlyEqual(prefabJoint->anchor.y, 0.2F) && kb::tests::NearlyEqual(prefabJoint->connectedAnchor.z, 0.6F) && kb::tests::NearlyEqual(prefabJoint->minLimit, -20.0F) && kb::tests::NearlyEqual(prefabJoint->maxLimit, 35.0F) && prefabJoint->enableLimit, "Loaded prefab joint definition was not preserved");

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
    const kb::scene::CharacterControllerComponent* characterController = target.Components().CharacterControllers().TryGet(instance.ObjectAt(rootNode).Entity());
    const kb::scene::JointComponent* joint = target.Components().Joints().TryGet(instance.ObjectAt(rootNode).Entity());
    const kb::scene::TagsComponent* tags = target.Components().Tags().TryGet(instance.ObjectAt(rootNode).Entity());
    const kb::scene::LightComponent* light = target.Components().Lights().TryGet(instance.ObjectAt(childNode).Entity());
    const kb::scene::BehaviourComponent* behaviour = target.Components().Behaviours().TryGet(instance.ObjectAt(childNode).Entity());
    const kb::scene::AudioSourceComponent* audioSource = target.Components().AudioSources().TryGet(instance.ObjectAt(childNode).Entity());
    const kb::scene::AudioListenerComponent* audioListener = target.Components().AudioListeners().TryGet(instance.ObjectAt(childNode).Entity());
    const kb::scene::Animator* animator = target.Components().Animators().TryGet(instance.ObjectAt(childNode).Entity());
    kb::tests::Require(meshRenderer != nullptr && meshRenderer->meshAssetId == 101 && !meshRenderer->castsShadow, "Loaded prefab mesh renderer was not preserved");
    kb::tests::Require(input != nullptr && input->mappingContextAssetId == 303 && input->priority == -4 && !input->enabled, "Loaded prefab input component was not preserved");
    kb::tests::Require(rigidbody != nullptr && rigidbody->bodyType == kb::scene::RigidbodyBodyType::Kinematic && kb::tests::NearlyEqual(rigidbody->mass, 8.0F) && kb::tests::NearlyEqual(rigidbody->linearVelocity.z, 3.0F) && !rigidbody->useGravity && rigidbody->lockRotation, "Loaded prefab rigidbody was not preserved");
    kb::tests::Require(collider != nullptr && collider->shape == kb::scene::ColliderShape::Capsule && kb::tests::NearlyEqual(collider->center.y, 0.2F) && kb::tests::NearlyEqual(collider->boxSize.z, 4.0F) && kb::tests::NearlyEqual(collider->radius, 1.25F) && collider->trigger && kb::tests::NearlyEqual(collider->friction, 0.3F) && kb::tests::NearlyEqual(collider->restitution, 0.7F) && collider->layer == 8U, "Loaded prefab collider material/layer was not preserved");
    kb::tests::Require(characterController != nullptr && kb::tests::NearlyEqual(characterController->center.z, 0.6F) && kb::tests::NearlyEqual(characterController->radius, 0.35F) && kb::tests::NearlyEqual(characterController->height, 1.75F) && kb::tests::NearlyEqual(characterController->slopeLimitDegrees, 43.0F) && kb::tests::NearlyEqual(characterController->stepOffset, 0.25F) && kb::tests::NearlyEqual(characterController->gravityScale, 1.5F) && !characterController->useGravity, "Loaded prefab CharacterController was not preserved");
    kb::tests::Require(joint != nullptr && joint->type == kb::scene::JointType::Hinge && joint->connectedEntity == instance.ObjectAt(childNode).Entity() && kb::tests::NearlyEqual(joint->anchor.y, 0.2F) && kb::tests::NearlyEqual(joint->connectedAnchor.z, 0.6F) && kb::tests::NearlyEqual(joint->minLimit, -20.0F) && kb::tests::NearlyEqual(joint->maxLimit, 35.0F) && joint->enableLimit, "Loaded prefab joint did not resolve its connected entity");
    kb::tests::Require(tags != nullptr && kb::scene::TagsText(*tags) == "Player", "Loaded prefab tag was not preserved");
    kb::tests::Require(light != nullptr && light->kind == kb::scene::LightKind::Spot && kb::tests::NearlyEqual(light->intensity, 6.0F), "Loaded prefab light was not preserved");
    kb::tests::Require(light != nullptr && kb::tests::NearlyEqual(light->areaWidth, 2.0F) && kb::tests::NearlyEqual(light->areaHeight, 0.5F), "Loaded prefab light area size was not preserved");
    kb::tests::Require(light != nullptr && kb::tests::NearlyEqual(light->contactShadowLength, 0.3F) && kb::tests::NearlyEqual(light->volumetricScattering, 0.2F), "Loaded prefab light production controls were not preserved");
    kb::tests::Require(light != nullptr && !light->castsShadow, "Loaded prefab light shadow flag was not preserved");
    kb::tests::Require(behaviour != nullptr && behaviour->behaviourAssetId == 404 && behaviour->backend == kb::scene::BehaviourBackend::Lua && !behaviour->enabled && behaviour->tickGroup == kb::scene::BehaviourTickGroup::Physics && behaviour->executionOrder == -12, "Loaded prefab behaviour was not preserved");
    kb::tests::Require(audioSource != nullptr && audioSource->clipAssetId == 505 && kb::tests::NearlyEqual(audioSource->volume, 0.35F) && kb::tests::NearlyEqual(audioSource->pitch, 1.25F) && audioSource->loop && !audioSource->spatial && audioSource->autoplay && !audioSource->enabled && audioSource->mute && audioSource->attenuationModel == kb::audio::AudioAttenuationModel::Linear && kb::tests::NearlyEqual(audioSource->maxDistance, 80.0F) && kb::scene::AudioSourceOutputBus(*audioSource) == "Ambience", "Loaded prefab audio source was not preserved");
    kb::tests::Require(audioListener != nullptr && audioListener->priority == -6
            && audioListener->localUser == kb::input::LocalUserId{ 4U }
            && !audioListener->primary && !audioListener->enabled,
        "Loaded prefab audio listener was not preserved");
    kb::tests::Require(animator != nullptr && animator->controllerAssetId == 606 &&
            kb::tests::NearlyEqual(animator->speed, 1.5F) && !animator->enabled &&
            animator->rootMotionOwner == kb::scene::AnimatorRootMotionOwner::Rigidbody,
        "Loaded prefab Animator was not preserved");

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

// A prefab created from an instance root nests the old prefab and takes the instance over; one created
// from an instance child takes that subtree out of the outer instance. Either way every object keeps
// exactly one owner.
void RunPrefabCreateAssetFromInstanceKeepsOneOwnerTest() {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "21kb_engine_prefab_one_owner";
    std::error_code removeError;
    std::filesystem::remove_all(directory, removeError);
    std::filesystem::create_directories(directory);

    kb::scene::Scene scene;
    const kb::scene::SceneObject root = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Owner Root" });
    const kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Owner Child", .parent = root });
    const kb::scene::ScenePrefabHandle inner = scene.Prefabs().CreateAsset(root, "Inner", directory / "Inner.kbprefab");
    const kb::scene::ScenePrefabInstanceHandle innerInstance = scene.Prefabs().RootInstance(root);
    kb::tests::Require(inner.IsValid() && innerInstance.IsValid(), "One-owner setup did not create the inner prefab instance");

    const kb::scene::ScenePrefabHandle outer = scene.Prefabs().CreateAsset(root, "Outer", directory / "Outer.kbprefab");
    const kb::scene::ScenePrefabInstanceHandle outerInstance = scene.Prefabs().RootInstance(root);
    kb::tests::Require(outer.IsValid() && outerInstance.IsValid() && scene.Prefabs().SourcePrefab(outerInstance) == outer,
        "Prefab created from an instance root did not take the instance over");
    kb::tests::Require(!scene.Prefabs().IsInstance(innerInstance), "Prefab created from an instance root left the old instance record");
    kb::tests::Require(scene.Prefabs().RefreshInstances(inner) == 0U, "Old prefab still lists an instance after its root was taken over");
    std::uint32_t childNode = 99U;
    kb::tests::Require(scene.Prefabs().ContainingInstance(child, childNode) == outerInstance && childNode == 1U, "Instance child did not move to the new prefab instance");
    kb::tests::Require(scene.Prefabs().Get(outer).Nodes()[0].nestedPrefabGuid == scene.Prefabs().Guid(inner), "Prefab created from an instance root does not nest the old prefab");

    const kb::scene::ScenePrefabHandle part = scene.Prefabs().CreateAsset(child, "Part", directory / "Part.kbprefab");
    const kb::scene::ScenePrefabInstanceHandle partInstance = scene.Prefabs().RootInstance(child);
    kb::tests::Require(part.IsValid() && partInstance.IsValid(), "Prefab created from an instance child is not linked");
    kb::tests::Require(scene.Prefabs().ContainingInstance(child, childNode) == partInstance && childNode == 0U, "Instance child still belongs to the outer instance");
    kb::tests::Require(scene.Prefabs().RootInstance(root) == outerInstance, "Outer instance lost its root when a child became a prefab");
    bool childMissing = false;
    bool childAdded = false;
    for (const kb::scene::ScenePrefabPropertyOverride& property : scene.Prefabs().Overrides(outerInstance).properties) {
        childMissing = childMissing || (property.nodeIndex == 1U && property.flag == kb::scene::ScenePrefabOverrideFlag::MissingObject);
        childAdded = childAdded || (property.nodeIndex == 0U && property.flag == kb::scene::ScenePrefabOverrideFlag::AddedChild);
    }
    kb::tests::Require(childMissing && childAdded, "Outer instance does not report the child it gave up to the new prefab");

    std::filesystem::remove_all(directory, removeError);
}

// A .kbprefab changed on disk by someone else is the same asset: loading it again keeps the guid stored
// in the file and the loaded prefab, and RefreshInstances brings existing instances to the new content.
void RunPrefabReloadOfChangedFileKeepsGuidTest() {
    const std::filesystem::path prefabPath = std::filesystem::temp_directory_path() / "21kb_engine_prefab_reload_changed.kbprefab";
    std::error_code removeError;
    std::filesystem::remove(prefabPath, removeError);

    kb::scene::Scene scene;
    const kb::scene::SceneObject root = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Barrel" });
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Band", .parent = root }));
    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().CreateAsset(root, "Barrel", prefabPath);
    kb::tests::Require(handle.IsValid(), "Reload setup did not create the prefab asset");
    const std::string guid = scene.Prefabs().Guid(handle);
    const kb::scene::ScenePrefabInstanceHandle instance = scene.Prefabs().RootInstance(root);

    {
        kb::scene::Scene writer;
        const kb::scene::ScenePrefabHandle written = writer.Prefabs().Load(prefabPath);
        const kb::scene::ScenePrefabInstance writerInstance = writer.Prefabs().Instantiate(written);
        kb::scene::TransformComponent transform = writer.Transforms().Get(writerInstance.RootObject());
        transform.localPosition.x = 4.0F;
        writer.Transforms().Set(writerInstance.RootObject(), transform);
        kb::tests::Require(writer.Prefabs().ApplyOverrides(writerInstance.Handle(), prefabPath), "Reload setup did not write the changed prefab");
    }

    const kb::scene::ScenePrefabHandle reloaded = scene.Prefabs().Load(prefabPath);
    kb::tests::Require(reloaded == handle && scene.Prefabs().Guid(reloaded) == guid, "Reloading a changed prefab file gave it a new identity");
    kb::tests::Require(scene.Prefabs().RegisteredCount() == 1U, "Reloading a changed prefab file registered a second prefab");
    kb::tests::Require(scene.Prefabs().RefreshInstances(reloaded) == 1U, "Existing instance was not refreshed from the changed prefab");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(root).localPosition.x, 4.0F), "Existing instance did not take the changed prefab content");
    kb::tests::Require(scene.Prefabs().Overrides(instance).properties.empty(), "Refreshed instance reports the new prefab content as overrides");

    std::filesystem::remove(prefabPath, removeError);
}

// Spawning a captured prefab at runtime makes plain objects; only document loads and editor restores
// ask for the instances named in it to be linked to their prefabs again.
void RunRuntimeSpawnDoesNotLinkPrefabInstancesTest() {
    kb::scene::Scene scene;
    kb::scene::ScenePrefab fx;
    static_cast<void>(fx.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Fx" }));
    const kb::scene::ScenePrefabHandle fxHandle = scene.Prefabs().Register("Fx", std::move(fx));
    kb::scene::ScenePrefab bullet;
    const std::uint32_t bulletRoot = bullet.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Bullet" });
    const std::uint32_t bulletFx = bullet.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Fx", .nestedPrefabGuid = scene.Prefabs().Guid(fxHandle), .parentNode = bulletRoot });

    const kb::scene::ScenePrefabInstance spawned = scene.Prefabs().Instantiate(bullet);
    kb::tests::Require(spawned.ObjectCount() == 2U && !scene.Prefabs().RootInstance(spawned.ObjectAt(bulletFx)).IsValid(),
        "A runtime spawn created a prefab instance record");
    const kb::scene::ScenePrefabInstance restored = scene.Prefabs().Instantiate(bullet, kb::scene::ScenePrefabInstantiationSettings{ .linkPrefabInstances = true });
    kb::tests::Require(scene.Prefabs().SourcePrefab(scene.Prefabs().RootInstance(restored.ObjectAt(bulletFx))) == fxHandle,
        "A linked instantiation did not link the nested prefab instance");
}

// A destroyed instance child leaves a missing node behind. The entity that reuses its slot is not that
// node, and an undone delete still finds its node after the slot was reused in between.
void RunDestroyedInstanceChildSlotReuseTest() {
    kb::scene::Scene scene;
    kb::scene::ScenePrefab prefab;
    const std::uint32_t rootNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Crate" });
    const std::uint32_t lidNode = prefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Lid", .parentNode = rootNode });
    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().Register("Crate", std::move(prefab));
    const kb::scene::ScenePrefabInstance crate = scene.Prefabs().Instantiate(handle);
    const kb::scene::SceneEntity lid = crate.ObjectAt(lidNode).Entity();

    scene.Entities().Destroy(lid);
    const kb::scene::SceneObject plain = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Plain" });
    kb::tests::Require((plain.Entity().Id() & 0xFFFFFFFFULL) == (lid.Id() & 0xFFFFFFFFULL), "Slot reuse setup: the new object did not reuse the destroyed child's slot");
    std::uint32_t node = 99U;
    kb::tests::Require(!scene.Prefabs().ContainingInstance(plain, node).IsValid(), "An entity reusing a destroyed instance child's slot was taken for the prefab node");

    const kb::scene::ScenePrefabInstance other = scene.Prefabs().Instantiate(handle);
    scene.Entities().Destroy(other.RootObject());
    const kb::scene::SceneObject restored = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Lid", .parent = crate.RootObject() });
    const std::array<kb::scene::SceneEntity, 1> destroyed{ lid };
    const std::array<kb::scene::SceneObject, 1> recreated{ restored };
    scene.Prefabs().RelinkRestoredObjects(destroyed, recreated);
    kb::tests::Require(scene.Prefabs().ContainingInstance(restored, node) == crate.Handle() && node == lidNode,
        "An undone delete was not put back into its instance after the slot was reused");
}

// Objects keep their prefab node through a save and reopen by identity, not by name or place: an
// added child named like a prefab node stays added, and a node moved inside the instance stays its node.
void RequireCrateNodeIdentity(kb::scene::Scene& scene, const char* phase) {
    const std::string prefix = std::string{ phase } + ": ";
    const kb::scene::SceneObject crate = scene.Hierarchy().RootObjects().front();
    const kb::scene::ScenePrefabInstanceHandle instance = scene.Prefabs().RootInstance(crate);
    kb::tests::Require(instance.IsValid(), (prefix + "crate lost its prefab link").c_str());
    bool originalLid = false;
    bool addedLid = false;
    bool movedHinge = false;
    for (const kb::scene::SceneObject child : scene.Hierarchy().Children(crate)) {
        std::uint32_t node = 99U;
        const kb::scene::ScenePrefabInstanceHandle owner = scene.Prefabs().ContainingInstance(child, node);
        const float x = scene.Transforms().Get(child).localPosition.x;
        originalLid = originalLid || (scene.Entities().Name(child) == "Lid" && x == 1.0F && owner == instance && node == 1U);
        addedLid = addedLid || (scene.Entities().Name(child) == "Lid" && x == 9.0F && !owner.IsValid());
        movedHinge = movedHinge || (scene.Entities().Name(child) == "Hinge" && owner == instance && node == 2U);
    }
    kb::tests::Require(originalLid, (prefix + "the original Lid is not prefab node 1").c_str());
    kb::tests::Require(addedLid, (prefix + "the added child named Lid was taken for a prefab node").c_str());
    kb::tests::Require(movedHinge, (prefix + "the Hinge moved inside the instance fell out of it").c_str());
}

void RunPrefabInstanceNodeIdentitySurvivesReopenTest() {
    const std::filesystem::path projectRoot = std::filesystem::temp_directory_path() / "21kb_engine_prefab_identity_project";
    std::error_code removeError;
    std::filesystem::remove_all(projectRoot, removeError);
    std::filesystem::create_directories(projectRoot / "Assets");
    const std::filesystem::path scenePath = projectRoot / "Assets" / "Identity.21kbscene";
    {
        kb::scene::Scene scene;
        kb::tests::Require(scene.Assets().MountProject(projectRoot), "Identity project mount failed");
        const kb::scene::SceneObject crate = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Crate" });
        const kb::scene::SceneObject lid = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Lid", .parent = crate, .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F } } });
        const kb::scene::SceneObject hinge = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Hinge", .parent = lid });
        kb::tests::Require(scene.Prefabs().CreateAsset(crate, "Crate", projectRoot / "Assets" / "Crate.kbprefab").IsValid(), "Identity prefab was not created");

        static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Lid", .parent = crate, .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 9.0F, 0.0F, 0.0F } } }));
        kb::tests::Require(scene.Hierarchy().SetParent(lid, kb::scene::SceneObject{}) && scene.Hierarchy().SetParent(lid, crate) &&
            scene.Hierarchy().SetParent(hinge, crate), "Identity setup could not move the instance nodes");
        RequireCrateNodeIdentity(scene, "before save");
        kb::tests::Require(kb::scene::SceneDocumentService::Save(scene, scenePath, "Identity"), "Identity scene was not saved");

        const kb::scene::SceneDocument document = kb::scene::SceneDocumentService::Capture(scene, "Identity");
        kb::tests::Require(kb::scene::SceneDocumentService::LoadIntoScene(scene, document), "Identity document did not reload in place");
        RequireCrateNodeIdentity(scene, "in-place reload");
    }
    {
        kb::scene::Scene reopened;
        kb::tests::Require(reopened.Assets().MountProject(projectRoot), "Identity reopen project mount failed");
        static_cast<void>(reopened.Assets().Discover());
        kb::tests::Require(kb::scene::SceneDocumentService::LoadFileIntoScene(reopened, scenePath), "Identity scene did not reopen");
        RequireCrateNodeIdentity(reopened, "reopen");
    }
    std::filesystem::remove_all(projectRoot, removeError);
}

// A scene keeps the prefab content of the day it was saved. Reopened after the prefab changed, its
// instance shows the prefab as it is now plus only its own overrides, so Apply cannot undo the change.
void RunReopenedInstanceFollowsChangedPrefabTest() {
    const std::filesystem::path projectRoot = std::filesystem::temp_directory_path() / "21kb_engine_prefab_rebase_project";
    std::error_code removeError;
    std::filesystem::remove_all(projectRoot, removeError);
    std::filesystem::create_directories(projectRoot / "Assets");
    const std::filesystem::path prefabPath = projectRoot / "Assets" / "Crate.kbprefab";
    const std::filesystem::path scenePath = projectRoot / "Assets" / "Rebase.21kbscene";
    {
        kb::scene::Scene scene;
        kb::tests::Require(scene.Assets().MountProject(projectRoot), "Rebase project mount failed");
        const kb::scene::SceneObject crate = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Crate" });
        static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Lid", .parent = crate }));
        kb::tests::Require(scene.Prefabs().CreateAsset(crate, "Crate", prefabPath).IsValid(), "Rebase prefab was not created");
        kb::scene::TransformComponent transform = scene.Transforms().Get(crate);
        transform.localPosition.z = 5.0F;
        scene.Transforms().Set(crate, transform);
        kb::tests::Require(kb::scene::SceneDocumentService::Save(scene, scenePath, "Rebase"), "Rebase scene was not saved");
    }
    {
        kb::scene::Scene editor;
        const kb::scene::ScenePrefabInstance edited = editor.Prefabs().Instantiate(editor.Prefabs().Load(prefabPath));
        kb::scene::TransformComponent lid = editor.Transforms().Get(edited.ObjectAt(1U));
        lid.localPosition.x = 4.0F;
        editor.Transforms().Set(edited.ObjectAt(1U), lid);
        static_cast<void>(editor.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Handle", .parent = edited.RootObject() }));
        kb::tests::Require(editor.Prefabs().ApplyOverrides(edited.Handle(), prefabPath), "Rebase setup could not change the prefab");
    }
    kb::scene::Scene reopened;
    kb::tests::Require(reopened.Assets().MountProject(projectRoot), "Rebase reopen project mount failed");
    static_cast<void>(reopened.Assets().Discover());
    kb::tests::Require(kb::scene::SceneDocumentService::LoadFileIntoScene(reopened, scenePath), "Rebase scene did not reopen");
    const kb::scene::SceneObject crate = reopened.Hierarchy().RootObjects().front();
    const kb::scene::ScenePrefabInstanceHandle instance = reopened.Prefabs().RootInstance(crate);
    std::vector<std::string> names;
    float lidX = 0.0F;
    for (const kb::scene::SceneObject child : reopened.Hierarchy().Children(crate)) {
        names.push_back(reopened.Entities().Name(child));
        if (names.back() == "Lid") {
            lidX = reopened.Transforms().Get(child).localPosition.x;
        }
    }
    kb::tests::Require(instance.IsValid() && lidX == 4.0F, "Reopened instance did not take the prefab's changed value");
    kb::tests::Require(std::ranges::count(names, std::string{ "Handle" }) == 1, "Reopened instance did not get the node the prefab gained");
    kb::tests::Require(reopened.Transforms().Get(crate).localPosition.z == 5.0F, "Reopened instance lost its own override");
    const kb::scene::ScenePrefabOverrideReport overrides = reopened.Prefabs().Overrides(instance);
    kb::tests::Require(overrides.properties.size() == 1U && overrides.properties.front().nodeIndex == 0U,
        "Reopened instance reports the prefab's own change as an override");
    std::filesystem::remove_all(projectRoot, removeError);
}

// A scene file keeps an instance only as the prefab guid on its root node. Reopening the file in a
// fresh scene, and reloading the captured document in place (how Play mode stops), must link the
// instance, its node mapping and its overrides again.
void RequireRelinkedCrate(kb::scene::Scene& scene, const std::string& prefabGuid, const char* phase) {
    const std::string prefix = std::string{ phase } + ": ";
    const std::vector<kb::scene::SceneObject> roots = scene.Hierarchy().RootObjects();
    kb::tests::Require(roots.size() == 1U && scene.Entities().Name(roots.front()) == "Crate", (prefix + "scene did not reload the crate root").c_str());
    const kb::scene::SceneObject root = roots.front();
    const kb::scene::ScenePrefabInstanceHandle instance = scene.Prefabs().RootInstance(root);
    kb::tests::Require(instance.IsValid(), (prefix + "crate root lost its prefab instance").c_str());
    kb::tests::Require(scene.Prefabs().Guid(scene.Prefabs().SourcePrefab(instance)) == prefabGuid, (prefix + "crate instance links the wrong prefab").c_str());

    const std::vector<kb::scene::SceneObject> children = scene.Hierarchy().Children(root);
    kb::tests::Require(children.size() == 2U, (prefix + "crate children were not reloaded").c_str());
    std::uint32_t lidNode = 99U;
    kb::tests::Require(scene.Entities().Name(children[0]) == "Open Lid" && scene.Prefabs().ContainingInstance(children[0], lidNode) == instance && lidNode == 1U,
        (prefix + "renamed lid did not map to its prefab node").c_str());
    std::uint32_t noteNode = 99U;
    kb::tests::Require(scene.Entities().Name(children[1]) == "Note" && !scene.Prefabs().ContainingInstance(children[1], noteNode).IsValid(),
        (prefix + "added child was linked as a prefab node").c_str());

    bool renamedLid = false;
    bool missingHinge = false;
    bool addedChild = false;
    for (const kb::scene::ScenePrefabPropertyOverride& property : scene.Prefabs().Overrides(instance).properties) {
        renamedLid = renamedLid || (property.nodeIndex == 1U && property.propertyPath == "name" && property.value == "Open Lid");
        missingHinge = missingHinge || (property.nodeIndex == 2U && property.flag == kb::scene::ScenePrefabOverrideFlag::MissingObject);
        addedChild = addedChild || (property.nodeIndex == 0U && property.flag == kb::scene::ScenePrefabOverrideFlag::AddedChild);
    }
    kb::tests::Require(renamedLid && missingHinge && addedChild, (prefix + "crate overrides were not kept").c_str());
}

void RunPrefabInstanceLinkSurvivesSceneReopenTest() {
    const std::filesystem::path projectRoot = std::filesystem::temp_directory_path() / "21kb_engine_prefab_relink_project";
    std::error_code removeError;
    std::filesystem::remove_all(projectRoot, removeError);
    std::filesystem::create_directories(projectRoot / "Assets");
    const std::filesystem::path prefabPath = projectRoot / "Assets" / "Crate.kbprefab";
    const std::filesystem::path scenePath = projectRoot / "Assets" / "Relink.21kbscene";

    std::string prefabGuid;
    {
        kb::scene::Scene scene;
        kb::tests::Require(scene.Assets().MountProject(projectRoot), "Relink project mount failed");
        const kb::scene::SceneObject root = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Crate" });
        const kb::scene::SceneObject lid = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Lid", .parent = root });
        const kb::scene::SceneObject hinge = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Hinge", .parent = lid });
        const kb::scene::ScenePrefabHandle prefab = scene.Prefabs().CreateAsset(root, "Crate", prefabPath);
        kb::tests::Require(prefab.IsValid(), "Relink prefab asset was not created");
        prefabGuid = scene.Prefabs().Guid(prefab);

        scene.Entities().SetName(lid, "Open Lid");
        scene.Entities().Destroy(hinge);
        static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Note", .parent = root }));
        kb::tests::Require(kb::scene::SceneDocumentService::Save(scene, scenePath, "Relink"), "Relink scene was not saved");

        const kb::scene::SceneDocument document = kb::scene::SceneDocumentService::Capture(scene, "Relink");
        kb::tests::Require(kb::scene::SceneDocumentService::LoadIntoScene(scene, document), "Relink document did not reload in place");
        RequireRelinkedCrate(scene, prefabGuid, "in-place reload");
    }
    {
        kb::scene::Scene reopened;
        kb::tests::Require(reopened.Assets().MountProject(projectRoot), "Relink reopen project mount failed");
        static_cast<void>(reopened.Assets().Discover());
        kb::tests::Require(reopened.Prefabs().RegisteredCount() == 0U, "Reopened scene should start without loaded prefabs");
        kb::tests::Require(kb::scene::SceneDocumentService::LoadFileIntoScene(reopened, scenePath), "Relink scene did not reopen");
        RequireRelinkedCrate(reopened, prefabGuid, "reopen");
    }

    std::filesystem::remove_all(projectRoot, removeError);
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

// LIB-092: the missing counterpart to RunPrefabVariantAssetRoundTripTest —
// proves a HIERARCHY override (reparenting a node WITHIN the same prefab
// instance) survives a real save+load round trip, not just an in-memory
// detect/revert cycle. Before this fix, ScenePrefabAssetVariantWriter wrote
// a raw runtime entity id for a "parent" override's new-parent target, and
// ScenePrefabAssetOverrideReader never read it back at all — a loaded
// "parent" override silently failed to reproduce the reparent. The fix
// resolves the new parent via a stable, within-instance node id (the same
// mechanism nodeId/ResolveNodeIndex already use for the override's own
// target), which — unlike a raw entity id — survives the round trip.
void RunPrefabParentOverrideAssetRoundTripTest() {
    const std::filesystem::path basePath = std::filesystem::temp_directory_path() / "21kb_engine_prefab_parent_override_base.kbprefab";
    const std::filesystem::path variantPath = std::filesystem::temp_directory_path() / "21kb_engine_prefab_parent_override_variant.kbprefab";
    std::error_code removeError;
    std::filesystem::remove(basePath, removeError);
    std::filesystem::remove(variantPath, removeError);

    kb::scene::Scene source;
    kb::scene::ScenePrefab basePrefab;
    const std::uint32_t rootNode = basePrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Parent Override Root" });
    const std::uint32_t siblingA = basePrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Parent Override Sibling A", .parentNode = rootNode });
    const std::uint32_t siblingB = basePrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Parent Override Sibling B", .parentNode = rootNode });
    const kb::scene::ScenePrefabHandle baseHandle = source.Prefabs().Register("ParentOverrideBase", std::move(basePrefab));
    kb::tests::Require(baseHandle.IsValid(), "Parent override base registration failed");

    // Instantiate, then reparent siblingB under siblingA — BOTH within this
    // same instance, the exact scenario this fix addresses (the new parent
    // is resolvable via a stable node id, unlike an entity outside the
    // instance, which stays honestly unresolved — see
    // RunPrefabApplyRejectsDetachedTrackedChildTest for that boundary).
    const kb::scene::ScenePrefabInstance instance = source.Prefabs().Instantiate(baseHandle);
    const kb::scene::ScenePrefabInstanceHandle instanceHandle = instance.Handle();
    kb::tests::Require(source.Hierarchy().SetParent(instance.ObjectAt(siblingB), instance.ObjectAt(siblingA)), "Parent override fixture could not reparent siblingB under siblingA");

    const kb::scene::ScenePrefabOverrideReport report = source.Prefabs().Overrides(instanceHandle);
    kb::tests::Require(report.nodes.size() == 1, "Parent override fixture should report exactly one structural override");
    kb::tests::Require(kb::scene::HasPrefabOverride(report.nodes[0].flags, kb::scene::ScenePrefabOverrideFlag::Parent), "Parent override fixture did not report a parent override");

    const kb::scene::ScenePrefabPropertyOverride* parentProperty = nullptr;
    for (const kb::scene::ScenePrefabPropertyOverride& property : report.properties) {
        if (property.propertyPath == "parent") {
            parentProperty = &property;
            break;
        }
    }
    kb::tests::Require(parentProperty != nullptr, "Parent override fixture report did not include a 'parent' property override");
    kb::tests::Require(parentProperty->objectReferenceNodeId != kb::scene::ScenePrefabNodeDesc::InvalidStableId,
        "LIB-092: a 'parent' override whose new parent is another node WITHIN the same instance must resolve to a stable, non-zero node id, not stay unresolved");

    const kb::scene::ScenePrefabHandle variantHandle = source.Prefabs().RegisterVariant("ParentOverrideVariant", baseHandle, report.properties);
    kb::tests::Require(variantHandle.IsValid(), "Parent override variant registration failed");
    kb::tests::Require(source.Prefabs().Save(baseHandle, basePath), "Parent override base asset save failed");
    kb::tests::Require(source.Prefabs().Save(variantHandle, variantPath), "Parent override variant asset save failed");

    // Fresh target Scene: the real round trip — load both files, instantiate,
    // and confirm the reparent survived.
    kb::scene::Scene target;
    const kb::scene::ScenePrefabHandle loadedBase = target.Prefabs().Load(basePath);
    const kb::scene::ScenePrefabHandle loadedVariant = target.Prefabs().Load(variantPath);
    kb::tests::Require(loadedBase.IsValid(), "Parent override base asset load failed");
    kb::tests::Require(loadedVariant.IsValid(), "Parent override variant asset load failed");

    const kb::scene::ScenePrefabInstance loadedInstance = target.Prefabs().Instantiate(loadedVariant);
    kb::tests::Require(loadedInstance.ObjectCount() == 3, "Loaded parent-override variant did not instantiate all 3 nodes");
    const kb::scene::SceneEntity loadedSiblingBParent = target.Hierarchy().Parent(loadedInstance.ObjectAt(siblingB).Entity());
    kb::tests::Require(loadedSiblingBParent == loadedInstance.ObjectAt(siblingA).Entity(),
        "LIB-092: a 'parent' override reparenting a node WITHIN the same instance must survive save+load — siblingB must be parented under siblingA, not left under root (or unresolved)");

    std::filesystem::remove(basePath, removeError);
    std::filesystem::remove(variantPath, removeError);
}

// LIB-092: proves a variant ADDED CHILD (a whole entity subtree attached to a
// variant instance that the base template does not have) is promoted into the
// variant on ApplyOverrides, reproduced for every future in-memory instance,
// AND survives a real save+load round trip. Before this fix, the variant apply
// path treated the added-child override as a report-only "children" string that
// the materializer/synchronizer no-op'd — a new variant instance never grew the
// child, and nothing about it reached disk. The fix captures the added subtree
// (ScenePrefabCaptureService), stores it on the variant record
// (variantAddedChildren), re-appends it as real nodes during materialization,
// and serializes it in the variant asset — the same way a TEMPLATE ApplyOverrides
// already re-captures added children into its base prefab.
void RunPrefabVariantAddedChildAssetRoundTripTest() {
    const std::filesystem::path basePath = std::filesystem::temp_directory_path() / "21kb_engine_prefab_variant_addedchild_base.kbprefab";
    const std::filesystem::path variantPath = std::filesystem::temp_directory_path() / "21kb_engine_prefab_variant_addedchild_variant.kbprefab";
    std::error_code removeError;
    std::filesystem::remove(basePath, removeError);
    std::filesystem::remove(variantPath, removeError);

    kb::scene::Scene source;
    kb::scene::ScenePrefab basePrefab;
    const std::uint32_t rootNode = basePrefab.AddNode(kb::scene::ScenePrefabNodeDesc{ .name = "Variant AddedChild Base Root" });
    const kb::scene::ScenePrefabHandle baseHandle = source.Prefabs().Register("VariantAddedChildBase", std::move(basePrefab));
    kb::tests::Require(baseHandle.IsValid(), "Variant added-child base registration failed");

    std::vector<kb::scene::ScenePrefabPropertyOverride> overrides{
        kb::scene::ScenePrefabPropertyOverride{
            .nodeIndex = rootNode,
            .propertyPath = "name",
            .value = "Variant AddedChild Root",
            .flag = kb::scene::ScenePrefabOverrideFlag::Name,
        },
    };
    const kb::scene::ScenePrefabHandle variantHandle = source.Prefabs().RegisterVariant("VariantAddedChild", baseHandle, std::move(overrides));
    kb::tests::Require(variantHandle.IsValid(), "Variant added-child registration failed");

    // Instantiate the variant, then attach a child the base does not have —
    // the exact "AddedChild" scenario. ApplyOverrides must promote it into the
    // variant, not merely report it.
    const kb::scene::ScenePrefabInstance authoring = source.Prefabs().Instantiate(variantHandle);
    kb::tests::Require(authoring.ObjectCount() == 1, "Variant added-child authoring instance should start with only the root");
    static_cast<void>(source.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Variant Added Child",
        .parent = authoring.ObjectAt(rootNode),
    }));
    kb::tests::Require(source.Prefabs().ApplyOverrides(authoring.Handle()), "Variant added-child ApplyOverrides failed");

    // The variant prefab itself must now structurally contain the added child.
    const kb::scene::ScenePrefab refreshedVariant = source.Prefabs().Get(variantHandle);
    kb::tests::Require(refreshedVariant.NodeCount() == 2, "Variant ApplyOverrides did not promote the added child into the variant prefab");

    // IN-MEMORY proof: a brand new instance of the variant reproduces the added
    // child (a fresh spawn from the materialized variant, not the authoring one).
    const kb::scene::ScenePrefabInstance memoryInstance = source.Prefabs().Instantiate(variantHandle);
    kb::tests::Require(memoryInstance.ObjectCount() == 2, "New in-memory variant instance did not reproduce the added child");
    bool memoryFound = false;
    for (const kb::scene::SceneEntity child : source.Hierarchy().ChildEntities(memoryInstance.ObjectAt(rootNode).Entity())) {
        memoryFound = memoryFound || source.Entities().Name(child) == "Variant Added Child";
    }
    kb::tests::Require(memoryFound, "New in-memory variant instance did not reproduce the added child by name");

    kb::tests::Require(source.Prefabs().Save(baseHandle, basePath), "Variant added-child base asset save failed");
    kb::tests::Require(source.Prefabs().Save(variantHandle, variantPath), "Variant added-child variant asset save failed");

    // DISK proof: fresh scene, load both files, instantiate — the added child
    // must be reproduced from the serialized subtree, parented under the root.
    kb::scene::Scene target;
    const kb::scene::ScenePrefabHandle loadedBase = target.Prefabs().Load(basePath);
    const kb::scene::ScenePrefabHandle loadedVariant = target.Prefabs().Load(variantPath);
    kb::tests::Require(loadedBase.IsValid(), "Variant added-child base asset load failed");
    kb::tests::Require(loadedVariant.IsValid(), "Variant added-child variant asset load failed");

    const kb::scene::ScenePrefabInstance loadedInstance = target.Prefabs().Instantiate(loadedVariant);
    kb::tests::Require(loadedInstance.ObjectCount() == 2, "Loaded variant did not reproduce the added child from disk");
    kb::tests::Require(target.Entities().Name(loadedInstance.ObjectAt(rootNode)) == "Variant AddedChild Root", "Loaded variant lost its name override");
    bool diskFound = false;
    for (const kb::scene::SceneEntity child : target.Hierarchy().ChildEntities(loadedInstance.ObjectAt(rootNode).Entity())) {
        diskFound = diskFound || target.Entities().Name(child) == "Variant Added Child";
    }
    kb::tests::Require(diskFound, "Loaded variant did not reproduce the added child by name from disk");

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
    kb::scene::ScenePrefabNodeDesc* innerRootNode = innerPrefab.TryGetMutableNode(innerRoot);
    const kb::scene::ScenePrefabNodeDesc* innerChildNode = innerPrefab.TryGetNode(innerChild);
    kb::tests::Require(innerRootNode != nullptr && innerChildNode != nullptr, "Nested joint fixture nodes were not created");
    innerRootNode->components.joint = kb::scene::ScenePrefabJointComponent{
        .type = kb::scene::JointType::Distance,
        .connectedNodeStableId = innerChildNode->stableId,
        .minLimit = 1.0F,
        .maxLimit = 3.0F,
        .enableLimit = true,
    };
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
    const kb::scene::JointComponent* nestedJoint = target.Components().Joints().TryGet(instance.ObjectAt(1).Entity());
    kb::tests::Require(nestedJoint != nullptr && nestedJoint->type == kb::scene::JointType::Distance &&
                            nestedJoint->connectedEntity == instance.ObjectAt(2).Entity() &&
                            kb::tests::NearlyEqual(nestedJoint->minLimit, 1.0F) && kb::tests::NearlyEqual(nestedJoint->maxLimit, 3.0F) && nestedJoint->enableLimit,
        "Nested prefab joint did not remap its stable target to the composed instance's child entity");

    std::filesystem::remove(innerPath, removeError);
    std::filesystem::remove(outerPath, removeError);
}

void RunPrefabCaptureRejectsExternalJointTargetTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject owner = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "External Joint Owner" });
    const kb::scene::SceneObject externalTarget = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "External Joint Target" });
    scene.Components().Joints().Set(owner.Entity(), kb::scene::JointComponent{
        .type = kb::scene::JointType::Fixed,
        .connectedEntity = externalTarget.Entity(),
    });

    kb::scene::ScenePrefab captured = scene.Prefabs().Capture(owner);
    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().Register("ExternalJointMustNotBecomeWorld", std::move(captured));
    kb::tests::Require(!handle.IsValid(),
        "Capturing a prefab joint that points outside its subtree must be rejected, never silently converted into a world joint");
}

void RunPhysicsComponentPrefabHashCoverageTest() {
    kb::scene::ScenePrefab baseline;
    const std::uint32_t nodeIndex = baseline.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "Physics Hash Coverage",
        .components = kb::scene::ScenePrefabNodeComponents{
            .collider = kb::scene::ColliderComponent{},
            .characterController = kb::scene::CharacterControllerComponent{},
        },
    });
    const auto prefabGuid = [](kb::scene::ScenePrefab prefab) {
        kb::scene::Scene scene;
        const kb::scene::ScenePrefabHandle handle = scene.Prefabs().Register("PhysicsHashCoverage", std::move(prefab));
        kb::tests::Require(handle.IsValid(), "Physics prefab hash fixture could not be registered");
        return scene.Prefabs().Guid(handle);
    };
    const std::string baselineGuid = prefabGuid(baseline);

    const auto requireHashChange = [&baseline, nodeIndex, &prefabGuid, &baselineGuid](auto mutate, const char* message) {
        kb::scene::ScenePrefab changed = baseline;
        kb::scene::ScenePrefabNodeDesc* node = changed.TryGetMutableNode(nodeIndex);
        kb::tests::Require(node != nullptr, "Physics prefab hash fixture node is missing");
        mutate(node->components);
        kb::tests::Require(prefabGuid(std::move(changed)) != baselineGuid, message);
    };

    requireHashChange([](kb::scene::ScenePrefabNodeComponents& components) { components.collider->friction = 0.2F; },
        "Prefab hash must change when Collider PhysicsMaterial friction changes");
    requireHashChange([](kb::scene::ScenePrefabNodeComponents& components) { components.collider->restitution = 0.8F; },
        "Prefab hash must change when Collider PhysicsMaterial restitution changes");
    requireHashChange([](kb::scene::ScenePrefabNodeComponents& components) { components.collider->layer = 8U; },
        "Prefab hash must change when Collider layer changes");
    requireHashChange([](kb::scene::ScenePrefabNodeComponents& components) { components.characterController->stepOffset = 0.75F; },
        "Prefab hash must change when CharacterController payload changes");
    requireHashChange([](kb::scene::ScenePrefabNodeComponents& components) {
        components.joint = kb::scene::ScenePrefabJointComponent{ .connectedNodeStableId = 0U, .maxLimit = 2.0F };
    }, "Prefab hash must change when Joint payload is added");
}

} // namespace

namespace kb::tests {

void RunScenePrefabCaptureTests() {
    const auto run = [](const char* name, void (*test)()) {
        TracePrefabCaptureTest(name);
        test();
    };
    run("RunPrefabCaptureTest", RunPrefabCaptureTest);
    run("RunPrefabAssetRoundTripTest", RunPrefabAssetRoundTripTest);
    run("RunPrefabCreateAssetRegistersSourceInstanceTest", RunPrefabCreateAssetRegistersSourceInstanceTest);
    run("RunPrefabCreateAssetFromInstanceKeepsOneOwnerTest", RunPrefabCreateAssetFromInstanceKeepsOneOwnerTest);
    run("RunPrefabReloadOfChangedFileKeepsGuidTest", RunPrefabReloadOfChangedFileKeepsGuidTest);
    run("RunPrefabInstanceLinkSurvivesSceneReopenTest", RunPrefabInstanceLinkSurvivesSceneReopenTest);
    run("RunPrefabInstanceNodeIdentitySurvivesReopenTest", RunPrefabInstanceNodeIdentitySurvivesReopenTest);
    run("RunReopenedInstanceFollowsChangedPrefabTest", RunReopenedInstanceFollowsChangedPrefabTest);
    run("RunRuntimeSpawnDoesNotLinkPrefabInstancesTest", RunRuntimeSpawnDoesNotLinkPrefabInstancesTest);
    run("RunDestroyedInstanceChildSlotReuseTest", RunDestroyedInstanceChildSlotReuseTest);
    run("RunPrefabVariantAssetRoundTripTest", RunPrefabVariantAssetRoundTripTest);
    run("RunPrefabParentOverrideAssetRoundTripTest", RunPrefabParentOverrideAssetRoundTripTest);
    run("RunPrefabVariantAddedChildAssetRoundTripTest", RunPrefabVariantAddedChildAssetRoundTripTest);
    run("RunNestedPrefabAssetRoundTripTest", RunNestedPrefabAssetRoundTripTest);
    run("RunPrefabCaptureRejectsExternalJointTargetTest", RunPrefabCaptureRejectsExternalJointTargetTest);
    run("RunPhysicsComponentPrefabHashCoverageTest", RunPhysicsComponentPrefabHashCoverageTest);
}

} // namespace kb::tests
