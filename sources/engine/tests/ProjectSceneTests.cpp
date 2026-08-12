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
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/DetailSwitchComponent.hpp"
#include "engine/scene/VisibilityBlockerComponent.hpp"
#include "engine/scene/VisibilityCellComponent.hpp"
#include "engine/scene/AuxFrameComponent.hpp"
#include "engine/scene/GeometrySwarmComponent.hpp"
#include "engine/scene/SurfaceCastComponent.hpp"
#include "engine/scene/FacingPanelComponent.hpp"
#include "engine/scene/SpaceStrokeComponent.hpp"
#include "engine/scene/HistoryRibbonComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneAudioOcclusionAccess.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTagCatalog.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/ecs/ComponentSerialization.hpp"
#include "engine/ecs/World.hpp"
#include "engine/project/ProjectDescriptor.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
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

[[nodiscard]] bool MetaContainsDependency(
    const std::filesystem::path& path, std::uint64_t expectedAssetId, std::string_view expectedRole);

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
    kb::scene::AudioSourceComponent roundTripAudioSource{
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
    };
    Require(kb::scene::SetAudioSourceOutputBus(roundTripAudioSource, "Music"), "Scene audio source bus fixture was invalid");
    source.Components().AudioSources().Set(child, roundTripAudioSource);
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
    Require(source.Tags().Define("Boss") && source.Tags().SetAssigned(root, "Boss", true),
        "Scene tag catalogue fixture was not created");
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(source, 0xA17D10U);
    kb::scene::SceneAudioMixerAccess::SetActiveSnapshot(source, "Gameplay");
    kb::tests::Require(kb::scene::SceneAudioMixerAccess::SetBusVolumeOverride(source, "Music", 0.4F),
        "Audio mixer override fixture setup failed");
    kb::tests::Require(kb::scene::SceneAudioMixerAccess::BeginSnapshotTransition(source, "Quiet", 2.0F),
        "Audio mixer transition fixture setup failed");
    kb::scene::SceneAudioOcclusionAccess::Configure(source, kb::scene::AudioOcclusionSettings{
        .enabled = true,
        .occludedVolumeScale = 0.2F,
        .maxDistance = 75.0F,
        .layerMask = 0x0000000FU,
        .maxRaycastsPerTick = 17U,
    });
    kb::scene::SceneAudioOcclusionAccess::PublishRuntimeStats(source, kb::scene::AudioOcclusionRuntimeStats{
        .sampleRequests = 9U,
        .raycasts = 8U,
        .occludedSamples = 3U,
    });

    Require(kb::scene::SceneDocumentService::Save(source, sceneFile, "RoundTrip"), "Scene document was not saved");
    Require(MetaContainsDependency(sceneFile.parent_path() / "RoundTrip.meta", 0xA17D10U, "audioMixer"),
        "Scene metadata omitted the authored audio mixer dependency");

    kb::scene::Scene target;
    static_cast<void>(target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "OldRoot" }));
    kb::tests::Require(kb::scene::SceneAudioMixerAccess::SetBusVolumeOverride(target, "OldBus", 0.1F),
        "Target audio mixer override fixture setup failed");
    kb::tests::Require(kb::scene::SceneAudioMixerAccess::BeginSnapshotTransition(target, "OldSnapshot", 4.0F),
        "Target audio mixer transition fixture setup failed");
    kb::scene::SceneAudioOcclusionAccess::PublishRuntimeStats(target, kb::scene::AudioOcclusionRuntimeStats{ .sampleRequests = 4U, .raycasts = 3U, .occludedSamples = 2U });
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
    Require(audioSource != nullptr && audioSource->clipAssetId == 90 && NearlyEqual(audioSource->volume, 0.25F) && NearlyEqual(audioSource->pitch, 1.5F) && audioSource->loop && !audioSource->spatial && audioSource->autoplay && !audioSource->enabled && audioSource->mute && NearlyEqual(audioSource->pan, -0.4F) && NearlyEqual(audioSource->spatialBlend, 0.35F) && audioSource->attenuationModel == kb::audio::AudioAttenuationModel::Linear && NearlyEqual(audioSource->minDistance, 2.0F) && NearlyEqual(audioSource->maxDistance, 80.0F) && NearlyEqual(audioSource->rolloff, 0.5F) && NearlyEqual(audioSource->dopplerFactor, 0.25F) && kb::scene::AudioSourceOutputBus(*audioSource) == "Music", "Scene document audio source did not roundtrip");
    Require(audioListener != nullptr && audioListener->primary && !audioListener->enabled, "Scene document audio listener did not roundtrip");
    Require(behaviour != nullptr && behaviour->behaviourAssetId == 91 && behaviour->backend == kb::scene::BehaviourBackend::Lua && behaviour->tickGroup == kb::scene::BehaviourTickGroup::Gameplay && behaviour->executionOrder == -3, "Scene document behaviour did not roundtrip");
    Require(target.Tags().Contains("Boss") && target.Tags().IsAssigned(roots[0], "Boss"),
        "Scene document tag catalogue and assignment did not roundtrip");
    Require(kb::scene::SceneAudioMixerAccess::ActiveMixer(target) == 0xA17D10U
            && kb::scene::SceneAudioMixerAccess::ActiveSnapshot(target) == "Gameplay",
        "Scene document authored audio mixer selection did not roundtrip");
    const kb::scene::AudioOcclusionSettings& occlusion = kb::scene::SceneAudioOcclusionAccess::Settings(target);
    Require(occlusion.enabled && NearlyEqual(occlusion.occludedVolumeScale, 0.2F)
            && NearlyEqual(occlusion.maxDistance, 75.0F) && occlusion.layerMask == 0x0000000FU
            && occlusion.maxRaycastsPerTick == 17U,
        "Scene document authored audio occlusion settings did not roundtrip");
    const kb::scene::AudioOcclusionRuntimeStats& stats = kb::scene::SceneAudioOcclusionAccess::RuntimeStats(target);
    Require(kb::scene::SceneAudioMixerAccess::BusVolumeOverrides(target).empty()
            && !kb::scene::SceneAudioMixerAccess::SnapshotTransition(target).IsActive()
            && stats.sampleRequests == 0U && stats.raycasts == 0U && stats.occludedSamples == 0U,
        "Non-additive scene load retained runtime-only audio state");
}

[[nodiscard]] std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    return std::vector<std::uint8_t>{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

[[nodiscard]] bool WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

[[nodiscard]] std::uint32_t ReadUInt32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    Require(offset + sizeof(std::uint32_t) <= bytes.size(), "Binary fixture uint32 read exceeded its buffer");
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::uint64_t ReadUInt64(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    Require(offset + sizeof(std::uint64_t) <= bytes.size(), "Binary fixture uint64 read exceeded its buffer");
    std::uint64_t value = 0U;
    for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
        value |= static_cast<std::uint64_t>(bytes[offset + byte]) << (byte * 8U);
    }
    return value;
}

