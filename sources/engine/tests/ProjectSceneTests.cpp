#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/project/ProjectManager.hpp"
#include "engine/assets/AssetHandle.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/modules/IEngineModule.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/ecs/ComponentSerialization.hpp"
#include "engine/ecs/World.hpp"
#include "engine/project/ProjectDescriptor.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace kb::tests {
namespace {

constexpr std::array<std::uint8_t, 8U> kSceneMagic{ '2', '1', 'K', 'B', 'S', 'C', 'N', 0 };
constexpr std::array<std::uint8_t, 8U> kSceneMetaMagic{ '2', '1', 'K', 'B', 'S', 'M', 'T', 0 };
constexpr std::array<std::uint8_t, 8U> kProjectMagic{ '2', '1', 'K', 'B', 'P', 'R', 'J', 0 };
constexpr std::array<std::uint8_t, 8U> kProjectMetaMagic{ '2', '1', 'K', 'B', 'P', 'M', 'T', 0 };

[[nodiscard]] std::filesystem::path TempRoot() {
    return std::filesystem::temp_directory_path() / "21kb_engine_project_scene_tests";
}

void CleanTempRoot() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    std::filesystem::create_directories(TempRoot(), error);
}

struct SceneDocumentSystemProbe {
    int updateCount = 0;
};

class SceneDocumentProbeSystem final : public kb::scene::SceneSystem {
public:
    explicit SceneDocumentProbeSystem(SceneDocumentSystemProbe& probe) noexcept
        : probe_(probe) {}

    void OnUpdate(kb::scene::SceneSystemContext&) override {
        ++probe_.updateCount;
    }

private:
    SceneDocumentSystemProbe& probe_;
};

class SceneDocumentProbeModule final : public kb::modules::IEngineModule {
public:
    explicit SceneDocumentProbeModule(SceneDocumentSystemProbe& probe) noexcept
        : probe_(probe) {}

    [[nodiscard]] kb::modules::EngineModuleMetadata Metadata() const override {
        return kb::modules::EngineModuleMetadata{ "SceneDocumentProbe", 1U, {}, kb::modules::EngineModuleLoadingPhase::Default };
    }

    void OnSceneAttach(kb::scene::Scene& scene) override {
        scene.Runtime().AddSceneSystem(std::make_unique<SceneDocumentProbeSystem>(probe_));
    }

private:
    SceneDocumentSystemProbe& probe_;
};

