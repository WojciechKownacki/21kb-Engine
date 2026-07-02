#include "kb/render/runtime/RuntimeRenderResourceCache.hpp"

#include "kb/render/runtime/RuntimeRenderAssetDiscovery.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "runtime/RuntimeMaterialResourceEnsurer.hpp"
#include "runtime/RuntimeMeshResourceEnsurer.hpp"
#include "runtime/RuntimeRenderResourceLifecycle.hpp"
#include "runtime/RuntimeRenderResourcePruner.hpp"
#include "runtime/RuntimeTextureResourceEnsurer.hpp"

namespace kb::render {

void RuntimeRenderResourceCache::Reserve(const RuntimeRenderResourceCacheReserveDesc& desc) {
    if (desc.meshes > 0U) {
        meshes_.reserve(desc.meshes);
    }
    if (desc.materials > 0U) {
        materials_.reserve(desc.materials);
        embeddedMaterials_.reserve(desc.materials);
    }
    if (desc.textures > 0U) {
        textures_.reserve(desc.textures);
    }
}

void RuntimeRenderResourceCache::EnsureSceneResources(const RuntimeRenderResourceEnsureContext& context) {
    context.assetDiscovery.Ensure(context.scene, context.currentFrame);
    EnsureMeshResources(context);
    EnsureMaterialResources(context);
    EnsureTextureResources(context);
}

void RuntimeRenderResourceCache::EnsureMeshResources(const RuntimeRenderResourceEnsureContext& context) {
    RuntimeMeshResourceEnsurer::Ensure(context, meshes_, embeddedMaterials_);
}

void RuntimeRenderResourceCache::EnsureMaterialResources(const RuntimeRenderResourceEnsureContext& context) {
    RuntimeMaterialResourceEnsurer::Ensure(context, materials_, embeddedMaterials_);
}

void RuntimeRenderResourceCache::EnsureTextureResources(const RuntimeRenderResourceEnsureContext& context) {
    RuntimeTextureResourceEnsurer::Ensure(context, materials_, embeddedMaterials_, textures_);
}

void RuntimeRenderResourceCache::ReleaseScene(kb::scene::Scene& scene, SceneRenderer* sceneRenderer) noexcept {
    RuntimeRenderResourceLifecycle::ReleaseScene(scene, sceneRenderer, meshes_, materials_, embeddedMaterials_, textures_);
}

void RuntimeRenderResourceCache::DestroyAll(SceneRenderer* sceneRenderer) noexcept {
    RuntimeRenderResourceLifecycle::DestroyAll(sceneRenderer, meshes_, materials_, embeddedMaterials_, textures_);
}

void RuntimeRenderResourceCache::InvalidateMaterials(SceneRenderer* sceneRenderer) noexcept {
    if (sceneRenderer == nullptr) {
        materials_.clear();
        embeddedMaterials_.clear();
        return;
    }
    for (const auto& [materialKey, resource] : materials_) {
        static_cast<void>(materialKey);
        sceneRenderer->ResourceMap().UnbindMaterialHandle(resource.handle);
        sceneRenderer->Resources().DestroyMaterial(resource.handle);
    }
    materials_.clear();

    for (const auto& [materialKey, resource] : embeddedMaterials_) {
        static_cast<void>(materialKey);
        sceneRenderer->ResourceMap().UnbindMaterialHandle(resource.handle);
        sceneRenderer->Resources().DestroyMaterial(resource.handle);
    }
    embeddedMaterials_.clear();
}

void RuntimeRenderResourceCache::PruneUnused(
    std::span<const kb::scene::Scene*> submittedScenes,
    const RuntimeFrameResourceReferences& frameReferences,
    SceneRenderer& sceneRenderer,
    std::uint64_t currentFrame,
    std::uint64_t retentionFrames) {
    RuntimeRenderResourcePruner::PruneUnused(
        submittedScenes,
        frameReferences,
        sceneRenderer,
        currentFrame,
        retentionFrames,
        meshes_,
        materials_,
        embeddedMaterials_,
        textures_);
}

RuntimeRenderResourceCacheStats RuntimeRenderResourceCache::Stats() const noexcept {
    return RuntimeRenderResourceCacheStats{
        .meshCount = static_cast<std::uint32_t>(meshes_.size()),
        .materialCount = static_cast<std::uint32_t>(materials_.size() + embeddedMaterials_.size()),
        .textureCount = static_cast<std::uint32_t>(textures_.size()),
        .meshCapacity = static_cast<std::uint32_t>(meshes_.bucket_count()),
        .materialCapacity = static_cast<std::uint32_t>(materials_.bucket_count() + embeddedMaterials_.bucket_count()),
        .textureCapacity = static_cast<std::uint32_t>(textures_.bucket_count()),
    };
}

} // namespace kb::render
