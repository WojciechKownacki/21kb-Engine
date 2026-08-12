#include "script/AudioShooterDemoProjectFiles.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputAssetIO.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/InputComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

constexpr std::string_view kControllerVirtual = "/Game/Samples/AudioShooter/AudioShooterController.lua";
constexpr std::string_view kProjectileScriptVirtual = "/Game/Samples/AudioShooter/AudioProjectile.lua";
constexpr std::string_view kCubeMeshVirtual = "/Game/Samples/AudioShooter/DemoCube.obj";
constexpr std::string_view kEngineClipVirtual = "/Game/Samples/AudioShooter/EngineLoop.wav";
constexpr std::string_view kFireActionVirtual = "/Game/Samples/AudioShooter/Fire.21kbinputaction";
constexpr std::string_view kInputContextVirtual = "/Game/Samples/AudioShooter/AudioShooter.21kbinputcontext";

constexpr std::string_view kControllerLua = R"lua(-- AudioShooterController.lua
-- A self-contained audio demo controller. The entity flies continuously,
-- spawns a cube projectile on Space/Fire, and plays a spatial one-shot.
-- @expose flightSpeed Float = 3.5

function Ready(self, dt)
    Log("Audio Shooter ready - press Space to fire")
end

function Tick(self, dt)
    local speed = self:GetVariable("flightSpeed") or 3.5
    Transform.Translate(self.entity, 0.0, 0.0, speed * dt)

    if not Input.Pressed("Fire") then
        return
    end

    local position = Transform.GetPosition(self.entity)
    if position == nil then
        return
    end

    World.InstantiatePrefab({
        prefab = "/Game/Samples/AudioShooter/AudioProjectile.kbprefab",
        x = position.x,
        y = position.y,
        z = position.z + 1.8
    })

    local voice, audioError = Audio.Play("/Game/Samples/AudioShooter/Shot.wav", {
        entity = self.entity,
        volume = 0.65,
        spatial = true,
        spatialBlend = 1.0,
        minDistance = 1.0,
        maxDistance = 45.0,
        rolloff = 1.0,
        priority = 180
    })
    if voice == nil then
        Log("Audio Shooter could not play Shot.wav: " .. tostring(audioError))
    end
end
)lua";

constexpr std::string_view kProjectileLua = R"lua(-- AudioProjectile.lua
-- Visible, gravity-free demo projectile with per-instance state.
-- @expose launched Bool = false
-- @expose lifetime Float = 0.0

local speed = 18.0
local maximumLifetime = 4.0

function Tick(self, dt)
    if not self:GetVariable("launched") then
        if Physics.SetVelocity(self.entity, 0.0, 0.0, speed) then
            self:SetVariable("launched", true)
        end
    end

    local lifetime = (self:GetVariable("lifetime") or 0.0) + dt
    self:SetVariable("lifetime", lifetime)
    if lifetime >= maximumLifetime then
        World.Destroy(self.entity)
    end
end

function OnCollisionEnter(self, event)
    World.Destroy(self.entity)
end
)lua";

constexpr std::string_view kCubeObj = R"obj(# Unit cube used by the Audio Shooter demo.
v -0.5 -0.5 -0.5
v  0.5 -0.5 -0.5
v  0.5  0.5 -0.5
v -0.5  0.5 -0.5
v -0.5 -0.5  0.5
v  0.5 -0.5  0.5
v  0.5  0.5  0.5
v -0.5  0.5  0.5
f 1 4 3 2
f 5 6 7 8
f 1 5 8 4
f 2 3 7 6
f 1 2 6 5
f 4 8 7 3
)obj";

[[nodiscard]] kb::assets::AssetId AssetId(std::string_view virtualPath, std::string_view type) {
    return kb::assets::MakeAssetId(kb::assets::NormalizeAssetPath(std::filesystem::path{ virtualPath }) + ":" + std::string{ type });
}

[[nodiscard]] bool ExistingFile(const std::filesystem::path& path, bool& exists, std::string& error) {
    std::error_code errorCode;
    const std::filesystem::file_status status = std::filesystem::status(path, errorCode);
    if (errorCode == std::errc::no_such_file_or_directory) {
        exists = false;
        return true;
    }
    if (errorCode) {
        error = "could not inspect demo asset: " + path.string();
        return false;
    }
    exists = std::filesystem::is_regular_file(status);
    if (!exists && std::filesystem::exists(status)) {
        error = "demo asset path is not a regular file: " + path.string();
        return false;
    }
    return true;
}

