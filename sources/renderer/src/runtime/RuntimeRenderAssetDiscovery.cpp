#include "kb/render/runtime/RuntimeRenderAssetDiscovery.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialTypeAssetLoader.hpp"
#include "kb/render/resources/PostProcessProfileAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#include <memory>

namespace kb::render {
namespace {

template <typename Loader>
void RegisterLoaderIfMissing(kb::assets::AssetManager& manager, std::string_view type) {
    if (!manager.HasLoaderForType(type)) {
        static_cast<void>(manager.RegisterLoader(std::make_unique<Loader>()));
    }
}

} // namespace

void RuntimeRenderAssetDiscovery::Ensure(kb::scene::Scene& scene, std::uint64_t currentFrame) {
    if (registeredScenes_.insert(scene.Id()).second) {
        kb::assets::AssetManager& manager = scene.Assets().Manager();
        RegisterLoaderIfMissing<RenderMeshAssetLoader>(manager, "RenderMesh");
        RegisterLoaderIfMissing<RenderMaterialAssetLoader>(manager, "RenderMaterial");
        RegisterLoaderIfMissing<RenderMaterialFunctionAssetLoader>(manager, kRenderMaterialFunctionAssetType);
        RegisterLoaderIfMissing<RenderMaterialGraphAssetLoader>(manager, kRenderMaterialGraphAssetType);
        RegisterLoaderIfMissing<RenderMaterialInstanceAssetLoader>(manager, "RenderMaterialInstance");
        RegisterLoaderIfMissing<RenderMaterialTypeAssetLoader>(manager, kRenderMaterialTypeAssetType);
        RegisterLoaderIfMissing<PostProcessProfileAssetLoader>(manager, kPostProcessProfileAssetType);
        RegisterLoaderIfMissing<RenderTextureAssetLoader>(manager, "RenderTexture");
        // ParticleEffectAssetLoader is registered unconditionally by every kb::scene::Scene's
        // own constructor (Scene.cpp, mirrors PhysicsLayersAssetLoader's precedent) - kb::scene
        // never depends on kb::render, so it does not belong in this renderer-only discovery
        // list. The built-in particle quad mesh needs no registration at all here - see
        // BuiltInParticleQuadMesh.hpp's own doc comment for why it bypasses AssetRegistry
        // entirely (RuntimeMeshResourceEnsurer resolves it directly).
    }

    Refresh(scene, currentFrame);
}

void RuntimeRenderAssetDiscovery::ReserveSceneCount(std::uint32_t sceneCount) {
    if (sceneCount > 0U) {
        registeredScenes_.reserve(sceneCount);
        lastDiscoveryFrames_.reserve(sceneCount);
    }
}

void RuntimeRenderAssetDiscovery::ReleaseScene(std::uint64_t sceneId) noexcept {
    registeredScenes_.erase(sceneId);
    lastDiscoveryFrames_.erase(sceneId);
}

void RuntimeRenderAssetDiscovery::Clear() noexcept {
    registeredScenes_.clear();
    lastDiscoveryFrames_.clear();
}

void RuntimeRenderAssetDiscovery::SetDiscoveryIntervalFrames(std::uint64_t frameInterval) noexcept {
    discoveryIntervalFrames_ = frameInterval;
}

std::uint64_t RuntimeRenderAssetDiscovery::DiscoveryIntervalFrames() const noexcept {
    return discoveryIntervalFrames_;
}

void RuntimeRenderAssetDiscovery::SetDiscoveryEnabled(bool enabled) noexcept {
    discoveryEnabled_ = enabled;
}

bool RuntimeRenderAssetDiscovery::DiscoveryEnabled() const noexcept {
    return discoveryEnabled_;
}

RuntimeRenderAssetDiscoveryStats RuntimeRenderAssetDiscovery::Stats() const noexcept {
    return RuntimeRenderAssetDiscoveryStats{
        .registeredSceneCount = static_cast<std::uint32_t>(registeredScenes_.size()),
        .discoverySceneCount = static_cast<std::uint32_t>(lastDiscoveryFrames_.size()),
        .discoverySceneCapacity = static_cast<std::uint32_t>(lastDiscoveryFrames_.bucket_count()),
    };
}

void RuntimeRenderAssetDiscovery::Refresh(kb::scene::Scene& scene, std::uint64_t currentFrame) {
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    std::uint64_t& lastDiscoveryFrame = lastDiscoveryFrames_[scene.Id()];
    if (lastDiscoveryFrame != 0ULL) {
        if (!discoveryEnabled_) {
            return;
        }
        if (lastDiscoveryFrame == currentFrame) {
            return;
        }
        if (discoveryIntervalFrames_ != 0ULL &&
            currentFrame < lastDiscoveryFrame + discoveryIntervalFrames_) {
            return;
        }
    }

    static_cast<void>(manager.DiscoverMountedAssets());
    lastDiscoveryFrame = currentFrame;
}

} // namespace kb::render