void RunProjectDescriptorRoundTripTest() {
    CleanTempRoot();
    const std::filesystem::path projectFile = TempRoot() / "Sample.21kbproject";

    kb::project::ProjectDescriptor descriptor;
    descriptor.name = "Sample";
    descriptor.category = "Game";
    descriptor.description = "Roundtrip descriptor";
    descriptor.defaultScene = "/Game/Scenes/Test.21kbscene";
    descriptor.targetPlatforms = { "Windows", "Linux" };
    descriptor.modules.push_back(kb::project::ProjectModuleDescriptor{ .name = "SampleRuntime", .type = "Runtime", .loadingPhase = "Default" });
    descriptor.plugins.push_back(kb::project::ProjectPluginReference{ .name = "GameplayTools", .binaryPath = "Plugins/GameplayTools.dll", .enabled = true });
    // LIB-129: named collision layers asset reference, file version >= 5.
    descriptor.physicsLayersAsset = "/Game/Physics/GameplayLayers.21kbphysicslayers";

    Require(kb::project::ProjectManager::CreateProject(projectFile, descriptor), "Project descriptor was not created");
    Require(std::filesystem::is_regular_file(projectFile), "Project descriptor file was not written");
    Require(std::filesystem::is_regular_file(projectFile.parent_path() / "Sample.meta"), "Project descriptor meta file was not written");
    {
        std::ifstream projectInput{ projectFile, std::ios::binary };
        std::array<std::uint8_t, kProjectMagic.size()> magic{};
        projectInput.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
        Require(magic == kProjectMagic, "Project descriptor was not written with the binary project magic");
    }
    {
        std::ifstream metaInput{ projectFile.parent_path() / "Sample.meta", std::ios::binary };
        std::array<std::uint8_t, kProjectMetaMagic.size()> magic{};
        metaInput.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
        Require(magic == kProjectMetaMagic, "Project meta was not written with the binary meta magic");
    }
    const kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(projectFile);
    Require(loaded.succeeded, "Project descriptor did not load");
    Require(loaded.descriptor.name == "Sample", "Project descriptor name did not roundtrip");
    Require(loaded.descriptor.defaultScene == "/Game/Scenes/Test.21kbscene", "Project descriptor default scene did not roundtrip");
    Require(loaded.descriptor.targetPlatforms.size() == 2, "Project descriptor target platforms did not roundtrip");
    Require(!loaded.descriptor.modules.empty() && loaded.descriptor.modules.front().name == "SampleRuntime", "Project descriptor modules did not roundtrip");
    Require(!loaded.descriptor.plugins.empty() && loaded.descriptor.plugins.front().name == "GameplayTools", "Project descriptor plugins did not roundtrip");
    Require(loaded.descriptor.plugins.front().binaryPath == "Plugins/GameplayTools.dll", "Project descriptor plugin binary path did not roundtrip");
    Require(loaded.descriptor.physicsLayersAsset == "/Game/Physics/GameplayLayers.21kbphysicslayers", "LIB-129 project descriptor physics layers asset path did not roundtrip");
    Require(loaded.descriptor.fileVersion >= 5U, "LIB-129 project descriptor written at file version >= 5");
}

void RunProjectDescriptorRejectsChecksumMismatchTest() {
    CleanTempRoot();
    const std::filesystem::path projectFile = TempRoot() / "Tamper.21kbproject";

    kb::project::ProjectDescriptor descriptor;
    descriptor.name = "Tamper";
    descriptor.category = "Game";
    descriptor.description = "Tamper descriptor";
    descriptor.defaultScene = "/Game/Scenes/Main.21kbscene";
    descriptor.targetPlatforms = { "Windows" };

    Require(kb::project::ProjectManager::CreateProject(projectFile, descriptor), "Project descriptor tamper fixture was not created");

    std::ofstream output{ projectFile, std::ios::binary | std::ios::app };
    output << "#tampered\n";
    output.close();
    Require(!kb::project::ProjectManager::LoadProject(projectFile).succeeded, "Project descriptor accepted mismatched integrity metadata");
}

