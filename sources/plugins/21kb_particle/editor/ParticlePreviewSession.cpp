#include "ParticlePreviewSession.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/ParticleEffectComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneLightingAccess.hpp"
#include "engine/scene/SceneMode.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"
#include "kb/render/Renderer.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace kb::particle_editor {
namespace {

constexpr float kParticlePreviewMinDistance = 1.0F;
constexpr float kParticlePreviewMaxDistance = 24.0F;
constexpr float kParticlePreviewMaxPitchDegrees = 85.0F;
constexpr float kParticlePreviewDegreesPerPixel = 0.4F;

[[nodiscard]] ParticleEditorResult ValidatePreviewAsset(const kb::scene::ParticleEffectAsset& asset) {
    auto validation = kb::scene::ParticleEffectAssetValidator::ValidateStructure(asset);
    if (!validation.Succeeded()) {
        return { .status = ParticleEditorStatus::InvalidAsset,
                 .message = "particle preview rejected an invalid working copy",
                 .diagnostics = std::move(validation.diagnostics) };
    }
    return {};
}

} // namespace

ParticlePreviewSession::~ParticlePreviewSession() = default;

ParticleEditorResult ParticlePreviewSession::Start(
    const kb::project::ProjectDescriptor& project,
    const kb::assets::AssetRegistry& sourceRegistry,
    kb::assets::AssetId assetId,
    std::filesystem::path virtualPath,
    const kb::scene::ParticleEffectAsset& asset) {
    if (scene_ != nullptr) {
        return { .status = ParticleEditorStatus::RuntimeFailure,
                 .message = "particle preview session is already active" };
    }
    if (!assetId.IsValid() || virtualPath.empty()) {
        return { .status = ParticleEditorStatus::InvalidAsset,
                 .message = "particle preview requires a stable asset id and virtual path" };
    }
    ParticleEditorResult validation = ValidatePreviewAsset(asset);
    if (!validation.Succeeded()) return validation;

    auto candidate = std::make_unique<kb::scene::Scene>(project, kb::scene::SceneMode::Runtime);
    if (!candidate->IsModuleActive("Rendering.21kbParticle") ||
        !kb::particles::ParticlePlayback::HasBackend(*candidate)) {
        return { .status = ParticleEditorStatus::ProviderUnavailable,
                 .message = "particle preview requires Rendering.21kbParticle to be enabled and loaded" };
    }
    auto& manager = candidate->Assets().Manager();
    for (const kb::assets::AssetMetadata& metadata : sourceRegistry.All()) {
        static_cast<void>(manager.Registry().Upsert(metadata));
    }
    kb::assets::AssetMetadata effectMetadata{
        .id = assetId,
        .type = kb::scene::kParticleEffectAssetType,
        .name = asset.displayName,
        .virtualPath = virtualPath,
        .physicalPath = "__21kb_particle_editor_working_copy__",
        .contentHash = 1U,
        .runtimeLoadable = true,
    };
    static_cast<void>(manager.Registry().Upsert(std::move(effectMetadata)));
    if (!manager.PublishRuntimeAsset(assetId,
            std::make_shared<kb::scene::ParticleEffectAsset>(asset))) {
        return { .status = ParticleEditorStatus::PublicationFailed,
                 .message = manager.LastError() };
    }

    const kb::scene::SceneEntity effectEntity = candidate->Entities().CreateEntity();
    candidate->Transforms().Set(effectEntity, {});
    candidate->Components().ParticleEffects().Set(effectEntity, {
        .effectAssetId = assetId.value,
        .deterministicSeed = asset.determinismSeed,
        .enabled = true,
        .autoPlay = true,
        .followTransform = true,
        .restartOnActivate = true,
    });
    const kb::scene::SceneEntity cameraEntity = candidate->Entities().CreateEntity();
    candidate->Components().Cameras().Set(cameraEntity, {
        .projection = kb::scene::CameraProjection::Perspective,
        .verticalFovDegrees = 50.0F,
        .nearClip = 0.01F,
        .farClip = 1000.0F,
        .primary = true,
    });
    const kb::scene::SceneEntity environment = candidate->Entities().CreateEntity(
        kb::scene::SceneObjectDesc{.name = "Particle Preview Environment"});
    candidate->Components().WorldBackdrops().Set(environment, kb::scene::WorldBackdropComponent{
        .mode = kb::scene::WorldBackdropMode::VerticalGradient,
        .horizonColor = kb::scene::Vec3{0.018F, 0.020F, 0.026F},
        .zenithColor = kb::scene::Vec3{0.045F, 0.050F, 0.068F},
        .gradientExponent = 1.15F,
        .priority = 100,
        .enabled = true,
    });
    candidate->Components().AmbientRadiances().Set(environment, kb::scene::AmbientRadianceComponent{
        .mode = kb::scene::AmbientRadianceMode::Gradient,
        .horizonColor = kb::scene::Vec3{0.04F, 0.045F, 0.055F},
        .zenithColor = kb::scene::Vec3{0.08F, 0.09F, 0.12F},
        .intensity = 0.45F,
        .priority = 100,
        .enabled = true,
    });
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(*candidate, true);

    scene_ = std::move(candidate);
    assetId_ = assetId;
    virtualPath_ = std::move(virtualPath);
    effectEntity_ = effectEntity;
    cameraEntity_ = cameraEntity;
    orbitYawDegrees_ = 0.0F;
    orbitPitchDegrees_ = 12.0F;
    cameraDistance_ = 5.0F;
    orbitDragging_ = false;
    ApplyCamera();
    return {};
}