void WriteUInt32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    Require(offset + sizeof(value) <= bytes.size(), "Binary fixture uint32 write exceeded its buffer");
    for (std::uint32_t byte = 0U; byte < 4U; ++byte) {
        bytes[offset + byte] = static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU);
    }
}

void WriteUInt64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    Require(offset + sizeof(value) <= bytes.size(), "Binary fixture uint64 write exceeded its buffer");
    for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
        bytes[offset + byte] = static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU);
    }
}

[[nodiscard]] std::size_t SkipString(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    const std::uint32_t length = ReadUInt32(bytes, offset);
    const std::size_t next = offset + sizeof(std::uint32_t) + length;
    Require(next <= bytes.size(), "Binary fixture string exceeded its buffer");
    return next;
}

struct TestIntegrity {
    std::uint64_t hash = 14695981039346656037ULL;
    std::uint32_t checksum = 0xFFFFFFFFU;
};

[[nodiscard]] TestIntegrity ComputeIntegrity(const std::vector<std::uint8_t>& bytes) noexcept {
    TestIntegrity result;
    for (const std::uint8_t byte : bytes) {
        result.hash ^= byte;
        result.hash *= 1099511628211ULL;
        result.checksum ^= byte;
        for (std::uint32_t bit = 0U; bit < 8U; ++bit) {
            result.checksum = (result.checksum & 1U) != 0U
                ? (0xEDB88320U ^ (result.checksum >> 1U))
                : (result.checksum >> 1U);
        }
    }
    result.checksum ^= 0xFFFFFFFFU;
    if (result.hash == 0U) {
        result.hash = 1099511628211ULL;
    }
    return result;
}

[[nodiscard]] bool MetaContainsDependency(
    const std::filesystem::path& path, std::uint64_t expectedAssetId, std::string_view expectedRole) {
    const std::vector<std::uint8_t> bytes = ReadBytes(path);
    if (bytes.size() < 12U) {
        return false;
    }
    std::size_t offset = 12U;
    for (std::uint32_t stringIndex = 0U; stringIndex < 4U; ++stringIndex) {
        offset = SkipString(bytes, offset);
    }
    offset += sizeof(std::uint64_t) * 2U + sizeof(std::uint32_t) * 3U;
    const std::uint32_t dependencyCount = ReadUInt32(bytes, offset);
    offset += sizeof(std::uint32_t);
    for (std::uint32_t index = 0U; index < dependencyCount; ++index) {
        const std::uint64_t assetId = ReadUInt64(bytes, offset);
        offset += sizeof(std::uint64_t);
        const std::uint32_t roleLength = ReadUInt32(bytes, offset);
        offset += sizeof(std::uint32_t);
        Require(offset + roleLength <= bytes.size(), "Scene meta dependency role exceeded its buffer");
        const std::string_view role{ reinterpret_cast<const char*>(bytes.data() + offset), roleLength };
        offset += roleLength;
        if (assetId == expectedAssetId && role == expectedRole) {
            return true;
        }
    }
    return false;
}

void RunSceneAudioListenerComponentReflectionSerializationTest() {
    kb::scene::Scene source;
    const kb::scene::SceneEntity sourceEntity = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "AudioListener" });
    source.Components().AudioListeners().Set(sourceEntity, kb::scene::AudioListenerComponent{
        .priority = -11,
        .localUser = kb::input::LocalUserId{ 3U },
        .primary = false,
        .enabled = true,
    });

    kb::ecs::World& sourceWorld = source.Runtime().EcsWorld();
    const kb::ecs::ComponentReflection* reflection = sourceWorld.Reflection(kb::scene::AudioListenerComponent::StableId);
    Require(reflection != nullptr, "AudioListenerComponent reflection was not registered");
    Require(reflection->FindField("primary") != nullptr, "AudioListenerComponent reflection is missing primary");

    kb::ecs::SerializedComponent serialized;
    Require(sourceWorld.SerializeComponent(sourceEntity, sourceWorld.Component<kb::scene::AudioListenerComponent>(), serialized), "AudioListenerComponent reflection serialization failed");

    kb::scene::Scene target;
    const kb::scene::SceneEntity targetEntity = target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "AudioListenerTarget" });
    Require(target.Runtime().EcsWorld().ApplySerializedComponent(targetEntity, serialized), "AudioListenerComponent reflection apply failed");

    const kb::scene::AudioListenerComponent* restored = target.Components().AudioListeners().TryGet(targetEntity);
    Require(restored != nullptr && restored->priority == -11 && restored->localUser == kb::input::LocalUserId{ 3U }
            && !restored->primary && restored->enabled,
        "AudioListenerComponent reflection did not roundtrip");
}

void RunSceneRadianceEmitterReflectionSerializationTest() {
    kb::scene::Scene source;
    const kb::scene::SceneEntity sourceEntity = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Radiance Emitter" });
    source.Components().Lights().Set(sourceEntity, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Tube,
        .color = kb::scene::Vec3{ 0.9F, 0.7F, 0.4F },
        .intensity = 6.0F,
        .range = 14.0F,
        .innerConeDegrees = 18.0F,
        .outerConeDegrees = 42.0F,
        .areaWidth = 5.0F,
        .areaHeight = 1.25F,
        .contactShadowLength = 0.3F,
        .volumetricScattering = 0.6F,
        .castsShadow = false,
        .useColorTemperature = true,
        .colorTemperatureKelvin = 4200.0F,
        .layerMask = 0x00000008U,
    });

    kb::ecs::World& sourceWorld = source.Runtime().EcsWorld();
    const kb::ecs::ComponentReflection* reflection = sourceWorld.Reflection(kb::scene::LightComponent::StableId);
    Require(reflection != nullptr, "3D Radiance Emitter reflection was not registered under its stable id");
    Require(reflection != nullptr && reflection->FindField("areaWidth") != nullptr && reflection->FindField("areaHeight") != nullptr,
        "3D Radiance Emitter reflection is missing its surface geometry fields");

    kb::ecs::SerializedComponent serialized;
    Require(sourceWorld.SerializeComponent(sourceEntity, sourceWorld.Component<kb::scene::LightComponent>(), serialized),
        "3D Radiance Emitter reflection serialization failed");

    kb::scene::Scene target;
    const kb::scene::SceneEntity targetEntity = target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Radiance Emitter Target" });
    Require(target.Runtime().EcsWorld().ApplySerializedComponent(targetEntity, serialized),
        "3D Radiance Emitter reflection apply failed");

    const kb::scene::LightComponent* restored = target.Components().Lights().TryGet(targetEntity);
    Require(restored != nullptr && restored->kind == kb::scene::LightKind::Tube &&
            NearlyEqual(restored->areaWidth, 5.0F) && NearlyEqual(restored->areaHeight, 1.25F) &&
            NearlyEqual(restored->contactShadowLength, 0.3F) && NearlyEqual(restored->volumetricScattering, 0.6F) &&
            !restored->castsShadow && restored->useColorTemperature &&
            NearlyEqual(restored->colorTemperatureKelvin, 4200.0F) && restored->layerMask == 0x00000008U,
        "3D Radiance Emitter reflection did not roundtrip its authored state");
}

