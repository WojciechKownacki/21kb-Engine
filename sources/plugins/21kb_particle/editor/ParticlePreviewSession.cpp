#include "ParticlePreviewSession.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/ParticleEffectComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneMode.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "kb/render/Renderer.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace kb::particle_editor {
namespace {

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
    kb::scene::TransformComponent cameraTransform{};
    cameraTransform.localPosition = {0.0F, 1.0F, -5.0F};
    candidate->Transforms().Set(cameraEntity, cameraTransform);
    candidate->Components().Cameras().Set(cameraEntity, {
        .projection = kb::scene::CameraProjection::Perspective,
        .verticalFovDegrees = 60.0F,
        .nearClip = 0.01F,
        .farClip = 1000.0F,
        .primary = true,
    });

    scene_ = std::move(candidate);
    assetId_ = assetId;
    virtualPath_ = std::move(virtualPath);
    effectEntity_ = effectEntity;
    cameraEntity_ = cameraEntity;
    return {};
}

ParticleEditorResult ParticlePreviewSession::PublishWorkingCopy(
    const kb::scene::ParticleEffectAsset& asset) {
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
    if (scene_ != nullptr) renderer.ReleaseScene(*scene_);
    scene_.reset();
    assetId_ = {};
    virtualPath_.clear();
    effectEntity_ = {};
    cameraEntity_ = {};
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

} // namespace kb::particle_editor