ParticleEditorResult ParticlePreviewSession::PublishWorkingCopy(
    const kb::scene::ParticleEffectAsset& asset,
    bool restartLiveInstances) {
    if (scene_ == nullptr) {
        return { .status = ParticleEditorStatus::RuntimeFailure,
                 .message = "particle preview session is not active" };
    }
    ParticleEditorResult validation = ValidatePreviewAsset(asset);
    if (!validation.Succeeded()) return validation;
    auto& manager = scene_->Assets().Manager();
    kb::assets::AssetMetadata* metadata = manager.Registry().FindMutable(assetId_);
    if (metadata == nullptr) {
        return { .status = ParticleEditorStatus::PublicationFailed,
                 .message = "particle preview runtime metadata is missing" };
    }
    ++metadata->contentHash;
    metadata->name = asset.displayName;
    if (!manager.PublishRuntimeAsset(assetId_,
            std::make_shared<kb::scene::ParticleEffectAsset>(asset))) {
        return { .status = ParticleEditorStatus::PublicationFailed,
                 .message = manager.LastError() };
    }
    // RefreshCompiledEffects runs on the fixed step, and it already repoints live
    // instances at the new compiled effect - resetting them only when the change is
    // topologically incompatible. A restart here is therefore additional, and only
    // warranted for edits the running simulation cannot surface by itself.
    if (!scene_->Runtime().Update(kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds)) {
        return { .status = ParticleEditorStatus::RuntimeFailure,
                 .message = "particle preview could not compile the published working copy" };
    }
    if (!restartLiveInstances) {
        return {};
    }
    for (const std::uint64_t instanceId : kb::particles::ParticlePlayback::LiveInstanceIds(*scene_)) {
        const auto restarted = kb::particles::ParticlePlayback::Restart(*scene_, instanceId);
        if (!restarted.Succeeded()) {
            return { .status = ParticleEditorStatus::RuntimeFailure,
                     .message = "particle preview could not restart after publishing the working copy" };
        }
    }
    return {};
}