void RunSceneAmbientRadianceReflectionSerializationTest() {
    kb::scene::Scene source;
    const kb::scene::SceneEntity sourceEntity = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Ambient Radiance" });
    source.Components().AmbientRadiances().Set(sourceEntity, kb::scene::AmbientRadianceComponent{
        .mode = kb::scene::AmbientRadianceMode::CapturedEnvironment,
        .color = kb::scene::Vec3{ 0.1F, 0.2F, 0.3F }, .horizonColor = kb::scene::Vec3{ 0.2F, 0.3F, 0.4F }, .zenithColor = kb::scene::Vec3{ 0.5F, 0.6F, 0.7F },
        .environmentAssetId = 51U, .intensity = 1.4F, .diffuseIntensity = 1.2F, .specularIntensity = 0.4F, .priority = 6, .enabled = true,
    });
    kb::ecs::World& sourceWorld = source.Runtime().EcsWorld();
    const kb::ecs::ComponentReflection* reflection = sourceWorld.Reflection(kb::scene::AmbientRadianceComponent::StableId);
    Require(reflection != nullptr && reflection->FindField("environmentAssetId") != nullptr && reflection->FindField("specularIntensity") != nullptr,
        "Ambient Radiance reflection was not registered under its stable id");
    kb::ecs::SerializedComponent serialized;
    Require(sourceWorld.SerializeComponent(sourceEntity, sourceWorld.Component<kb::scene::AmbientRadianceComponent>(), serialized), "Ambient Radiance reflection serialization failed");
    kb::scene::Scene target;
    const kb::scene::SceneEntity targetEntity = target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Ambient Target" });
    Require(target.Runtime().EcsWorld().ApplySerializedComponent(targetEntity, serialized), "Ambient Radiance reflection apply failed");
    const kb::scene::AmbientRadianceComponent* restored = target.Components().AmbientRadiances().TryGet(targetEntity);
    Require(restored != nullptr && restored->mode == kb::scene::AmbientRadianceMode::CapturedEnvironment && restored->environmentAssetId == 51U &&
            NearlyEqual(restored->zenithColor.z, 0.7F) && NearlyEqual(restored->intensity, 1.4F) && NearlyEqual(restored->diffuseIntensity, 1.2F) && NearlyEqual(restored->specularIntensity, 0.4F) && restored->priority == 6,
        "Ambient Radiance reflection did not roundtrip authored state");
}

void RunSceneDetailSwitchReflectionSerializationTest() {
    kb::scene::Scene source;
    const kb::scene::SceneEntity sourceEntity = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Detail Switch" });
    source.Components().DetailSwitches().Set(sourceEntity, kb::scene::SceneDetailSwitchComponent{
        .groupId = 73U, .minimumLod = 1U, .maximumLod = 4U, .promoteCoverage = 0.42F, .demoteCoverage = 0.21F, .enabled = false,
    });
    kb::ecs::World& sourceWorld = source.Runtime().EcsWorld();
    const kb::ecs::ComponentReflection* reflection = sourceWorld.Reflection(kb::scene::SceneDetailSwitchComponent::StableId);
    Require(reflection != nullptr && reflection->FindField("groupId") != nullptr && reflection->FindField("demoteCoverage") != nullptr,
        "Detail Switch reflection was not registered under its stable id");
    kb::ecs::SerializedComponent serialized;
    Require(sourceWorld.SerializeComponent(sourceEntity, sourceWorld.Component<kb::scene::SceneDetailSwitchComponent>(), serialized), "Detail Switch reflection serialization failed");
    kb::scene::Scene target;
    const kb::scene::SceneEntity targetEntity = target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Detail Switch Target" });
    Require(target.Runtime().EcsWorld().ApplySerializedComponent(targetEntity, serialized), "Detail Switch reflection apply failed");
    const kb::scene::SceneDetailSwitchComponent* restored = target.Components().DetailSwitches().TryGet(targetEntity);
    Require(restored != nullptr && restored->groupId == 73U && restored->minimumLod == 1U && restored->maximumLod == 4U &&
            NearlyEqual(restored->promoteCoverage, 0.42F) && NearlyEqual(restored->demoteCoverage, 0.21F) && !restored->enabled,
        "Detail Switch reflection did not roundtrip authored state");
}

void RunSceneVisibilityBlockerReflectionSerializationTest() {
    kb::scene::Scene source;
    const kb::scene::SceneEntity sourceEntity = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Visibility Blocker" });
    source.Components().VisibilityBlockers().Set(sourceEntity, kb::scene::SceneVisibilityBlockerComponent{
        .localCenter = kb::scene::Vec3{ 0.25F, -0.5F, 0.75F }, .size = kb::scene::Vec3{ 2.0F, 3.0F, 4.0F }, .enabled = false,
    });
    kb::ecs::World& sourceWorld = source.Runtime().EcsWorld();
    const kb::ecs::ComponentReflection* reflection = sourceWorld.Reflection(kb::scene::SceneVisibilityBlockerComponent::StableId);
    Require(reflection != nullptr && reflection->FindField("localCenter") != nullptr && reflection->FindField("size") != nullptr,
        "Visibility Blocker reflection was not registered under its stable id");
    kb::ecs::SerializedComponent serialized;
    Require(sourceWorld.SerializeComponent(sourceEntity, sourceWorld.Component<kb::scene::SceneVisibilityBlockerComponent>(), serialized), "Visibility Blocker reflection serialization failed");
    kb::scene::Scene target;
    const kb::scene::SceneEntity targetEntity = target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Visibility Blocker Target" });
    Require(target.Runtime().EcsWorld().ApplySerializedComponent(targetEntity, serialized), "Visibility Blocker reflection apply failed");
    const kb::scene::SceneVisibilityBlockerComponent* restored = target.Components().VisibilityBlockers().TryGet(targetEntity);
    Require(restored != nullptr && NearlyEqual(restored->localCenter.x, 0.25F) && NearlyEqual(restored->localCenter.y, -0.5F) &&
            NearlyEqual(restored->size.z, 4.0F) && !restored->enabled,
        "Visibility Blocker reflection did not roundtrip authored state");
}