void RunSceneDocumentRoundTripTest() {
    CleanTempRoot();
    const std::filesystem::path sceneFile = TempRoot() / "Scenes" / "RoundTrip.21kbscene";

    kb::scene::Scene source;
    const kb::scene::SceneEntity root = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Root" });
    const kb::scene::SceneEntity child = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Child", .parent = source.Entities().Object(root) });
    const kb::scene::SceneEntity secondRoot = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "SecondRoot" });
    source.Components().MeshRenderers().Set(root, kb::scene::MeshRendererComponent{
        .meshAssetId = 41,
        .materialAssetId = 42,
        .castsShadow = false,
        .layer = 0x00000010U,
    });
    source.Components().Lights().Set(child, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Directional,
        .intensity = 3.0F,
        .useColorTemperature = true,
        .colorTemperatureKelvin = 4200.0F,
        .layerMask = 0x00000006U,
    });
    source.Components().Cameras().Set(secondRoot, kb::scene::CameraComponent{
        .orthographicHeight = 16.0F,
        .primary = true,
        .viewportId = 7U,
        .priority = -3,
        .cullingMask = 0x0000000FU,
        .clearMode = kb::scene::CameraClearMode::DontClear,
        .clearColor = kb::scene::Vec3{ 0.1F, 0.2F, 0.3F },
    });
    source.Components().Rigidbodies().Set(root, kb::scene::RigidbodyComponent{
        .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
        .mass = 8.0F,
        .linearVelocity = kb::scene::Vec3{ 1.0F, 2.0F, 3.0F },
        .angularVelocity = kb::scene::Vec3{ 0.0F, 4.0F, 0.0F },
        .gravityScale = 0.5F,
    });
    source.Components().Colliders().Set(root, kb::scene::ColliderComponent{
        .shape = kb::scene::ColliderShape::Capsule,
        .center = kb::scene::Vec3{ 0.0F, 1.0F, 0.0F },
        .radius = 0.75F,
        .height = 2.5F,
        .trigger = true,
    });
    source.Components().AudioSources().Set(child, kb::scene::AudioSourceComponent{
        .clipAssetId = 90,
        .volume = 0.25F,
        .pitch = 1.5F,
        .loop = true,
        .spatial = false,
        .autoplay = true,
        .enabled = false,
        .mute = true,
        .pan = -0.4F,
        .spatialBlend = 0.35F,
        .attenuationModel = kb::audio::AudioAttenuationModel::Linear,
        .minDistance = 2.0F,
        .maxDistance = 80.0F,
        .rolloff = 0.5F,
        .dopplerFactor = 0.25F,
    });
    source.Components().AudioListeners().Set(secondRoot, kb::scene::AudioListenerComponent{
        .primary = true,
        .enabled = false,
    });
    source.Components().Behaviours().Set(child, kb::scene::BehaviourComponent{
        .behaviourAssetId = 91,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
        .tickGroup = kb::scene::BehaviourTickGroup::Gameplay,
        .executionOrder = -3,
    });

    Require(kb::scene::SceneDocumentService::Save(source, sceneFile, "RoundTrip"), "Scene document was not saved");

    kb::scene::Scene target;
    static_cast<void>(target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "OldRoot" }));
    Require(kb::scene::SceneDocumentService::LoadFileIntoScene(target, sceneFile), "Scene document was not loaded into target scene");

    const std::vector<kb::scene::SceneEntity> roots = target.Hierarchy().RootEntities();
    Require(roots.size() == 2, "Scene document should restore both root entities");
    Require(target.Entities().Name(roots[0]) == "Root", "Scene document first root name did not roundtrip");
    const std::vector<kb::scene::SceneEntity> restoredChildren = target.Hierarchy().ChildEntities(roots[0]);
    Require(restoredChildren.size() == 1, "Scene document child hierarchy did not roundtrip");
    Require(target.Entities().Name(roots[1]) == "SecondRoot", "Scene document second root name did not roundtrip");
    const kb::scene::MeshRendererComponent* meshRenderer = target.Components().MeshRenderers().TryGet(roots[0]);
    const kb::scene::LightComponent* light = target.Components().Lights().TryGet(restoredChildren[0]);
    const kb::scene::CameraComponent* camera = target.Components().Cameras().TryGet(roots[1]);
    const kb::scene::RigidbodyComponent* rigidbody = target.Components().Rigidbodies().TryGet(roots[0]);
    const kb::scene::ColliderComponent* collider = target.Components().Colliders().TryGet(roots[0]);
    const kb::scene::AudioSourceComponent* audioSource = target.Components().AudioSources().TryGet(restoredChildren[0]);
    const kb::scene::AudioListenerComponent* audioListener = target.Components().AudioListeners().TryGet(roots[1]);
    const kb::scene::BehaviourComponent* behaviour = target.Components().Behaviours().TryGet(restoredChildren[0]);
    Require(meshRenderer != nullptr && meshRenderer->meshAssetId == 41 && !meshRenderer->castsShadow && meshRenderer->layer == 0x00000010U, "Scene document mesh renderer did not roundtrip");
    std::uint32_t iteratedMeshRenderers = 0U;
    target.Components().Visitors().ForEachMeshRenderer(
        [](kb::scene::SceneEntity, const kb::scene::TransformComponent&, const kb::scene::MeshRendererComponent& renderer, void* context) {
            if (renderer.meshAssetId == 41) {
                auto* count = static_cast<std::uint32_t*>(context);
                ++(*count);
            }
        },
        &iteratedMeshRenderers);
    Require(iteratedMeshRenderers == 1U, "Scene document mesh renderer did not roundtrip into runtime component iteration");
    Require(light != nullptr && light->kind == kb::scene::LightKind::Directional && NearlyEqual(light->intensity, 3.0F), "Scene document light did not roundtrip");
    Require(light->useColorTemperature && NearlyEqual(light->colorTemperatureKelvin, 4200.0F) && light->layerMask == 0x00000006U,
        "Scene document light did not roundtrip useColorTemperature/colorTemperatureKelvin/layerMask");
    std::uint32_t iteratedLights = 0U;
    target.Components().Visitors().ForEachLight(
        [](kb::scene::SceneEntity, const kb::scene::TransformComponent&, const kb::scene::LightComponent& light, void* context) {
            if (light.kind == kb::scene::LightKind::Directional && NearlyEqual(light.intensity, 3.0F)) {
                auto* count = static_cast<std::uint32_t*>(context);
                ++(*count);
            }
        },
        &iteratedLights);
    Require(iteratedLights == 1U, "Scene document light did not roundtrip into runtime component iteration");
    Require(camera != nullptr && camera->primary && NearlyEqual(camera->orthographicHeight, 16.0F) && camera->viewportId == 7U && camera->priority == -3
            && camera->cullingMask == 0x0000000FU && camera->clearMode == kb::scene::CameraClearMode::DontClear
            && NearlyEqual(camera->clearColor.x, 0.1F) && NearlyEqual(camera->clearColor.y, 0.2F) && NearlyEqual(camera->clearColor.z, 0.3F),
        "Scene document camera did not roundtrip");
    Require(rigidbody != nullptr && rigidbody->bodyType == kb::scene::RigidbodyBodyType::Dynamic && NearlyEqual(rigidbody->mass, 8.0F) && NearlyEqual(rigidbody->linearVelocity.z, 3.0F) && NearlyEqual(rigidbody->angularVelocity.y, 4.0F) && NearlyEqual(rigidbody->gravityScale, 0.5F), "Scene document rigidbody did not roundtrip");
    Require(collider != nullptr && collider->shape == kb::scene::ColliderShape::Capsule && NearlyEqual(collider->center.y, 1.0F) && NearlyEqual(collider->radius, 0.75F) && NearlyEqual(collider->height, 2.5F) && collider->trigger, "Scene document collider did not roundtrip");
    Require(audioSource != nullptr && audioSource->clipAssetId == 90 && NearlyEqual(audioSource->volume, 0.25F) && NearlyEqual(audioSource->pitch, 1.5F) && audioSource->loop && !audioSource->spatial && audioSource->autoplay && !audioSource->enabled && audioSource->mute && NearlyEqual(audioSource->pan, -0.4F) && NearlyEqual(audioSource->spatialBlend, 0.35F) && audioSource->attenuationModel == kb::audio::AudioAttenuationModel::Linear && NearlyEqual(audioSource->minDistance, 2.0F) && NearlyEqual(audioSource->maxDistance, 80.0F) && NearlyEqual(audioSource->rolloff, 0.5F) && NearlyEqual(audioSource->dopplerFactor, 0.25F), "Scene document audio source did not roundtrip");
    Require(audioListener != nullptr && audioListener->primary && !audioListener->enabled, "Scene document audio listener did not roundtrip");
    Require(behaviour != nullptr && behaviour->behaviourAssetId == 91 && behaviour->backend == kb::scene::BehaviourBackend::Lua && behaviour->tickGroup == kb::scene::BehaviourTickGroup::Gameplay && behaviour->executionOrder == -3, "Scene document behaviour did not roundtrip");
}