ParticleEditorResult ParticlePreviewSession::RetargetWorkingCopy(
    kb::assets::AssetId assetId,
    std::filesystem::path virtualPath,
    const kb::scene::ParticleEffectAsset& asset) {
    if (scene_ == nullptr || !assetId.IsValid() || virtualPath.empty()) {
        return { .status = ParticleEditorStatus::RuntimeFailure,
                 .message = "particle preview retarget requires an active session and stable asset identity" };
    }
    ParticleEditorResult validation = ValidatePreviewAsset(asset);
    if (!validation.Succeeded()) return validation;

    auto& manager = scene_->Assets().Manager();
    kb::assets::AssetMetadata* metadata = manager.Registry().FindMutable(assetId);
    if (metadata == nullptr) {
        return { .status = ParticleEditorStatus::PublicationFailed,
                 .message = "particle preview retarget metadata is missing" };
    }
    kb::assets::AssetMetadata replacementMetadata = *metadata;
    replacementMetadata.name = asset.displayName;
    replacementMetadata.virtualPath = virtualPath;
    ++replacementMetadata.contentHash;
    if (!manager.Registry().Upsert(std::move(replacementMetadata))) {
        return { .status = ParticleEditorStatus::PublicationFailed,
                 .message = "particle preview retarget metadata update failed" };
    }
    if (!manager.PublishRuntimeAsset(
            assetId,
            std::make_shared<kb::scene::ParticleEffectAsset>(asset))) {
        return { .status = ParticleEditorStatus::PublicationFailed,
                 .message = manager.LastError() };
    }

    const kb::scene::ParticleEffectComponent* current =
        scene_->Components().ParticleEffects().TryGet(effectEntity_);
    if (current == nullptr) {
        return { .status = ParticleEditorStatus::RuntimeFailure,
                 .message = "particle preview retarget component is missing" };
    }
    kb::scene::ParticleEffectComponent replacement = *current;
    replacement.effectAssetId = assetId.value;
    replacement.deterministicSeed = asset.determinismSeed;
    scene_->Components().ParticleEffects().Set(effectEntity_, replacement);
    assetId_ = assetId;
    virtualPath_ = std::move(virtualPath);

    // The fixed-step component reconciler releases the previous instance and
    // creates the new asset instance with its own deterministic seed.
    if (!scene_->Runtime().Update(kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds)) {
        return { .status = ParticleEditorStatus::RuntimeFailure,
                 .message = "particle preview could not activate the retargeted working copy" };
    }
    for (const std::uint64_t instanceId :
         kb::particles::ParticlePlayback::LiveInstanceIds(*scene_)) {
        const kb::particles::ParticleRuntimeQueryResult query =
            kb::particles::ParticlePlayback::Query(*scene_, instanceId);
        if (query.assetId != assetId.value) continue;
        const auto restarted = kb::particles::ParticlePlayback::Restart(
            *scene_, instanceId);
        if (!restarted.Succeeded()) {
            return { .status = ParticleEditorStatus::RuntimeFailure,
                     .message = "particle preview could not restart the retargeted working copy" };
        }
    }
    return {};
}

ParticleEditorResult ParticlePreviewSession::Tick(float wallDeltaSeconds) {
    if (scene_ == nullptr || !std::isfinite(wallDeltaSeconds) || wallDeltaSeconds < 0.0F) {
        return { .status = ParticleEditorStatus::RuntimeFailure,
                 .message = "particle preview tick requires an active session and finite non-negative delta" };
    }
    if (!scene_->Runtime().Update(wallDeltaSeconds)) {
        return { .status = ParticleEditorStatus::RuntimeFailure,
                 .message = "particle preview runtime update failed" };
    }
    return {};
}

bool ParticlePreviewSession::Submit(kb::render::Renderer& renderer) const {
    if (scene_ == nullptr || !renderer.IsFrameActive()) return false;
    renderer.SubmitScene(*scene_);
    return true;
}

void ParticlePreviewSession::Release(kb::render::Renderer& renderer) {
    Release([&renderer](const kb::scene::Scene& scene) { renderer.ReleaseScene(scene); });
}

void ParticlePreviewSession::Release(
    const std::function<void(const kb::scene::Scene&)>& releaseRendererScene) {
    if (scene_ != nullptr) {
        if (!releaseRendererScene) {
            throw std::logic_error{"particle preview release requires the renderer scene-release callback"};
        }
        releaseRendererScene(*scene_);
    }
    scene_.reset();
    assetId_ = {};
    virtualPath_.clear();
    effectEntity_ = {};
    cameraEntity_ = {};
    orbitDragging_ = false;
}

bool ParticlePreviewSession::Active() const noexcept { return scene_ != nullptr; }
const kb::scene::Scene& ParticlePreviewSession::PreviewScene() const {
    if (scene_ == nullptr) throw std::logic_error{"particle preview session is not active"};
    return *scene_;
}
kb::scene::Scene& ParticlePreviewSession::PreviewScene() {
    if (scene_ == nullptr) throw std::logic_error{"particle preview session is not active"};
    return *scene_;
}
kb::scene::SceneEntity ParticlePreviewSession::EffectEntity() const noexcept { return effectEntity_; }
kb::scene::SceneEntity ParticlePreviewSession::CameraEntity() const noexcept { return cameraEntity_; }