void RunSceneVisibilityCellReflectionSerializationTest() {
    kb::scene::Scene source;
    const kb::scene::SceneEntity sourceEntity = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Visibility Cell" });
    source.Components().VisibilityCells().Set(sourceEntity, kb::scene::VisibilityCellComponent{
        .membershipMask = 5U, .membership = kb::scene::VisibilityCellMembership::Exclude,
        .visibilityOverride = kb::scene::VisibilityCellOverride::ForceHidden, .enabled = false,
    });
    kb::ecs::World& sourceWorld = source.Runtime().EcsWorld();
    const kb::ecs::ComponentReflection* reflection = sourceWorld.Reflection(kb::scene::VisibilityCellComponent::StableId);
    Require(reflection != nullptr && reflection->FindField("membershipMask") != nullptr && reflection->FindField("visibilityOverride") != nullptr,
        "Visibility Cell reflection was not registered under its stable id");
    kb::ecs::SerializedComponent serialized;
    Require(sourceWorld.SerializeComponent(sourceEntity, sourceWorld.Component<kb::scene::VisibilityCellComponent>(), serialized), "Visibility Cell reflection serialization failed");
    kb::scene::Scene target;
    const kb::scene::SceneEntity targetEntity = target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Visibility Cell Target" });
    Require(target.Runtime().EcsWorld().ApplySerializedComponent(targetEntity, serialized), "Visibility Cell reflection apply failed");
    const kb::scene::VisibilityCellComponent* restored = target.Components().VisibilityCells().TryGet(targetEntity);
    Require(restored != nullptr && restored->membershipMask == 5U && restored->membership == kb::scene::VisibilityCellMembership::Exclude &&
            restored->visibilityOverride == kb::scene::VisibilityCellOverride::ForceHidden && !restored->enabled,
        "Visibility Cell reflection did not roundtrip authored state");
}

void RunSceneSecondaryFrameReflectionSerializationTest() {
    kb::scene::Scene source;
    const kb::scene::SceneEntity sourceEntity = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Secondary Frame" });
    source.Components().AuxFrames().Set(sourceEntity, kb::scene::AuxFrameComponent{
        .mode = kb::scene::AuxFrameMode::Mirror, .imageTargetId = 91U, .width = 1024U, .height = 576U,
        .mirrorPlaneNormal = kb::scene::Vec3{ 0.0F, 1.0F, 0.0F }, .mirrorPlaneOffset = -2.5F, .enabled = true,
    });
    kb::ecs::World& sourceWorld = source.Runtime().EcsWorld();
    const kb::ecs::ComponentReflection* reflection = sourceWorld.Reflection(kb::scene::AuxFrameComponent::StableId);
    Require(reflection != nullptr && reflection->FindField("imageTargetId") != nullptr && reflection->FindField("mirrorPlaneNormal") != nullptr,
        "Secondary Frame reflection was not registered under its stable id");
    kb::ecs::SerializedComponent serialized;
    Require(sourceWorld.SerializeComponent(sourceEntity, sourceWorld.Component<kb::scene::AuxFrameComponent>(), serialized), "Secondary Frame reflection serialization failed");
    kb::scene::Scene target;
    const kb::scene::SceneEntity targetEntity = target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Secondary Frame Target" });
    Require(target.Runtime().EcsWorld().ApplySerializedComponent(targetEntity, serialized), "Secondary Frame reflection apply failed");
    const kb::scene::AuxFrameComponent* restored = target.Components().AuxFrames().TryGet(targetEntity);
    Require(restored != nullptr && restored->mode == kb::scene::AuxFrameMode::Mirror && restored->imageTargetId == 91U &&
            restored->width == 1024U && restored->height == 576U && NearlyEqual(restored->mirrorPlaneOffset, -2.5F) && restored->enabled,
        "Secondary Frame reflection did not roundtrip authored state");
}

void RunSceneGeometrySwarmReflectionSerializationTest() {
    kb::scene::Scene source;
    const kb::scene::SceneEntity sourceEntity = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Geometry Swarm" });
    source.Components().GeometrySwarms().Set(sourceEntity, kb::scene::GeometrySwarmComponent{
        .meshAssetId = 17U, .materialAssetId = 23U, .instanceCount = 12U, .columns = 3U, .rows = 2U, .layers = 2U,
        .spacing = kb::scene::Vec3{ 1.5F, 2.0F, 0.5F }, .instanceScale = 0.75F, .layer = 4U,
        .castsShadow = false, .receivesShadow = true, .enabled = true,
    });
    kb::ecs::World& sourceWorld = source.Runtime().EcsWorld();
    const kb::ecs::ComponentReflection* reflection = sourceWorld.Reflection(kb::scene::GeometrySwarmComponent::StableId);
    Require(reflection != nullptr && reflection->FindField("meshAssetId") != nullptr && reflection->FindField("instanceScale") != nullptr,
        "Geometry Swarm reflection was not registered under its stable id");
    kb::ecs::SerializedComponent serialized;
    Require(sourceWorld.SerializeComponent(sourceEntity, sourceWorld.Component<kb::scene::GeometrySwarmComponent>(), serialized), "Geometry Swarm reflection serialization failed");
    kb::scene::Scene target;
    const kb::scene::SceneEntity targetEntity = target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Geometry Swarm Target" });
    Require(target.Runtime().EcsWorld().ApplySerializedComponent(targetEntity, serialized), "Geometry Swarm reflection apply failed");
    const kb::scene::GeometrySwarmComponent* restored = target.Components().GeometrySwarms().TryGet(targetEntity);
    Require(restored != nullptr && restored->meshAssetId == 17U && restored->materialAssetId == 23U && restored->instanceCount == 12U &&
            restored->columns == 3U && restored->rows == 2U && restored->layers == 2U && NearlyEqual(restored->spacing.x, 1.5F) &&
            NearlyEqual(restored->spacing.y, 2.0F) && NearlyEqual(restored->spacing.z, 0.5F) && NearlyEqual(restored->instanceScale, 0.75F) &&
            restored->layer == 4U && !restored->castsShadow && restored->receivesShadow && restored->enabled,
        "Geometry Swarm reflection did not roundtrip authored state");
}

void RunSceneSurfaceCastReflectionSerializationTest() {
    kb::scene::Scene source;
    const kb::scene::SceneEntity sourceEntity = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Surface Cast" });
    source.Components().SurfaceCasts().Set(sourceEntity, kb::scene::SurfaceCastComponent{ .materialAssetId = 23U, .receiverLayerMask = 6U, .order = -4, .content = kb::scene::SurfaceCastContent::Detail, .enabled = true });
    kb::ecs::World& sourceWorld = source.Runtime().EcsWorld();
    const kb::ecs::ComponentReflection* reflection = sourceWorld.Reflection(kb::scene::SurfaceCastComponent::StableId);
    Require(reflection != nullptr && reflection->FindField("receiverLayerMask") != nullptr && reflection->FindField("order") != nullptr, "Surface Cast reflection was not registered under its stable id");
    kb::ecs::SerializedComponent serialized;
    Require(sourceWorld.SerializeComponent(sourceEntity, sourceWorld.Component<kb::scene::SurfaceCastComponent>(), serialized), "Surface Cast reflection serialization failed");
    kb::scene::Scene target;
    const kb::scene::SceneEntity targetEntity = target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Surface Cast Target" });
    Require(target.Runtime().EcsWorld().ApplySerializedComponent(targetEntity, serialized), "Surface Cast reflection apply failed");
    const kb::scene::SurfaceCastComponent* restored = target.Components().SurfaceCasts().TryGet(targetEntity);
    Require(restored != nullptr && restored->materialAssetId == 23U && restored->receiverLayerMask == 6U && restored->order == -4 && restored->content == kb::scene::SurfaceCastContent::Detail && restored->enabled, "Surface Cast reflection did not roundtrip authored state");
}