[[nodiscard]] bool EnsureParentDirectory(const std::filesystem::path& path, std::string& error) {
    std::error_code errorCode;
    std::filesystem::create_directories(path.parent_path(), errorCode);
    if (errorCode) {
        error = "could not create directory: " + path.parent_path().string();
        return false;
    }
    return true;
}

void RecordWritten(ScriptAgentProjectFilesResult& result, const std::filesystem::path& path) {
    result.writtenFiles.push_back(path);
    result.wroteProjectAsset = true;
}

[[nodiscard]] bool WriteTextOnce(
    const std::filesystem::path& path,
    std::string_view content,
    ScriptAgentProjectFilesResult& result) {
    bool exists = false;
    if (!ExistingFile(path, exists, result.error)) {
        return false;
    }
    if (exists) {
        result.skippedFiles.push_back(path);
        return true;
    }
    if (!EnsureParentDirectory(path, result.error)) {
        return false;
    }
    const std::span<const std::uint8_t> bytes{
        reinterpret_cast<const std::uint8_t*>(content.data()), content.size()
    };
    if (!kb::scene::SceneAssetBinaryIO::WriteBytesAtomically(path, bytes)) {
        result.error = "could not write file: " + path.string();
        return false;
    }
    RecordWritten(result, path);
    return true;
}

void AppendUInt16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void AppendUInt32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void AppendText(std::vector<std::uint8_t>& bytes, std::string_view text) {
    bytes.insert(bytes.end(), text.begin(), text.end());
}