void RunSceneAudioListenerComponentReflectionSerializationTest() {
    kb::scene::Scene source;
    const kb::scene::SceneEntity sourceEntity = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "AudioListener" });
    source.Components().AudioListeners().Set(sourceEntity, kb::scene::AudioListenerComponent{
        .primary = false,
        .enabled = true,
    });

    kb::ecs::World& sourceWorld = source.Runtime().EcsWorld();
    const kb::ecs::ComponentReflection* reflection = sourceWorld.Reflection("kb.scene.AudioListenerComponent");
    Require(reflection != nullptr, "AudioListenerComponent reflection was not registered");
    Require(reflection->FindField("primary") != nullptr, "AudioListenerComponent reflection is missing primary");

    kb::ecs::SerializedComponent serialized;
    Require(sourceWorld.SerializeComponent(sourceEntity, sourceWorld.Component<kb::scene::AudioListenerComponent>(), serialized), "AudioListenerComponent reflection serialization failed");

    kb::scene::Scene target;
    const kb::scene::SceneEntity targetEntity = target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "AudioListenerTarget" });
    Require(target.Runtime().EcsWorld().ApplySerializedComponent(targetEntity, serialized), "AudioListenerComponent reflection apply failed");

    const kb::scene::AudioListenerComponent* restored = target.Components().AudioListeners().TryGet(targetEntity);
    Require(restored != nullptr && !restored->primary && restored->enabled, "AudioListenerComponent reflection did not roundtrip");
}