void RunSceneFacingPanelReflectionSerializationTest() {
    kb::scene::Scene source;
    const kb::scene::SceneEntity sourceEntity = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Facing Panel" });
    source.Components().FacingPanels().Set(sourceEntity, kb::scene::FacingPanelComponent{
        .mode = kb::scene::FacingPanelMode::Axis,
        .targetPoint = kb::scene::Vec3{ 1.0F, 2.0F, 3.0F }, .axis = kb::scene::Vec3{ -1.0F, 0.0F, 0.0F }, .up = kb::scene::Vec3{ 0.0F, 1.0F, 0.0F }, .enabled = true,
    });
    kb::ecs::World& sourceWorld = source.Runtime().EcsWorld();
    const kb::ecs::ComponentReflection* reflection = sourceWorld.Reflection(kb::scene::FacingPanelComponent::StableId);
    Require(reflection != nullptr && reflection->FindField("mode") != nullptr && reflection->FindField("targetPoint") != nullptr && reflection->FindField("axis") != nullptr && reflection->FindField("up") != nullptr, "Facing Panel reflection was not registered under its stable id");
    kb::ecs::SerializedComponent serialized;
    Require(sourceWorld.SerializeComponent(sourceEntity, sourceWorld.Component<kb::scene::FacingPanelComponent>(), serialized), "Facing Panel reflection serialization failed");
    kb::scene::Scene target;
    const kb::scene::SceneEntity targetEntity = target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Facing Panel Target" });
    Require(target.Runtime().EcsWorld().ApplySerializedComponent(targetEntity, serialized), "Facing Panel reflection apply failed");
    const kb::scene::FacingPanelComponent* restored = target.Components().FacingPanels().TryGet(targetEntity);
    Require(restored != nullptr && restored->mode == kb::scene::FacingPanelMode::Axis && NearlyEqual(restored->targetPoint.x, 1.0F) && NearlyEqual(restored->axis.x, -1.0F) && restored->enabled, "Facing Panel reflection did not roundtrip authored state");
}

void RunSceneSpaceStrokeReflectionSerializationTest() {
    kb::scene::Scene source;
    const kb::scene::SceneEntity sourceEntity = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Space Stroke" });
    source.Components().SpaceStrokes().Set(sourceEntity, kb::scene::SpaceStrokeComponent{ .meshAssetId = 17U, .materialAssetId = 23U, .mode = kb::scene::SpaceStrokeMode::Cable, .width = 0.25F, .cableSag = 1.5F, .splineSegments = 12U, .layer = 4U, .castsShadow = true, .receivesShadow = false, .enabled = true });
    kb::ecs::World& sourceWorld = source.Runtime().EcsWorld();
    const kb::ecs::ComponentReflection* reflection = sourceWorld.Reflection(kb::scene::SpaceStrokeComponent::StableId);
    Require(reflection != nullptr && reflection->FindField("mode") != nullptr && reflection->FindField("splineSegments") != nullptr, "Space Stroke reflection was not registered under its stable id");
    kb::ecs::SerializedComponent serialized;
    Require(sourceWorld.SerializeComponent(sourceEntity, sourceWorld.Component<kb::scene::SpaceStrokeComponent>(), serialized), "Space Stroke reflection serialization failed");
    kb::scene::Scene target;
    const kb::scene::SceneEntity targetEntity = target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Space Stroke Target" });
    Require(target.Runtime().EcsWorld().ApplySerializedComponent(targetEntity, serialized), "Space Stroke reflection apply failed");
    const kb::scene::SpaceStrokeComponent* restored = target.Components().SpaceStrokes().TryGet(targetEntity);
    Require(restored != nullptr && restored->meshAssetId == 17U && restored->materialAssetId == 23U && restored->mode == kb::scene::SpaceStrokeMode::Cable && NearlyEqual(restored->width, 0.25F) && NearlyEqual(restored->cableSag, 1.5F) && restored->splineSegments == 12U && restored->layer == 4U && restored->castsShadow && !restored->receivesShadow && restored->enabled, "Space Stroke reflection did not roundtrip authored state");
}

void RunSceneSpaceStrokePrefabRoundTripTest() {
    kb::scene::Scene source;
    const kb::scene::SceneObject root = source.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Space Stroke Prefab" });
    source.Components().SpaceStrokes().Set(root.Entity(), kb::scene::SpaceStrokeComponent{
        .meshAssetId = 17U, .materialAssetId = 23U, .mode = kb::scene::SpaceStrokeMode::Spline,
        .width = 0.35F, .cableSag = 0.8F, .splineSegments = 11U, .layer = 8U,
        .castsShadow = true, .receivesShadow = false, .enabled = true,
    });

    const kb::scene::ScenePrefab prefab = source.Prefabs().Capture(root);
    kb::scene::Scene target;
    const kb::scene::ScenePrefabInstance instance = target.Prefabs().Instantiate(prefab);
    Require(!instance.Empty(), "Space Stroke prefab did not instantiate");
    const kb::scene::SpaceStrokeComponent* restored = target.Components().SpaceStrokes().TryGet(instance.ObjectAt(0U).Entity());
    Require(restored != nullptr && restored->meshAssetId == 17U && restored->materialAssetId == 23U &&
            restored->mode == kb::scene::SpaceStrokeMode::Spline && NearlyEqual(restored->width, 0.35F) &&
            NearlyEqual(restored->cableSag, 0.8F) && restored->splineSegments == 11U && restored->layer == 8U &&
            restored->castsShadow && !restored->receivesShadow && restored->enabled,
        "Space Stroke prefab component did not roundtrip");
}

void RunSceneHistoryRibbonReflectionSerializationTest() {
    kb::scene::Scene source;
    const kb::scene::SceneEntity sourceEntity = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "History Ribbon" });
    source.Components().HistoryRibbons().Set(sourceEntity, kb::scene::HistoryRibbonComponent{ .meshAssetId = 17U, .materialAssetId = 23U, .lifetimeSeconds = 2.5F, .width = 0.25F, .sampleIntervalSeconds = 0.1F, .layer = 4U, .castsShadow = true, .receivesShadow = false, .enabled = true });
    kb::ecs::World& sourceWorld = source.Runtime().EcsWorld();
    const kb::ecs::ComponentReflection* reflection = sourceWorld.Reflection(kb::scene::HistoryRibbonComponent::StableId);
    Require(reflection != nullptr && reflection->FindField("lifetimeSeconds") != nullptr && reflection->FindField("sampleIntervalSeconds") != nullptr, "History Ribbon reflection was not registered under its stable id");
    kb::ecs::SerializedComponent serialized;
    Require(sourceWorld.SerializeComponent(sourceEntity, sourceWorld.Component<kb::scene::HistoryRibbonComponent>(), serialized), "History Ribbon reflection serialization failed");
    kb::scene::Scene target;
    const kb::scene::SceneEntity targetEntity = target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "History Ribbon Target" });
    Require(target.Runtime().EcsWorld().ApplySerializedComponent(targetEntity, serialized), "History Ribbon reflection apply failed");
    const kb::scene::HistoryRibbonComponent* restored = target.Components().HistoryRibbons().TryGet(targetEntity);
    Require(restored != nullptr && restored->meshAssetId == 17U && restored->materialAssetId == 23U && NearlyEqual(restored->lifetimeSeconds, 2.5F) && NearlyEqual(restored->width, 0.25F) && NearlyEqual(restored->sampleIntervalSeconds, 0.1F) && restored->layer == 4U && restored->castsShadow && !restored->receivesShadow && restored->enabled, "History Ribbon reflection did not roundtrip authored state");
}