void ParticlePreviewSession::ApplyCamera() {
    if (scene_ == nullptr) return;
    constexpr float degToRad = kb::math::kPi / 180.0F;
    const float pitch = std::clamp(orbitPitchDegrees_, -kParticlePreviewMaxPitchDegrees,
                                   kParticlePreviewMaxPitchDegrees) * degToRad;
    const float yaw = orbitYawDegrees_ * degToRad;
    const float cosPitch = std::cos(pitch);
    const kb::math::Vec3 eye{
        cameraDistance_ * cosPitch * std::sin(yaw),
        cameraDistance_ * std::sin(pitch),
        -cameraDistance_ * cosPitch * std::cos(yaw),
    };
    kb::scene::TransformComponent transform{};
    transform.localPosition = eye;
    const float lengthSquared = eye.x * eye.x + eye.y * eye.y + eye.z * eye.z;
    if (lengthSquared > 0.000001F)
        transform.localRotation = kb::math::LookRotation(kb::math::Normalize(kb::math::Vec3{-eye.x, -eye.y, -eye.z}),
            kb::math::Vec3{0.0F, 1.0F, 0.0F});
    scene_->Transforms().Set(cameraEntity_, transform);
}

bool ParticlePreviewSession::SetCameraOrbit(float yawDegrees, float pitchDegrees, float distance) {
    if (scene_ == nullptr) return false;
    const float yaw = yawDegrees;
    const float pitch = std::clamp(pitchDegrees, -kParticlePreviewMaxPitchDegrees, kParticlePreviewMaxPitchDegrees);
    const float clampedDistance = std::clamp(distance, kParticlePreviewMinDistance, kParticlePreviewMaxDistance);
    if (yaw == orbitYawDegrees_ && pitch == orbitPitchDegrees_ && clampedDistance == cameraDistance_)
        return false;
    orbitYawDegrees_ = yaw;
    orbitPitchDegrees_ = pitch;
    cameraDistance_ = clampedDistance;
    ApplyCamera();
    return true;
}

bool ParticlePreviewSession::OrbitCamera(float deltaYawDegrees, float deltaPitchDegrees) {
    return SetCameraOrbit(orbitYawDegrees_ + deltaYawDegrees, orbitPitchDegrees_ + deltaPitchDegrees, cameraDistance_);
}

bool ParticlePreviewSession::ZoomCamera(float scale) {
    if (!std::isfinite(scale) || scale <= 0.0F) return false;
    return SetCameraOrbit(orbitYawDegrees_, orbitPitchDegrees_, cameraDistance_ * scale);
}

bool ParticlePreviewSession::BeginOrbit(int x, int y) noexcept {
    if (scene_ == nullptr) return false;
    orbitDragging_ = true;
    orbitLastX_ = x;
    orbitLastY_ = y;
    return true;
}

bool ParticlePreviewSession::DragOrbit(int x, int y) {
    if (!orbitDragging_) return false;
    const float deltaYaw = -static_cast<float>(x - orbitLastX_) * kParticlePreviewDegreesPerPixel;
    const float deltaPitch = static_cast<float>(y - orbitLastY_) * kParticlePreviewDegreesPerPixel;
    orbitLastX_ = x;
    orbitLastY_ = y;
    return OrbitCamera(deltaYaw, deltaPitch);
}

bool ParticlePreviewSession::EndOrbit() noexcept {
    if (!orbitDragging_) return false;
    orbitDragging_ = false;
    return true;
}

bool ParticlePreviewSession::IsOrbiting() const noexcept { return orbitDragging_; }
float ParticlePreviewSession::OrbitYawDegrees() const noexcept { return orbitYawDegrees_; }
float ParticlePreviewSession::OrbitPitchDegrees() const noexcept { return orbitPitchDegrees_; }
float ParticlePreviewSession::CameraDistance() const noexcept { return cameraDistance_; }

} // namespace kb::particle_editor