void RunSceneAudioSourceComponentReflectionSerializationTest() {
    kb::scene::Scene source;
    const kb::scene::SceneEntity sourceEntity = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "AudioSource" });
    source.Components().AudioSources().Set(sourceEntity, kb::scene::AudioSourceComponent{
        .clipAssetId = 777,
        .volume = 0.6F,
        .pitch = 0.8F,
        .loop = true,
        .spatial = false,
        .autoplay = true,
        .enabled = false,
        .mute = true,
        .pan = 0.45F,
        .spatialBlend = 0.2F,
        .attenuationModel = kb::audio::AudioAttenuationModel::Exponential,
        .minDistance = 3.0F,
        .maxDistance = 30.0F,
        .rolloff = 2.0F,
        .dopplerFactor = 0.75F,
    });

    kb::ecs::World& sourceWorld = source.Runtime().EcsWorld();
    const kb::ecs::ComponentReflection* reflection = sourceWorld.Reflection("kb.scene.AudioSourceComponent");
    Require(reflection != nullptr, "AudioSourceComponent reflection was not registered");
    Require(reflection->FindField("clipAssetId") != nullptr, "AudioSourceComponent reflection is missing clipAssetId");
    Require(reflection->FindField("spatialBlend") != nullptr, "AudioSourceComponent reflection is missing spatialBlend");

    kb::ecs::SerializedComponent serialized;
    Require(sourceWorld.SerializeComponent(sourceEntity, sourceWorld.Component<kb::scene::AudioSourceComponent>(), serialized), "AudioSourceComponent reflection serialization failed");

    kb::scene::Scene target;
    const kb::scene::SceneEntity targetEntity = target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "AudioTarget" });
    Require(target.Runtime().EcsWorld().ApplySerializedComponent(targetEntity, serialized), "AudioSourceComponent reflection apply failed");

    const kb::scene::AudioSourceComponent* restored = target.Components().AudioSources().TryGet(targetEntity);
    Require(restored != nullptr && restored->clipAssetId == 777 && NearlyEqual(restored->volume, 0.6F) && NearlyEqual(restored->pitch, 0.8F) && restored->loop && !restored->spatial && restored->autoplay && !restored->enabled && restored->mute && NearlyEqual(restored->pan, 0.45F) && NearlyEqual(restored->spatialBlend, 0.2F) && restored->attenuationModel == kb::audio::AudioAttenuationModel::Exponential && NearlyEqual(restored->minDistance, 3.0F) && NearlyEqual(restored->maxDistance, 30.0F) && NearlyEqual(restored->rolloff, 2.0F) && NearlyEqual(restored->dopplerFactor, 0.75F), "AudioSourceComponent reflection did not roundtrip");
}