void RunSceneHistoryRibbonPrefabRoundTripTest() {
    kb::scene::Scene source;
    const kb::scene::SceneObject root = source.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "History Ribbon Prefab" });
    source.Components().HistoryRibbons().Set(root.Entity(), kb::scene::HistoryRibbonComponent{ .meshAssetId = 17U, .materialAssetId = 23U, .lifetimeSeconds = 2.0F, .width = 0.35F, .sampleIntervalSeconds = 0.2F, .layer = 8U, .castsShadow = true, .receivesShadow = false, .enabled = true });
    const kb::scene::ScenePrefab prefab = source.Prefabs().Capture(root);
    kb::scene::Scene target;
    const kb::scene::ScenePrefabInstance instance = target.Prefabs().Instantiate(prefab);
    Require(!instance.Empty(), "History Ribbon prefab did not instantiate");
    const kb::scene::HistoryRibbonComponent* restored = target.Components().HistoryRibbons().TryGet(instance.ObjectAt(0U).Entity());
    Require(restored != nullptr && restored->meshAssetId == 17U && restored->materialAssetId == 23U && NearlyEqual(restored->lifetimeSeconds, 2.0F) && NearlyEqual(restored->width, 0.35F) && NearlyEqual(restored->sampleIntervalSeconds, 0.2F) && restored->layer == 8U && restored->castsShadow && !restored->receivesShadow && restored->enabled, "History Ribbon prefab component did not roundtrip");
}

void RunSceneAudioSourceComponentReflectionSerializationTest() {
    kb::scene::Scene source;
    const kb::scene::SceneEntity sourceEntity = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "AudioSource" });
    kb::scene::AudioSourceComponent sourceComponent{
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
    };
    Require(kb::scene::SetAudioSourceOutputBus(sourceComponent, "Dialogue"), "Audio source reflection bus fixture was invalid");
    source.Components().AudioSources().Set(sourceEntity, sourceComponent);

    kb::ecs::World& sourceWorld = source.Runtime().EcsWorld();
    const kb::ecs::ComponentReflection* reflection = sourceWorld.Reflection("kb.scene.AudioSourceComponent");
    Require(reflection != nullptr, "AudioSourceComponent reflection was not registered");
    Require(reflection->FindField("clipAssetId") != nullptr, "AudioSourceComponent reflection is missing clipAssetId");
    Require(reflection->FindField("spatialBlend") != nullptr, "AudioSourceComponent reflection is missing spatialBlend");
    Require(reflection->FindField("outputBus") != nullptr && reflection->FindField("outputBusLength") != nullptr,
        "AudioSourceComponent reflection is missing output bus storage");

    kb::ecs::SerializedComponent serialized;
    Require(sourceWorld.SerializeComponent(sourceEntity, sourceWorld.Component<kb::scene::AudioSourceComponent>(), serialized), "AudioSourceComponent reflection serialization failed");

    kb::scene::Scene target;
    const kb::scene::SceneEntity targetEntity = target.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "AudioTarget" });
    Require(target.Runtime().EcsWorld().ApplySerializedComponent(targetEntity, serialized), "AudioSourceComponent reflection apply failed");

    const kb::scene::AudioSourceComponent* restored = target.Components().AudioSources().TryGet(targetEntity);
    Require(restored != nullptr && restored->clipAssetId == 777 && NearlyEqual(restored->volume, 0.6F) && NearlyEqual(restored->pitch, 0.8F) && restored->loop && !restored->spatial && restored->autoplay && !restored->enabled && restored->mute && NearlyEqual(restored->pan, 0.45F) && NearlyEqual(restored->spatialBlend, 0.2F) && restored->attenuationModel == kb::audio::AudioAttenuationModel::Exponential && NearlyEqual(restored->minDistance, 3.0F) && NearlyEqual(restored->maxDistance, 30.0F) && NearlyEqual(restored->rolloff, 2.0F) && NearlyEqual(restored->dopplerFactor, 0.75F) && kb::scene::AudioSourceOutputBus(*restored) == "Dialogue", "AudioSourceComponent reflection did not roundtrip");
}

void RunSceneAudioSourceOutputBusValidationTest() {
    kb::scene::AudioSourceComponent component;
    const std::string maximum(kb::scene::AudioSourceComponent::MaxOutputBusBytes, 'B');
    Require(kb::scene::SetAudioSourceOutputBus(component, maximum)
            && kb::scene::AudioSourceOutputBus(component) == maximum,
        "Audio source output bus rejected its maximum supported length");

    const std::string tooLong(kb::scene::AudioSourceComponent::MaxOutputBusBytes + 1U, 'C');
    Require(!kb::scene::SetAudioSourceOutputBus(component, tooLong)
            && kb::scene::AudioSourceOutputBus(component) == maximum,
        "Audio source output bus silently truncated an overlong name");

    const std::string embeddedNull{ "Effects\0Hidden", 14U };
    Require(!kb::scene::SetAudioSourceOutputBus(component, embeddedNull)
            && kb::scene::AudioSourceOutputBus(component) == maximum,
        "Audio source output bus accepted an embedded null byte");

    component.outputBusLength = kb::scene::AudioSourceComponent::MaxOutputBusBytes + 1U;
    Require(!kb::scene::IsAudioSourceOutputBusValid(component)
            && !kb::scene::AudioSourceOutputBus(component).empty(),
        "Corrupted audio source output bus length was exposed as master routing");
}