template <typename SampleGenerator>
[[nodiscard]] std::vector<std::uint8_t> BuildMonoPcm16Wave(
    std::uint32_t sampleRate,
    std::uint32_t sampleCount,
    SampleGenerator&& sampleGenerator) {
    constexpr std::uint16_t kChannels = 1U;
    constexpr std::uint16_t kBitsPerSample = 16U;
    constexpr std::uint16_t kBlockAlign = kChannels * (kBitsPerSample / 8U);
    const std::uint32_t dataBytes = sampleCount * kBlockAlign;

    std::vector<std::uint8_t> bytes;
    bytes.reserve(44U + dataBytes);
    AppendText(bytes, "RIFF");
    AppendUInt32(bytes, 36U + dataBytes);
    AppendText(bytes, "WAVEfmt ");
    AppendUInt32(bytes, 16U);
    AppendUInt16(bytes, 1U);
    AppendUInt16(bytes, kChannels);
    AppendUInt32(bytes, sampleRate);
    AppendUInt32(bytes, sampleRate * kBlockAlign);
    AppendUInt16(bytes, kBlockAlign);
    AppendUInt16(bytes, kBitsPerSample);
    AppendText(bytes, "data");
    AppendUInt32(bytes, dataBytes);

    for (std::uint32_t sampleIndex = 0U; sampleIndex < sampleCount; ++sampleIndex) {
        const float sample = std::clamp(sampleGenerator(sampleIndex), -1.0F, 1.0F);
        const auto pcm = static_cast<std::int16_t>(std::lround(sample * 32767.0F));
        AppendUInt16(bytes, static_cast<std::uint16_t>(pcm));
    }
    return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> BuildShotWave() {
    constexpr std::uint32_t kSampleRate = 22050U;
    constexpr std::uint32_t kSampleCount = kSampleRate / 5U;
    double phase = 0.0;
    return BuildMonoPcm16Wave(kSampleRate, kSampleCount, [&phase](std::uint32_t sampleIndex) {
        const double progress = static_cast<double>(sampleIndex) / static_cast<double>(kSampleCount);
        const double frequency = 1050.0 - (760.0 * progress);
        phase += (2.0 * std::numbers::pi * frequency) / static_cast<double>(kSampleRate);
        const double envelope = (1.0 - progress) * (1.0 - progress);
        return static_cast<float>(std::sin(phase) * envelope * 0.72);
    });
}

[[nodiscard]] std::vector<std::uint8_t> BuildEngineLoopWave() {
    constexpr std::uint32_t kSampleRate = 22000U;
    constexpr std::uint32_t kSampleCount = kSampleRate / 2U;
    return BuildMonoPcm16Wave(kSampleRate, kSampleCount, [](std::uint32_t sampleIndex) {
        const double time = static_cast<double>(sampleIndex) / static_cast<double>(kSampleRate);
        const double fundamental = std::sin(2.0 * std::numbers::pi * 110.0 * time);
        const double harmonic = std::sin(2.0 * std::numbers::pi * 220.0 * time);
        return static_cast<float>((fundamental * 0.14) + (harmonic * 0.04));
    });
}

[[nodiscard]] bool WriteBytesOnce(
    const std::filesystem::path& path,
    std::span<const std::uint8_t> bytes,
    ScriptAgentProjectFilesResult& result) {
    bool exists = false;
    if (!ExistingFile(path, exists, result.error)) {
        return false;
    }
    if (exists) {
        result.skippedFiles.push_back(path);
        return true;
    }
    if (!EnsureParentDirectory(path, result.error)) {
        return false;
    }
    if (!kb::scene::SceneAssetBinaryIO::WriteBytesAtomically(path, bytes)) {
        result.error = "could not write file: " + path.string();
        return false;
    }
    RecordWritten(result, path);
    return true;
}

template <typename Writer>
[[nodiscard]] bool WriteAssetOnce(
    const std::filesystem::path& path,
    ScriptAgentProjectFilesResult& result,
    Writer&& writer) {
    bool exists = false;
    if (!ExistingFile(path, exists, result.error)) {
        return false;
    }
    if (exists) {
        result.skippedFiles.push_back(path);
        return true;
    }
    if (!EnsureParentDirectory(path, result.error)) {
        return false;
    }
    if (!writer(path)) {
        result.error = "could not write demo asset: " + path.string();
        return false;
    }
    RecordWritten(result, path);
    return true;
}

[[nodiscard]] bool WriteInputAssets(
    const std::filesystem::path& demoRoot,
    ScriptAgentProjectFilesResult& result) {
    const std::filesystem::path firePath = demoRoot / "Fire.21kbinputaction";
    if (!WriteAssetOnce(firePath, result, [](const std::filesystem::path& path) {
            return kb::input::WriteInputAction(path, kb::input::InputActionAsset{
                .name = "Fire",
                .valueType = kb::input::InputActionValueType::Bool,
                .consumeInput = true,
            });
        })) {
        return false;
    }

    const kb::assets::AssetId fireActionId = AssetId(kFireActionVirtual, "InputAction");
    return WriteAssetOnce(demoRoot / "AudioShooter.21kbinputcontext", result, [fireActionId](const std::filesystem::path& path) {
        kb::input::InputMappingContextAsset context;
        context.mappings.push_back(kb::input::InputKeyMapping{
            .bindingId = 1U,
            .actionId = fireActionId.value,
            .key = kb::input::InputKey::Space,
        });
        return kb::input::WriteInputMappingContext(path, context);
    });
}

[[nodiscard]] bool WriteProjectilePrefab(
    const std::filesystem::path& path,
    ScriptAgentProjectFilesResult& result) {
    const kb::assets::AssetId projectileScriptId = AssetId(kProjectileScriptVirtual, "LuaScript");
    const kb::assets::AssetId cubeMeshId = AssetId(kCubeMeshVirtual, "RenderMesh");
    return WriteAssetOnce(path, result, [projectileScriptId, cubeMeshId](const std::filesystem::path& assetPath) {
        kb::scene::Scene source;
        kb::scene::SceneObjectDesc projectileDesc{ .name = "Audio Projectile" };
        projectileDesc.transform.localScale = kb::scene::Vec3{ 0.35F, 0.35F, 0.35F };
        const kb::scene::SceneObject projectile = source.Entities().CreateObject(projectileDesc);
        source.Components().MeshRenderers().Set(projectile.Entity(), kb::scene::MeshRendererComponent{
            .meshAssetId = cubeMeshId.value,
        });
        source.Components().Rigidbodies().Set(projectile.Entity(), kb::scene::RigidbodyComponent{
            .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
            .mass = 0.2F,
            .useGravity = false,
            .lockRotation = true,
            .useContinuousCollision = true,
        });
        source.Components().Colliders().Set(projectile.Entity(), kb::scene::ColliderComponent{
            .shape = kb::scene::ColliderShape::Box,
            .boxSize = kb::scene::Vec3{ 1.0F, 1.0F, 1.0F },
        });
        source.Components().Behaviours().Set(projectile.Entity(), kb::scene::BehaviourComponent{
            .behaviourAssetId = projectileScriptId.value,
            .backend = kb::scene::BehaviourBackend::Lua,
            .enabled = true,
        });
        const kb::scene::ScenePrefabHandle prefab = source.Prefabs().CaptureRegistered(projectile, "AudioProjectile");
        return source.Prefabs().Save(prefab, assetPath);
    });
}

void AddMarker(
    kb::scene::Scene& scene,
    kb::assets::AssetId cubeMeshId,
    std::string name,
    kb::scene::Vec3 position) {
    kb::scene::SceneObjectDesc markerDesc{ .name = std::move(name) };
    markerDesc.transform.localPosition = position;
    markerDesc.transform.localScale = kb::scene::Vec3{ 0.35F, 0.35F, 1.5F };
    const kb::scene::SceneObject marker = scene.Entities().CreateObject(markerDesc);
    scene.Components().MeshRenderers().Set(marker.Entity(), kb::scene::MeshRendererComponent{
        .meshAssetId = cubeMeshId.value,
    });
}

[[nodiscard]] bool WriteDemoScene(
    const std::filesystem::path& path,
    ScriptAgentProjectFilesResult& result) {
    const kb::assets::AssetId controllerId = AssetId(kControllerVirtual, "LuaScript");
    const kb::assets::AssetId cubeMeshId = AssetId(kCubeMeshVirtual, "RenderMesh");
    const kb::assets::AssetId engineClipId = AssetId(kEngineClipVirtual, "AudioClip");
    const kb::assets::AssetId inputContextId = AssetId(kInputContextVirtual, "InputMappingContext");

    return WriteAssetOnce(path, result, [controllerId, cubeMeshId, engineClipId, inputContextId](const std::filesystem::path& assetPath) {
        kb::scene::Scene scene;

        const kb::scene::SceneObject environment = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Environment" });
        scene.Components().WorldBackdrops().Set(environment.Entity(), kb::scene::WorldBackdropComponent{
            .mode = kb::scene::WorldBackdropMode::VerticalGradient,
            .horizonColor = kb::scene::Vec3{ 0.035F, 0.055F, 0.10F },
            .zenithColor = kb::scene::Vec3{ 0.005F, 0.010F, 0.025F },
            .gradientExponent = 1.4F,
        });
        scene.Components().AmbientRadiances().Set(environment.Entity(), kb::scene::AmbientRadianceComponent{
            .mode = kb::scene::AmbientRadianceMode::Gradient,
            .color = kb::scene::Vec3{ 0.10F, 0.14F, 0.22F },
            .horizonColor = kb::scene::Vec3{ 0.12F, 0.16F, 0.25F },
            .zenithColor = kb::scene::Vec3{ 0.03F, 0.05F, 0.12F },
            .intensity = 1.15F,
        });

        const kb::scene::SceneObject ship = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Player Ship" });
        scene.Components().Behaviours().Set(ship.Entity(), kb::scene::BehaviourComponent{
            .behaviourAssetId = controllerId.value,
            .backend = kb::scene::BehaviourBackend::Lua,
            .enabled = true,
        });
        scene.Components().Inputs().Set(ship.Entity(), kb::scene::InputComponent{
            .mappingContextAssetId = inputContextId.value,
            .priority = 100,
            .enabled = true,
        });
        scene.Components().AudioSources().Set(ship.Entity(), kb::scene::AudioSourceComponent{
            .clipAssetId = engineClipId.value,
            .volume = 0.14F,
            .pitch = 1.0F,
            .loop = true,
            .spatial = false,
            .autoplay = true,
            .spatialBlend = 0.0F,
        });

        kb::scene::SceneObjectDesc shipBodyDesc{ .name = "Ship Body", .parent = ship };
        shipBodyDesc.transform.localScale = kb::scene::Vec3{ 1.15F, 0.5F, 1.8F };
        const kb::scene::SceneObject shipBody = scene.Entities().CreateObject(shipBodyDesc);
        scene.Components().MeshRenderers().Set(shipBody.Entity(), kb::scene::MeshRendererComponent{
            .meshAssetId = cubeMeshId.value,
        });

        kb::scene::SceneObjectDesc cameraDesc{ .name = "Follow Camera", .parent = ship };
        cameraDesc.transform.localPosition = kb::scene::Vec3{ 0.0F, 2.6F, -8.0F };
        cameraDesc.transform.localRotation = kb::scene::Quat{ 0.0871557F, 0.0F, 0.0F, 0.9961947F };
        const kb::scene::SceneObject camera = scene.Entities().CreateObject(cameraDesc);
        scene.Components().Cameras().Set(camera.Entity(), kb::scene::CameraComponent{
            .verticalFovDegrees = 62.0F,
            .nearClip = 0.05F,
            .farClip = 500.0F,
            .primary = true,
            .clearColor = kb::scene::Vec3{ 0.005F, 0.010F, 0.025F },
        });
        scene.Components().AudioListeners().Set(camera.Entity(), kb::scene::AudioListenerComponent{
            .priority = 100,
            .primary = true,
            .enabled = true,
        });

        kb::scene::SceneObjectDesc barrelDesc{ .name = "Launcher", .parent = ship };
        barrelDesc.transform.localPosition = kb::scene::Vec3{ 0.0F, 0.0F, 0.75F };
        barrelDesc.transform.localScale = kb::scene::Vec3{ 0.18F, 0.18F, 0.65F };
        const kb::scene::SceneObject barrel = scene.Entities().CreateObject(barrelDesc);
        scene.Components().MeshRenderers().Set(barrel.Entity(), kb::scene::MeshRendererComponent{
            .meshAssetId = cubeMeshId.value,
        });

        kb::scene::SceneObjectDesc beaconDesc{ .name = "Spatial Audio Beacon" };
        beaconDesc.transform.localPosition = kb::scene::Vec3{ 4.5F, 0.0F, 28.0F };
        beaconDesc.transform.localScale = kb::scene::Vec3{ 0.7F, 2.5F, 0.7F };
        const kb::scene::SceneObject beacon = scene.Entities().CreateObject(beaconDesc);
        scene.Components().MeshRenderers().Set(beacon.Entity(), kb::scene::MeshRendererComponent{
            .meshAssetId = cubeMeshId.value,
        });
        scene.Components().AudioSources().Set(beacon.Entity(), kb::scene::AudioSourceComponent{
            .clipAssetId = engineClipId.value,
            .volume = 0.38F,
            .pitch = 1.75F,
            .loop = true,
            .spatial = true,
            .autoplay = true,
            .spatialBlend = 1.0F,
            .minDistance = 2.0F,
            .maxDistance = 32.0F,
            .rolloff = 1.0F,
            .dopplerFactor = 1.0F,
        });

        for (int index = 1; index <= 6; ++index) {
            const float z = static_cast<float>(index) * 12.0F;
            AddMarker(scene, cubeMeshId, "Left Flight Marker " + std::to_string(index), kb::scene::Vec3{ -5.5F, -1.0F, z });
            AddMarker(scene, cubeMeshId, "Right Flight Marker " + std::to_string(index), kb::scene::Vec3{ 5.5F, -1.0F, z });
        }

        kb::scene::SceneObjectDesc lightDesc{ .name = "Key Light" };
        lightDesc.transform.localRotation = kb::scene::Quat{ 0.28F, -0.18F, 0.05F, 0.942F };
        const kb::scene::SceneObject light = scene.Entities().CreateObject(lightDesc);
        scene.Components().Lights().Set(light.Entity(), kb::scene::LightComponent{
            .kind = kb::scene::LightKind::Directional,
            .color = kb::scene::Vec3{ 0.72F, 0.82F, 1.0F },
            .intensity = 2.4F,
            .castsShadow = true,
        });

        return kb::scene::SceneDocumentService::Save(scene, assetPath, "Audio Shooter Demo");
    });
}

} // namespace

bool WriteAudioShooterDemoProjectFiles(
    const std::filesystem::path& projectRoot,
    ScriptAgentProjectFilesResult& result) {
    const std::filesystem::path demoRoot = projectRoot / "Assets" / "Samples" / "AudioShooter";
    const std::vector<std::uint8_t> shotWave = BuildShotWave();
    const std::vector<std::uint8_t> engineLoopWave = BuildEngineLoopWave();
    if (!WriteTextOnce(demoRoot / "AudioShooterController.lua", kControllerLua, result)
        || !WriteTextOnce(demoRoot / "AudioProjectile.lua", kProjectileLua, result)
        || !WriteTextOnce(demoRoot / "DemoCube.obj", kCubeObj, result)
        || !WriteBytesOnce(demoRoot / "Shot.wav", shotWave, result)
        || !WriteBytesOnce(demoRoot / "EngineLoop.wav", engineLoopWave, result)
        || !WriteInputAssets(demoRoot, result)
        || !WriteProjectilePrefab(demoRoot / "AudioProjectile.kbprefab", result)
        || !WriteDemoScene(projectRoot / "Assets" / "Scenes" / "AudioShooterDemo.21kbscene", result)) {
        return false;
    }
    return true;
}

} // namespace kb::script