void RunSceneAudioSourcePrefabRoundTripTest() {
    kb::scene::Scene source;
    const kb::scene::SceneObject root = source.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "AudioPrefab" });
    source.Components().AudioSources().Set(root.Entity(), kb::scene::AudioSourceComponent{
        .clipAssetId = 1234,
        .volume = 0.7F,
        .pitch = 1.2F,
        .loop = true,
        .spatial = true,
        .autoplay = false,
        .enabled = true,
        .mute = false,
        .pan = -0.25F,
        .spatialBlend = 0.9F,
        .attenuationModel = kb::audio::AudioAttenuationModel::None,
        .minDistance = 1.5F,
        .maxDistance = 150.0F,
        .rolloff = 0.8F,
        .dopplerFactor = 1.5F,
    });

    kb::scene::ScenePrefab prefab = source.Prefabs().Capture(root);
    kb::scene::Scene target;
    const kb::scene::ScenePrefabInstance instance = target.Prefabs().Instantiate(prefab);
    Require(!instance.Empty(), "Audio source prefab did not instantiate");

    const kb::scene::AudioSourceComponent* restored = target.Components().AudioSources().TryGet(instance.ObjectAt(0).Entity());
    Require(restored != nullptr && restored->clipAssetId == 1234 && NearlyEqual(restored->volume, 0.7F) && NearlyEqual(restored->pitch, 1.2F) && restored->loop && restored->spatial && !restored->autoplay && restored->enabled && !restored->mute && NearlyEqual(restored->pan, -0.25F) && NearlyEqual(restored->spatialBlend, 0.9F) && restored->attenuationModel == kb::audio::AudioAttenuationModel::None && NearlyEqual(restored->minDistance, 1.5F) && NearlyEqual(restored->maxDistance, 150.0F) && NearlyEqual(restored->rolloff, 0.8F) && NearlyEqual(restored->dopplerFactor, 1.5F), "Audio source prefab component did not roundtrip");
}

void RunSceneAudioListenerPrefabRoundTripTest() {
    kb::scene::Scene source;
    const kb::scene::SceneObject root = source.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "AudioListenerPrefab" });
    source.Components().AudioListeners().Set(root.Entity(), kb::scene::AudioListenerComponent{
        .primary = false,
        .enabled = false,
    });

    kb::scene::ScenePrefab prefab = source.Prefabs().Capture(root);
    kb::scene::Scene target;
    const kb::scene::ScenePrefabInstance instance = target.Prefabs().Instantiate(prefab);
    Require(!instance.Empty(), "Audio listener prefab did not instantiate");

    const kb::scene::AudioListenerComponent* restored = target.Components().AudioListeners().TryGet(instance.ObjectAt(0).Entity());
    Require(restored != nullptr && !restored->primary && !restored->enabled, "Audio listener prefab component did not roundtrip");
}

void RunScenePhysicsComponentReflectionSerializationTest() {
    kb::scene::Scene source;
    const kb::scene::SceneEntity sourceEntity = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "PhysicsSource" });
    source.Components().Rigidbodies().Set(sourceEntity, kb::scene::RigidbodyComponent{
        .bodyType = kb::scene::RigidbodyBodyType::Kinematic,
        .mass = 5.0F,
        .linearVelocity = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F },
        .gravityScale = 0.0F,
        .useGravity = false,
        .lockRotation = true,
    });

    kb::ecs::World& sourceWorld = source.Runtime().EcsWorld();
    const kb::ecs::ComponentReflection* reflection = sourceWorld.Reflection("kb.scene.RigidbodyComponent");
    Require(reflection != nullptr, "RigidbodyComponent reflection was not registered");
    Require(reflection->FindField("linearVelocity") != nullptr, "RigidbodyComponent reflection is missing velocity");

    kb::ecs::SerializedComponent serialized;
    Require(sourceWorld.SerializeComponent(sourceEntity, sourceWorld.Component<kb::scene::RigidbodyComponent>(), serialized), "RigidbodyComponent reflection serialization failed");

    kb::scene::Scene target;
    const kb::scene::SceneEntity targetEntity = target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "PhysicsTarget" });
    Require(target.Runtime().EcsWorld().ApplySerializedComponent(targetEntity, serialized), "RigidbodyComponent reflection apply failed");

    const kb::scene::RigidbodyComponent* restored = target.Components().Rigidbodies().TryGet(targetEntity);
    Require(restored != nullptr && restored->bodyType == kb::scene::RigidbodyBodyType::Kinematic && NearlyEqual(restored->mass, 5.0F) && NearlyEqual(restored->linearVelocity.x, 2.0F) && !restored->useGravity && restored->lockRotation, "RigidbodyComponent reflection did not roundtrip");
}