void RunSceneAudioSourcePrefabRoundTripTest() {
    kb::scene::Scene source;
    const kb::scene::SceneObject root = source.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "AudioPrefab" });
    kb::scene::AudioSourceComponent sourceComponent{
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
    };
    Require(kb::scene::SetAudioSourceOutputBus(sourceComponent, "Effects"), "Audio prefab bus fixture was invalid");
    source.Components().AudioSources().Set(root.Entity(), sourceComponent);

    kb::scene::ScenePrefab prefab = source.Prefabs().Capture(root);
    kb::scene::Scene target;
    const kb::scene::ScenePrefabInstance instance = target.Prefabs().Instantiate(prefab);
    Require(!instance.Empty(), "Audio source prefab did not instantiate");

    const kb::scene::AudioSourceComponent* restored = target.Components().AudioSources().TryGet(instance.ObjectAt(0).Entity());
    Require(restored != nullptr && restored->clipAssetId == 1234 && NearlyEqual(restored->volume, 0.7F) && NearlyEqual(restored->pitch, 1.2F) && restored->loop && restored->spatial && !restored->autoplay && restored->enabled && !restored->mute && NearlyEqual(restored->pan, -0.25F) && NearlyEqual(restored->spatialBlend, 0.9F) && restored->attenuationModel == kb::audio::AudioAttenuationModel::None && NearlyEqual(restored->minDistance, 1.5F) && NearlyEqual(restored->maxDistance, 150.0F) && NearlyEqual(restored->rolloff, 0.8F) && NearlyEqual(restored->dopplerFactor, 1.5F) && kb::scene::AudioSourceOutputBus(*restored) == "Effects", "Audio source prefab component did not roundtrip");
}

void RunSceneAudioListenerPrefabRoundTripTest() {
    kb::scene::Scene source;
    const kb::scene::SceneObject root = source.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "AudioListenerPrefab" });
    source.Components().AudioListeners().Set(root.Entity(), kb::scene::AudioListenerComponent{
        .priority = 7,
        .localUser = kb::input::LocalUserId{ 2U },
        .primary = false,
        .enabled = false,
    });

    kb::scene::ScenePrefab prefab = source.Prefabs().Capture(root);
    kb::scene::Scene target;
    const kb::scene::ScenePrefabInstance instance = target.Prefabs().Instantiate(prefab);
    Require(!instance.Empty(), "Audio listener prefab did not instantiate");

    const kb::scene::AudioListenerComponent* restored = target.Components().AudioListeners().TryGet(instance.ObjectAt(0).Entity());
    Require(restored != nullptr && restored->priority == 7 && restored->localUser == kb::input::LocalUserId{ 2U }
            && !restored->primary && !restored->enabled,
        "Audio listener prefab component did not roundtrip");
}

void RunSceneAudioDocumentLoadSemanticsTest() {
    kb::scene::Scene scene;
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, 101U);
    kb::scene::SceneAudioMixerAccess::SetActiveSnapshot(scene, "Existing");
    kb::tests::Require(kb::scene::SceneAudioMixerAccess::SetBusVolumeOverride(scene, "ExistingBus", 0.6F),
        "Additive audio mixer override fixture setup failed");
    kb::tests::Require(kb::scene::SceneAudioMixerAccess::BeginSnapshotTransition(scene, "Later", 3.0F),
        "Additive audio mixer transition fixture setup failed");
    kb::scene::SceneAudioOcclusionAccess::Configure(scene, kb::scene::AudioOcclusionSettings{
        .enabled = true, .occludedVolumeScale = 0.4F, .maxDistance = 40.0F,
        .layerMask = 3U, .maxRaycastsPerTick = 6U,
    });
    kb::scene::SceneAudioOcclusionAccess::PublishRuntimeStats(scene, kb::scene::AudioOcclusionRuntimeStats{ .sampleRequests = 5U, .raycasts = 4U, .occludedSamples = 2U });

    kb::scene::Scene additiveSource;
    static_cast<void>(additiveSource.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "AdditiveRoot" }));
    kb::scene::SceneDocument additive = kb::scene::SceneDocumentService::Capture(additiveSource, "Additive");
    additive.audioMixerAssetId = 202U;
    additive.audioMixerSnapshot = "Incoming";
    additive.audioOcclusionSettings = {};
    Require(kb::scene::SceneDocumentService::LoadIntoSceneAdditive(scene, additive).succeeded,
        "Additive scene audio semantics fixture did not load");
    const kb::scene::AudioOcclusionRuntimeStats additiveStats = kb::scene::SceneAudioOcclusionAccess::RuntimeStats(scene);
    Require(kb::scene::SceneAudioMixerAccess::ActiveMixer(scene) == 101U
            && kb::scene::SceneAudioMixerAccess::ActiveSnapshot(scene) == "Existing"
            && kb::scene::SceneAudioMixerAccess::BusVolumeOverrides(scene).size() == 1U
            && kb::scene::SceneAudioMixerAccess::SnapshotTransition(scene).IsActive()
            && kb::scene::SceneAudioOcclusionAccess::Settings(scene).enabled
            && additiveStats.sampleRequests == 5U && additiveStats.raycasts == 4U,
        "Additive scene load overwrote scene-global authored or runtime audio state");

    kb::scene::SceneDocument invalid = additive;
    invalid.audioMixerSnapshot = "invalid snapshot";
    Require(!kb::scene::SceneDocumentService::LoadIntoScene(scene, invalid),
        "Non-additive scene load accepted an invalid authored audio snapshot name");
    Require(kb::scene::SceneAudioMixerAccess::ActiveMixer(scene) == 101U,
        "Rejected scene audio configuration mutated the current scene");

    invalid.audioMixerSnapshot = "Valid";
    invalid.audioOcclusionSettings.maxDistance = std::numeric_limits<float>::infinity();
    Require(!kb::scene::SceneDocumentService::LoadIntoScene(scene, invalid),
        "Non-additive scene load accepted non-finite authored occlusion settings");
}