void RunEmptySceneDocumentClearsRuntimeSceneTest() {
    kb::scene::Scene scene;
    static_cast<void>(scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "StaleRoot" }));
    Require(scene.Hierarchy().RootEntities().size() == 1, "Empty document clear setup failed");

    kb::scene::SceneDocument emptyScene;
    emptyScene.guid = "scene:empty";
    emptyScene.name = "Empty";
    Require(kb::scene::SceneDocumentService::LoadIntoScene(scene, emptyScene), "Empty scene document did not load");
    Require(scene.Hierarchy().RootEntities().empty(), "Empty scene document did not clear runtime roots");
}

void RunSceneDocumentLoadDoesNotTickRuntimeSystemsTest() {
    SceneDocumentSystemProbe probe;
    kb::project::ProjectDescriptor project;
    project.disableEnginePluginsByDefault = true;
    project.plugins.push_back(kb::project::ProjectPluginReference{ .name = "SceneDocumentProbe", .enabled = true });

    std::vector<std::unique_ptr<kb::modules::IEngineModule>> modules;
    modules.push_back(std::make_unique<SceneDocumentProbeModule>(probe));
    kb::scene::Scene scene{ std::move(project), std::move(modules) };

    kb::scene::SceneDocument document;
    document.guid = "scene:no-runtime-tick";
    document.name = "NoRuntimeTick";
    Require(kb::scene::SceneDocumentService::LoadIntoScene(scene, document), "Scene document did not load for runtime tick guard");
    Require(probe.updateCount == 0, "Scene document loading must not tick runtime scene systems");

    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(probe.updateCount == 1, "Runtime scene system did not tick during explicit runtime update");
}

void RunSceneDocumentAssetDiscoveryTest() {
    CleanTempRoot();
    const std::filesystem::path projectRoot = TempRoot() / "Project";
    const std::filesystem::path sceneFile = projectRoot / "Assets" / "Scenes" / "Discovered.21kbscene";

    kb::scene::Scene source;
    static_cast<void>(source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "DiscoveredRoot" }));
    Require(kb::scene::SceneDocumentService::Save(source, sceneFile, "Discovered"), "Scene document discovery fixture was not saved");

    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(projectRoot), "Project assets did not mount for Scene document discovery");
    Require(scene.Assets().Discover() == 1, "Scene document asset was not discovered");
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Scenes/Discovered.21kbscene");
    Require(metadata != nullptr, "Scene document metadata was not registered");
    Require(metadata->type == "Scene", "Scene document asset type was not registered");
    const kb::assets::AssetHandle<kb::scene::SceneDocument> loaded = scene.Assets().Manager().Load<kb::scene::SceneDocument>(metadata->id);
    Require(loaded.IsLoaded(), "Scene document asset did not load through the asset manager");
    Require(loaded->name == "Discovered", "Scene document asset name did not load through the asset manager");
}

void RunSceneAssetWritesMetaAndLoadsThroughSceneSystemTest() {
    CleanTempRoot();
    const std::filesystem::path sceneFile = TempRoot() / "Project" / "Assets" / "Scenes" / "Main.21kbscene";

    kb::scene::Scene source;
    const kb::scene::SceneEntity root = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "SceneRoot" });
    source.Components().MeshRenderers().Set(root, kb::scene::MeshRendererComponent{
        .meshAssetId = 101,
        .materialAssetId = 202,
    });

    Require(kb::scene::SceneDocumentService::Save(source, sceneFile, "Main"), "Scene asset was not saved");
    Require(std::filesystem::is_regular_file(sceneFile), "Scene asset file was not written");
    Require(std::filesystem::is_regular_file(sceneFile.parent_path() / "Main.meta"), "Scene asset meta file was not written");
    {
        std::ifstream sceneInput{ sceneFile, std::ios::binary };
        std::array<std::uint8_t, kSceneMagic.size()> magic{};
        sceneInput.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
        Require(magic == kSceneMagic, "Scene asset was not written with the binary scene magic");
    }
    {
        std::ifstream metaInput{ sceneFile.parent_path() / "Main.meta", std::ios::binary };
        std::array<std::uint8_t, kSceneMetaMagic.size()> magic{};
        metaInput.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
        Require(magic == kSceneMetaMagic, "Scene meta was not written with the binary meta magic");
    }

    const kb::scene::SceneDocumentLoadResult loaded = kb::scene::SceneDocumentService::Load(sceneFile);
    Require(loaded.succeeded, "Scene asset did not load through SceneDocumentService");
    Require(loaded.document.guid == "scene:Main", "Scene asset guid should use the scene namespace");
    Require(loaded.document.name == "Main", "Scene asset name did not roundtrip");
    Require(loaded.document.worldPrefab.NodeCount() == 1U, "Scene asset node count did not roundtrip");

    kb::scene::Scene runtime;
    Require(runtime.Assets().MountProject(TempRoot() / "Project"), "Scene asset project mount failed");
    Require(runtime.Assets().Discover() == 1U, "Scene asset discovery did not find the scene");
    const kb::assets::AssetMetadata* metadata = runtime.Assets().Manager().Registry().FindByPath("/Game/Scenes/Main.21kbscene");
    Require(metadata != nullptr, "Scene asset metadata was not registered");
    Require(metadata->type == "Scene", "Scene asset type was not registered");
    const kb::assets::AssetHandle<kb::scene::SceneDocument> sceneAsset = runtime.Assets().Manager().Load<kb::scene::SceneDocument>(metadata->id);
    Require(sceneAsset.IsLoaded(), "Scene asset did not load through the asset manager");
}

void RunSceneAssetRejectsChecksumMismatchTest() {
    CleanTempRoot();
    const std::filesystem::path sceneFile = TempRoot() / "Project" / "Assets" / "Scenes" / "Tamper.21kbscene";

    kb::scene::Scene source;
    static_cast<void>(source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "TamperRoot" }));
    Require(kb::scene::SceneDocumentService::Save(source, sceneFile, "Tamper"), "Scene asset checksum fixture was not saved");

    std::ofstream output{ sceneFile, std::ios::binary | std::ios::app };
    output << "#tampered\n";
    output.close();
    Require(!kb::scene::SceneDocumentService::Load(sceneFile).succeeded, "Scene asset accepted mismatched integrity metadata");
}

} // namespace

void RunProjectSceneTests() {
    RunProjectDescriptorRoundTripTest();
    RunProjectDescriptorRejectsChecksumMismatchTest();
    RunSceneDocumentRoundTripTest();
    RunSceneAudioListenerComponentReflectionSerializationTest();
    RunSceneAudioSourceComponentReflectionSerializationTest();
    RunSceneAudioListenerPrefabRoundTripTest();
    RunSceneAudioSourcePrefabRoundTripTest();
    RunScenePhysicsComponentReflectionSerializationTest();
    RunEmptySceneDocumentClearsRuntimeSceneTest();
    RunSceneDocumentLoadDoesNotTickRuntimeSystemsTest();
    RunSceneDocumentAssetDiscoveryTest();
    RunSceneAssetWritesMetaAndLoadsThroughSceneSystemTest();
    RunSceneAssetRejectsChecksumMismatchTest();
}

} // namespace kb::tests