void RunSceneAudioDocumentBackwardCompatibilityTest() {
    CleanTempRoot();
    const std::filesystem::path sceneFile = TempRoot() / "LegacyAudioDefaults.21kbscene";

    kb::scene::SceneDocument current;
    current.guid = "scene:legacy-audio-defaults";
    current.name = "LegacyAudioDefaults";
    Require(kb::scene::SceneDocumentService::Save(current, sceneFile),
        "Legacy scene audio compatibility fixture could not be saved");

    std::vector<std::uint8_t> sceneBytes = ReadBytes(sceneFile);
    constexpr std::size_t authoredAudioBlockBytes = sizeof(std::uint64_t) + sizeof(std::uint32_t)
        + sizeof(std::uint8_t) + sizeof(float) * 2U + sizeof(std::uint32_t) * 2U;
    Require(sceneBytes.size() > authoredAudioBlockBytes,
        "Legacy scene audio compatibility fixture is shorter than the v32 audio block");
    sceneBytes.resize(sceneBytes.size() - authoredAudioBlockBytes);
    WriteUInt32(sceneBytes, kSceneMagic.size(), 31U);
    Require(WriteBytes(sceneFile, sceneBytes), "Legacy scene audio compatibility fixture could not be rewritten");

    const TestIntegrity integrity = ComputeIntegrity(sceneBytes);
    const std::filesystem::path metaFile = sceneFile.parent_path() / "LegacyAudioDefaults.meta";
    std::vector<std::uint8_t> metaBytes = ReadBytes(metaFile);
    std::size_t integrityOffset = 12U;
    for (std::uint32_t stringIndex = 0U; stringIndex < 4U; ++stringIndex) {
        integrityOffset = SkipString(metaBytes, integrityOffset);
    }
    WriteUInt64(metaBytes, integrityOffset, sceneBytes.size());
    WriteUInt64(metaBytes, integrityOffset + sizeof(std::uint64_t), integrity.hash);
    WriteUInt32(metaBytes, integrityOffset + sizeof(std::uint64_t) * 2U, integrity.checksum);
    Require(WriteBytes(metaFile, metaBytes), "Legacy scene audio meta fixture could not be rewritten");

    const kb::scene::SceneDocumentLoadResult loaded = kb::scene::SceneDocumentService::Load(sceneFile);
    Require(loaded.succeeded && loaded.document.fileVersion == 31U
            && loaded.document.audioMixerAssetId == 0U && loaded.document.audioMixerSnapshot.empty()
            && !loaded.document.audioOcclusionSettings.enabled
            && NearlyEqual(loaded.document.audioOcclusionSettings.occludedVolumeScale, 0.35F)
            && NearlyEqual(loaded.document.audioOcclusionSettings.maxDistance, 100.0F)
            && loaded.document.audioOcclusionSettings.layerMask == 0xFFFFFFFFU
            && loaded.document.audioOcclusionSettings.maxRaycastsPerTick == 8U,
        "Pre-v32 scene did not load explicit authored audio defaults");

    kb::scene::Scene target;
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(target, 44U);
    kb::scene::SceneAudioMixerAccess::SetActiveSnapshot(target, "Stale");
    kb::scene::SceneAudioOcclusionAccess::Configure(target, kb::scene::AudioOcclusionSettings{ .enabled = true });
    Require(kb::scene::SceneDocumentService::LoadFileIntoScene(target, sceneFile),
        "Pre-v32 scene could not be reloaded into a runtime scene");
    Require(kb::scene::SceneAudioMixerAccess::ActiveMixer(target) == 0U
            && kb::scene::SceneAudioMixerAccess::ActiveSnapshot(target).empty()
            && !kb::scene::SceneAudioOcclusionAccess::Settings(target).enabled,
        "Pre-v32 scene reload leaked scene-global audio state");
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

void RunSceneTagSingleSelectionTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity first = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "FirstTagged" });
    const kb::scene::SceneEntity second = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "SecondTagged" });

    Require(scene.Tags().SetAssigned(first, "Enemy", true), "Initial scene tag assignment failed");
    Require(scene.Tags().SetAssigned(first, "Boss", true), "Replacing a scene tag assignment failed");
    const kb::scene::TagsComponent* replaced = scene.Components().Tags().TryGet(first);
    Require(replaced != nullptr && kb::scene::TagsText(*replaced) == "Boss",
        "Assigning a second tag must replace the previous entity classification");
    Require(!scene.Tags().IsAssigned(first, "Enemy") && scene.Tags().IsAssigned(first, "Boss"),
        "Scene tag queries exposed more than one classification after replacement");

    Require(scene.Tags().SetAssigned(second, "Boss", true) && scene.Tags().Undefine("Boss"),
        "Scene tag definition removal fixture failed");
    Require(!scene.Components().Tags().Has(first) && !scene.Components().Tags().Has(second),
        "Removing a tag definition must clear that tag from every entity");

    Require(scene.Tags().SetAssigned(first, "Player", true), "Built-in scene tag assignment fixture failed");
    for (const std::string_view builtIn : kb::scene::SceneTagCatalog::DefaultNames) {
        Require(kb::scene::SceneTagCatalog::IsBuiltIn(builtIn), "Built-in scene tag was not classified as immutable");
        Require(!scene.Tags().Undefine(builtIn) && scene.Tags().Contains(builtIn),
            "Built-in scene tag definition was removed");
    }
    Require(scene.Tags().IsAssigned(first, "Player"),
        "Rejected built-in scene tag removal cleared an existing assignment");

    const std::array<std::string, 1U> replacementDefinitions{ "QuestTarget" };
    Require(scene.Tags().ReplaceDefinitions(replacementDefinitions),
        "Replacing the scene tag catalogue with a custom definition failed");
    for (const std::string_view builtIn : kb::scene::SceneTagCatalog::DefaultNames) {
        Require(scene.Tags().Contains(builtIn),
            "Replacing scene tag definitions removed a built-in tag");
    }
    Require(scene.Tags().Contains("QuestTarget"),
        "Replacing scene tag definitions discarded a custom tag");

    kb::scene::TagsComponent legacy;
    kb::scene::SetTagsText(legacy, "Player, Enemy");
    scene.Components().Tags().Set(first, legacy);
    const kb::scene::TagsComponent* migrated = scene.Components().Tags().TryGet(first);
    Require(migrated != nullptr && kb::scene::TagsText(*migrated) == "Player" && !scene.Tags().IsAssigned(first, "Enemy"),
        "Legacy multi-tag component data was not migrated to one deterministic classification");

    for (std::size_t index = scene.Tags().Names().size(); index < kb::scene::SceneTagCatalog::MaxDefinitions; ++index) {
        Require(scene.Tags().Define("CapacityTag" + std::to_string(index)), "Scene tag catalogue capacity fixture failed");
    }
    Require(scene.Tags().Define("Player") && scene.Tags().SetAssigned(first, "Player", true),
        "A full scene tag catalogue must still accept existing definitions and assignments");
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
    RunSceneRadianceEmitterReflectionSerializationTest();
    RunSceneAmbientRadianceReflectionSerializationTest();
    RunSceneDetailSwitchReflectionSerializationTest();
    RunSceneVisibilityBlockerReflectionSerializationTest();
    RunSceneVisibilityCellReflectionSerializationTest();
    RunSceneSecondaryFrameReflectionSerializationTest();
    RunSceneGeometrySwarmReflectionSerializationTest();
    RunSceneSurfaceCastReflectionSerializationTest();
    RunSceneFacingPanelReflectionSerializationTest();
    RunSceneSpaceStrokeReflectionSerializationTest();
    RunSceneSpaceStrokePrefabRoundTripTest();
    RunSceneHistoryRibbonReflectionSerializationTest();
    RunSceneHistoryRibbonPrefabRoundTripTest();
    RunSceneAudioListenerComponentReflectionSerializationTest();
    RunSceneAudioSourceComponentReflectionSerializationTest();
    RunSceneAudioSourceOutputBusValidationTest();
    RunSceneAudioListenerPrefabRoundTripTest();
    RunSceneAudioSourcePrefabRoundTripTest();
    RunSceneAudioDocumentLoadSemanticsTest();
    RunSceneAudioDocumentBackwardCompatibilityTest();
    RunScenePhysicsComponentReflectionSerializationTest();
    RunEmptySceneDocumentClearsRuntimeSceneTest();
    RunSceneTagSingleSelectionTest();
    RunSceneDocumentLoadDoesNotTickRuntimeSystemsTest();
    RunSceneDocumentAssetDiscoveryTest();
    RunSceneAssetWritesMetaAndLoadsThroughSceneSystemTest();
    RunSceneAssetRejectsChecksumMismatchTest();
}

} // namespace kb::tests
